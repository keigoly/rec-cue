import AppKit

/// The app as a person uses it (started without a command-line mode): one recording at a time, started, paused and
/// stopped from the window or the menu bar. config.ini is read at each start, as in the Windows version.
@MainActor
final class Controller {
    enum State: Equatable { case idle, starting, recording, saving }

    /// What happened, for しおり and the notices (the observers only redraw).
    enum Event {
        case starting, started, failedStart, saving, paused, resumed
        case cancelled   // stopped while it was starting: nothing was recorded, no file
        case checkStarted, checkEnded   // the level check began / was stopped (the button, or its time was up)
        case finished(J)   // the recording's result
        case shot(Shot)
    }

    struct Shot {
        var ok = false
        var inRecording = false   // counted in the recording that runs (n: its number there)
        var n = 0
        var clip = false          // on the clipboard too
        var path = ""
        var error = ""
    }

    private(set) var state: State = .idle
    private(set) var audioOnly = false
    private(set) var recorder: Recorder?
    private(set) var lastResult: J?
    private(set) var lastFile: URL?
    /// Screenshots taken in the recording that runs (the camera's number).
    private(set) var shots = 0
    private var shooting = false
    private var started = false   // the recording got going (else its end is a failed start)
    /// 音量を確認: the level check that runs (only while idle), and since when (system uptime).
    private(set) var check: LevelCheck?
    private(set) var checkSince = 0.0
    /// The check stops itself after this many seconds (the window's tick looks).
    var checkLimit = 60.0
    /// The window warns when the Mac's sound has been silent this long while recording (tests shorten it).
    var silenceWarnMs = 60000.0
    /// The window and the menu bar redraw on every change.
    var observers: [() -> Void] = []
    var events: [(Event) -> Void] = []
    /// Tests (--ui-autotest): change the options of each recording (the test window instead of the screen, no mic).
    var testOptions: ((inout RecordOptions) -> Void)?
    /// … and of each screenshot (only Rec Cue!'s own windows, not the screen; not on the clipboard).
    var testShot: ((inout Screenshot.Options) -> Void)?
    /// … and of the level check (only Rec Cue!'s own sound, no microphone).
    var testCheck: ((inout LevelCheck.Options) -> Void)?
    /// Tests: no dialogs (a modal one would wait for a click that never comes), only the log.
    var quiet = false
    /// Tests: a dialog closes itself after this many seconds and leaves the keyboard to whoever is typing.
    var testAlertSeconds: Double?
    /// Dialogs asked for so far, quiet ones too (the autotest counts them).
    private(set) var alerts = 0

    var paused: Bool { recorder?.pauses.paused ?? false }
    var elapsed: Double { recorder?.fileSeconds ?? 0 }

    private func changed() {
        for f in observers { f() }
    }

    private func emit(_ e: Event) {
        for f in events { f(e) }
    }

    /// 録画 (audioOnly: false) or 録音だけ (true).
    func start(audioOnly: Bool) {
        guard state == .idle else { return }
        endCheck(said: false)   // a recording takes over from the level check
        self.audioOnly = audioOnly   // also what a failed start was meant to be (しおり's line)
        guard CGPreflightScreenCaptureAccess() else {
            CGRequestScreenCaptureAccess()
            emit(.failedStart)
            alert("画面収録の許可がありません",
                  "システム設定 → プライバシーとセキュリティ → 画面とシステムオーディオ録音 で Rec Cue! をオンにしてから、"
                  + "もう一度押してください。")
            return
        }
        let (cfg, problems) = Config.load()
        var o = RecordOptions(config: cfg, problems: problems)
        o.seconds = 0   // until ■
        o.audioOnly = audioOnly
        testOptions?(&o)
        let r = Recorder(o)
        let box = Unchecked(self)
        r.onStarted = { [weak r] in   // weak: the recorder holds this closure (a strong r kept every recording alive)
            Task { @MainActor in
                let c = box.value
                guard let r, c.recorder === r else { return }
                c.started = true
                guard c.state == .starting else { return }   // ■ came first: it is being saved already
                c.state = .recording
                c.changed()
                c.emit(.started)
            }
        }
        // it can end by itself (a stopped capture, a full disk, a failed writer, SIGTERM): 保存中 while the file is
        // finished, as after ■ (the window said 録画中 until it was saved, and ❚❚ still reached it)
        r.onStopping = { [weak r] in
            Task { @MainActor in
                let c = box.value
                guard let r, c.recorder === r, c.state == .recording || c.state == .starting else { return }
                c.state = .saving
                c.changed()
                c.emit(.saving)
            }
        }
        recorder = r
        shots = 0
        started = false
        state = .starting
        Running.set(r)
        changed()
        emit(.starting)
        Log.event("ui.start", ok: true, msg: audioOnly ? "sound only" : "picture and sound")
        Task { @MainActor in
            let result = await r.run()
            self.finished(r, result)
        }
    }

    func togglePause() {
        guard state == .recording, let r = recorder else { return }
        if r.pauses.paused { r.resume() } else { r.pause() }
        changed()
        emit(r.pauses.paused ? .paused : .resumed)
    }

    /// ■: the file is finished; the state goes back to idle when it is.
    func stop() {
        guard state == .recording || state == .starting, let r = recorder else { return }
        state = .saving
        changed()
        emit(.saving)
        r.requestStop(by: "app", "■")
    }

    /// The screen recorded (the configured one), as a PNG and on the clipboard. While recording it is counted in the
    /// recording and noted in its record with the time in the file.
    func screenshot() {
        guard !shooting else { return }   // the previous one is still being written
        shooting = true
        let (cfg, _) = Config.load()
        var s = Screenshot.Options()
        s.monitor = cfg.monitor
        s.dir = cfg.resolvedShotDir
        s.prefix = cfg.namePrefix
        s.clipboard = cfg.shotClipboard
        testShot?(&s)
        let rec = state == .recording ? recorder : nil
        let at = rec?.fileSeconds ?? 0
        Task { @MainActor in
            let j = await Screenshot.take(s)
            self.shooting = false
            var shot = Shot()
            shot.ok = j["ok"]?.isTrue == true
            shot.clip = j["clipboard"]?.isTrue == true
            if case .str(let p)? = j["path"] { shot.path = p }
            if case .str(let e)? = j["error"] { shot.error = e }
            // listed in the recording only if that same recording still runs (not saving, not a new one)
            if shot.ok, let rec, self.recorder === rec, self.state == .recording {
                self.shots += 1
                shot.inRecording = true
                shot.n = self.shots
                rec.noteShot(shot.path, at: at)
            }
            Log.event("ui.shot", ok: shot.ok, msg: shot.inRecording ? String(format: "#%d at %.1f s", shot.n, at)
                                                                     : (shot.ok ? "outside a recording" : shot.error))
            self.changed()
            self.emit(.shot(shot))
        }
    }

    /// 音量を確認: the microphone and the Mac's sound as config.ini says, on the meters only, until stopped, a
    /// recording starts or `checkLimit` is up. Started again, it opens the devices again (a changed setting).
    func startCheck() {
        guard state == .idle else { return }
        endCheck(said: false)
        let (cfg, _) = Config.load()
        var o = LevelCheck.Options()
        o.mic = cfg.mic
        o.system = cfg.system
        o.micGainDb = cfg.micGainDb
        o.systemGainDb = cfg.systemGainDb
        o.monitor = cfg.monitor
        testCheck?(&o)
        let c = LevelCheck(o)
        check = c
        checkSince = ProcessInfo.processInfo.systemUptime
        changed()
        emit(.checkStarted)
        Log.event("ui.check", ok: true, msg: "start mic=\(o.mic) system=\(o.system)")
        Task { @MainActor in
            do {
                try await c.start()
            } catch {
                Log.event("check.start", ok: false, msg: "\(error)")
                guard self.check === c else { return }
                self.check = nil
                self.changed()
                self.emit(.checkEnded)
                self.alert("音量を確認できませんでした", "\(error)")
            }
        }
    }

    /// Stops the level check: by its button or its time (`said`: しおり says it is over), or quietly because a
    /// recording starts or the check starts again.
    func endCheck(said: Bool) {
        guard let c = check else { return }
        check = nil
        Log.event("ui.check", ok: true, msg: String(format: "stop after %.1f s", ProcessInfo.processInfo.systemUptime
                                                                                 - checkSince))
        Task { @MainActor in await c.stop() }
        changed()
        if said { emit(.checkEnded) }
    }

    /// The save folder was changed: the window's 保存先 shows the new folder, not the last file in the old one.
    func forgetLastFile() {
        lastFile = nil
    }

    func openFolder() {
        let dir = Config.load().0.resolvedOutDir
        Paths.ensure(dir)
        NSWorkspace.shared.open(dir)
    }

    private func finished(_ r: Recorder, _ result: J) {
        guard recorder === r else { return }
        let wasStarted = started
        Running.set(nil)
        recorder = nil
        state = .idle
        lastResult = result
        if case .str(let p)? = result["path"] { lastFile = URL(fileURLWithPath: p) }
        changed()
        if result["cancelled"]?.isTrue == true {
            emit(.cancelled)
            return
        }
        emit(wasStarted ? .finished(result) : .failedStart)
        if result["ok"]?.isTrue != true {
            var why = "ログを確かめてください (歯車 → ログを取得)。"
            if case .str(let e)? = result["error"] { why = e }
            if case .str(let by)? = result["ended_by"], by != "time", by != "app", by != "user", by != "signal" {
                why = "途中で終わりました (\(by))。録れた所までは保存しています。"
            }
            alert(wasStarted ? "録画がうまく終わりませんでした" : "録画を始められませんでした", why)
        }
    }

    /// A dialog for a failure, shown from the run loop rather than inside the main-actor job that asks for it: runModal
    /// there held up every other main-actor job until it was closed (a recording stopped meanwhile stayed 保存中, the
    /// clock and the meters stood still).
    func alert(_ title: String, _ text: String) {
        Log.event("ui.alert", ok: false, msg: "\(title): \(text)")
        alerts += 1
        if quiet { return }
        let close = testAlertSeconds
        RunLoop.main.perform {
            MainActor.assumeIsolated {
                let a = NSAlert()
                a.messageText = title
                a.informativeText = text
                a.alertStyle = .warning
                if let s = close {
                    let t = Timer(timeInterval: s, repeats: false) { _ in NSApp.abortModal() }
                    RunLoop.main.add(t, forMode: .modalPanel)
                } else {
                    NSApp.activate(ignoringOtherApps: true)
                }
                a.runModal()
            }
        }
    }
}
