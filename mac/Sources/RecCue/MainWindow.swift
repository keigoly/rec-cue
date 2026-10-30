import AppKit
import AVFoundation

/// The main window: WindowView fed from the Controller, with 湊 しおり in the corner. Closing it only hides it (the
/// recording goes on; the menu bar icon stays). Timers run only while something moves (recording, the deck opening or
/// closing) and once when a passing line or a notice ends: idle costs no CPU.
@MainActor
final class MainWindow: NSObject, NSWindowDelegate {
    let window: NSWindow
    private let view = WindowView(frame: NSRect(origin: .zero, size: WindowView.size))
    private let controller: Controller
    private var timer: Timer?
    private var split: Double = 0
    private let t0 = Date()
    // the meters: per source the last tick's level, and the peak hold (dBFS) with when it was last raised
    private var meter: [String: AudioMixer.Level] = [:]
    private var hold: [String: (db: Double, at: Double)] = [:]
    // しおり (Shiori.swift): a line that fits the moment, chosen when the moment changes (never on a timer while idle),
    // and for a few seconds a passing one (a screenshot, the pin, a notice while idle)
    private let lines = Lines()
    private var line = "", lineTone = Tone.normal
    private var passing = "", passingTone = Tone.normal, passingUntil = 0.0
    private var warnScene: Scene?   // the warning she is giving about the recording
    private var sayMinute = 0       // the minute of the recording she last looked at
    private var checkHeard = (mic: false, sys: false, quiet: false)   // what she has said about this level check
    private var inactiveSince: Double?
    // a notice (a screenshot saved) holds the status line for a while when recording; in her bubble otherwise
    private var notice = "", noticeTone = Tone.normal, noticeUntil = 0.0
    // the recording's own status line, from the last tick, and whether it needs attention (the amber deck)
    private var recStatus = "", recTone = Tone.normal, alert = false
    private var flashUntil = 0.0    // the camera button lights up briefly after a shot
    private var hotkeys: Hotkeys?   // the shortcuts in any app (none in --ui-autotest)
    private var settings = false    // the settings view (the gear) in place of the deck and the rows
    private var packing = false     // ログを取得 is writing its ZIP (on another thread)
    /// The last ZIP of ログを取得 (the autotest reads it).
    private(set) var lastPack: (zip: URL, result: LogPack.Result)?
    /// Tests (--ui-autotest): the folder 変更 picks, without the panel (shot: the screenshots'; nil: cancelled) …
    var testPick: ((Bool) -> URL?)?
    /// … and where ログを取得 writes (not Downloads; Finder is not opened).
    var testLogsDir: URL?

    private static func now() -> Double { ProcessInfo.processInfo.systemUptime }

    init(controller: Controller, hotkeys: Bool = true) {
        self.controller = controller
        window = NSWindow(contentRect: NSRect(origin: .zero, size: WindowView.size),
                          styleMask: [.titled, .closable, .miniaturizable], backing: .buffered, defer: false)
        super.init()
        window.title = "Rec Cue!"
        window.appearance = NSAppearance(named: .darkAqua)
        window.backgroundColor = Pal.canvas
        window.isReleasedWhenClosed = false
        window.contentView = view
        window.delegate = self
        window.center()
        if Config.load().0.topmost { window.level = .floating }
        view.guideIdle = UiSnapshot.image("guide-idle")
        view.guideRecImage = UiSnapshot.image("guide-rec")
        view.onPart = { [weak self] p in self?.activate(p) }
        let sw = Stopwatch()
        let problems = lines.load()
        for p in problems { Log.event("lines", ok: false, msg: p) }
        Log.event("lines.load", ok: problems.isEmpty, ms: sw.ms,
                  msg: "lines=\(lines.count) mac_dropped=\(lines.macDropped) mac_added=\(lines.macAdded)")
        controller.observers.append { [weak self] in self?.refresh() }
        controller.events.append { [weak self] e in self?.on(e) }
        if hotkeys { useHotkeys(Config.load().0) }
        view.tipText = { [weak self] p in self?.tip(p) ?? "" }
        view.installTips()
        window.initialFirstResponder = view
        window.makeFirstResponder(view)
        say(.start)
    }

    /// What the window shows now (the autotest reads it).
    var shown: UiModel { view.model }
    /// The autotest: a click on the window while another app is in front reaches the button (not only the window).
    var takesFirstClick: Bool { view.acceptsFirstMouse(for: nil) }

    /// The autotest: a key as if pressed in the window (Tab 48, Space 49, Esc 53, ↓ 125).
    func press(_ keyCode: UInt16, shift: Bool = false) {
        let chars = keyCode == 48 ? "\t" : keyCode == 49 ? " " : keyCode == 53 ? "\u{1b}" : ""
        guard let e = NSEvent.keyEvent(with: .keyDown, location: .zero, modifierFlags: shift ? [.shift] : [],
                                       timestamp: ProcessInfo.processInfo.systemUptime, windowNumber: window.windowNumber,
                                       context: nil, characters: chars, charactersIgnoringModifiers: chars,
                                       isARepeat: false, keyCode: keyCode) else { return }
        view.keyDown(with: e)
    }

    /// The autotest: a click on a part, if it can be used now.
    func click(_ p: Part) {
        if view.model.enabled(p) { activate(p) }
    }

    func show() {
        hotkeys?.register(Config.load().0)   // picks up a changed config.ini
        window.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
        refresh()
    }

    func windowShouldClose(_ sender: NSWindow) -> Bool {
        window.orderOut(nil)   // hide only: the menu bar icon brings it back
        return false
    }

    func windowDidResignKey(_ notification: Notification) {
        inactiveSince = Self.now()
        view.model.pressed = nil
    }

    func windowDidBecomeKey(_ notification: Notification) {
        // used again after 20 minutes or more
        if let t = inactiveSince, Self.now() - t > 20 * 60, controller.state == .idle { say(.back) }
    }

    // MARK: しおり

    private func context() -> SayContext {
        var c = SayContext()
        c.audio = controller.audioOnly
        c.recKey = hotkeys?.text(.record) ?? ""   // a line naming a shortcut that does not work is left out
        c.audioKey = hotkeys?.text(.audio) ?? ""
        c.shotKey = hotkeys?.text(.shot) ?? ""
        return c
    }

    /// Registers the shortcuts (the autotest: keys of its own that nobody uses).
    func useHotkeys(_ cfg: Config) {
        let hk = hotkeys ?? Hotkeys { [weak self] a in self?.hotkey(a) }
        hk.register(cfg)
        hotkeys = hk
    }

    /// A shortcut pressed in any app (the Windows version's hotkeys): ● / the microphone button, or スクショ.
    private func hotkey(_ a: Hotkeys.Action) {
        let c = controller
        Log.event("ui.hotkey_pressed", ok: true, msg: "\(a)")
        switch a {
        case .record, .audio:
            let audio = a == .audio
            if c.state == .starting || c.state == .saving { return }
            if c.state == .recording && c.audioOnly != audio {
                showNotice(audio ? "録画中は録音を始められません" : "録音中は録画を始められません", .warn)
                return
            }
            c.state == .recording ? c.stop() : c.start(audioOnly: audio)
        case .shot: c.screenshot()
        }
    }

    /// The tooltip of a part (the Windows version's texts, with the Mac's formats).
    func tip(_ p: Part) -> String {
        let c = controller, m = view.model
        func key(_ a: Hotkeys.Action) -> String { hotkeys?.keys[a]?.shown ?? "なし" }
        switch p {
        case .guide: return "湊 しおり (案内役) — クリックすると、ほかのひとことを話します"
        case .record:
            return c.state == .recording && !c.audioOnly ? "録画を止めて保存 (\(key(.record)))"
                                                         : "録画: 画面とマイクと PC の音声 (\(key(.record)))"
        case .audio:
            return c.state == .recording && c.audioOnly ? "録音を止めて保存 (\(key(.audio)))"
                                                        : "録音だけ: マイクと PC の音声 (\(key(.audio)))"
        case .pin: return window.level == .floating ? "最前面の固定をやめる" : "最前面に固定する"
        case .shot: return "スクショ (\(key(.shot))) — Rec Cue! の窓は写りません"
        case .gear: return settings ? "設定を閉じる" : "設定 — 保存先・ショートカットキー・ログ・リンク"
        case .back: return "戻る (Esc)"
        case .outDir, .shotDir: return "Finder で開く"
        case .outChange, .shotChange:
            if !m.enabled(p) { return "録画中・録音中は変えられません" }
            return p == .outChange ? "録画と録音の保存先を、Finder の画面で選ぶ"
                                   : "スクショの保存先を選ぶ — 録画と同じフォルダーを選ぶと「録画と同じ場所」に戻ります"
        case .logs:
            return packing ? "ログをまとめています"
                           : "ログを取得 — 直近 7 日のログと録画ごとの記録を ZIP にまとめ、ダウンロードに置きます"
        case .github: return Config.load().0.githubUrl
        case .site: return Config.load().0.siteUrl
        case .format:
            return m.enabled(.format) ? "録音の形式: \(m.audioFormat) — クリックで M4A・WAV から選べます (録画はいつも MP4)" : ""
        case .pause, .audioPause:
            guard m.enabled(p) else { return "" }
            let what = p == .pause ? "録画" : "録音"
            return c.paused ? "\(what)を再開 (止めていた間は入りません)" : "\(what)を一時停止 (■ で停止して保存)"
        default: return ""
        }
    }

    private func pick(_ s: Scene, _ c: SayContext) -> String {
        let l = lines.pick(s, c)
        Log.event("ui.say", ok: !l.isEmpty, msg: "scene=\(s.name) line=\(l.isEmpty ? "-" : lines.lastSource)")
        return l
    }

    /// What she says from now on, until the next moment.
    private func say(_ s: Scene, _ c: SayContext? = nil) {
        line = pick(s, c ?? context())
        lineTone = s.tone
        refresh()
    }

    /// A line for a few seconds, then the moment's line again.
    private func passText(_ text: String, _ seconds: Double, _ tone: Tone) {
        guard !text.isEmpty else { return }
        passing = text
        passingTone = tone
        passingUntil = Self.now() + seconds
        refresh(after: seconds)
        refresh()
    }

    private func sayPassing(_ s: Scene, _ seconds: Double, _ c: SayContext? = nil) {
        passText(pick(s, c ?? context()), seconds, s.tone)
    }

    /// A short message (a screenshot saved) for 4 s, 8 s for a failure: in the status line while recording, in her
    /// bubble otherwise.
    private func showNotice(_ text: String, _ tone: Tone) {
        let s = tone == .bad ? 8.0 : 4.0
        notice = text
        noticeTone = tone
        noticeUntil = Self.now() + s
        refresh(after: s)
        if controller.state == .idle { passText(text, s, tone) }
    }

    /// A click on her or her bubble: something else that fits the moment (while idle a tip or a word of hers).
    private func talk() {
        let c = controller
        guard c.state != .starting && c.state != .saving else { return }
        passingUntil = 0
        var ctx = context()
        if c.state == .recording && c.paused {
            say(.pause, ctx)
        } else if c.state == .recording, let w = warnScene {
            say(w, ctx)
        } else if c.state == .recording {
            ctx.sec = c.elapsed
            ctx.n = Int(ctx.sec / 60)
            say(c.audioOnly ? .audioOn : .recOn, ctx)
        } else if c.check != nil {
            say(.check, ctx)
        } else {
            say(lines.roll(10) < 4 ? .tip : .talk, ctx)
        }
    }

    private func on(_ e: Controller.Event) {
        let c = controller
        switch e {
        case .starting:
            showSettings(false)   // the deck shows the recording (its time and ■)
            passingUntil = 0
            noticeUntil = 0
            warnScene = nil
            recStatus = ""
            alert = false
            sayMinute = 0
            meter.removeAll()
            hold.removeAll()
            say(.starting)
        case .started:
            say(c.audioOnly ? .audio : .rec)
        case .failedStart:
            say(.failStart)
        case .cancelled:
            showNotice(c.audioOnly ? "録音を取りやめました" : "録画を取りやめました", .normal)
        case .checkStarted:
            meter.removeAll()
            hold.removeAll()
            checkHeard = (false, false, false)
            say(.check)
        case .checkEnded:
            say(.checkEnd)
        case .saving:
            say(.saving)
        case .paused, .resumed:
            noticeUntil = 0
            warnScene = nil   // a warning that still holds after the pause is said again
            if case .paused = e { say(.pause) } else { say(.resume) }
        case .finished(let r):
            // she reports it until the next moment
            var ctx = context()
            ctx.shots = c.shots
            ctx.sec = Self.num(r["summary"]?["duration_s"])
            ctx.mb = Self.num(r["summary"]?["file_bytes"]) / 1048576
            if case .str(let by)? = r["ended_by"] { ctx.autoStop = ["stream", "writer", "disk", "wav_full"].contains(by) }
            say(r["verify"]?["ok"]?.isTrue == true ? .saved : .failSave, ctx)
        case .shot(let s):
            guard s.ok else {
                showNotice("スクショを撮れませんでした (\(s.error))", .bad)   // in her bubble while idle
                if c.state == .recording { sayPassing(.failShot, 8) }
                // hidden: the window comes forward with the notice, without the keyboard (a dialog took the keyboard
                // from the meeting in front; the Windows version only says it in the window)
                if !window.isVisible { window.orderFrontRegardless() }
                return
            }
            flashUntil = Self.now() + 0.6
            refresh(after: 0.6)
            if c.state == .recording {
                let name = (s.path as NSString).lastPathComponent
                showNotice((s.inRecording ? "スクショ \(s.n) 枚目を保存" : "スクショを保存")
                           + (s.clip ? "し、コピーしました" : "しました: " + name), .good)
            }
            var ctx = context()
            ctx.n = s.n
            ctx.clip = s.clip
            sayPassing(s.inRecording ? .shot : .shotIdle, 4, ctx)
        }
        refresh()
    }

    private static func num(_ j: J?) -> Double {
        switch j {
        case .num(let v, _)?: return v
        case .int(let v)?: return Double(v)
        default: return 0
        }
    }

    // MARK: model

    /// The deck: the recording side opens once the recording runs, and closes after it is saved.
    private var splitTarget: Double {
        let c = controller
        return c.state == .recording || c.state == .saving ? (c.audioOnly ? -1 : 1) : 0
    }

    func refresh() {
        let c = controller
        let cfg = Config.load().0
        let now = Self.now()
        var m = view.model
        let checking = c.state == .idle && c.check != nil
        switch c.state {
        case .idle: m.phase = checking ? .checking : .idle
        case .starting: m.phase = .starting
        case .recording: m.phase = .recording
        case .saving: m.phase = .saving
        }
        m.audioOnly = c.state != .idle && c.audioOnly
        m.paused = c.state == .recording && c.paused
        m.timer = Self.clock(c.elapsed)
        m.split = split
        m.status = switch c.state {
        case .starting: "準備中…"
        case .saving: "ファイルを仕上げています…"
        case .recording where c.paused: "一時停止中 — ▶ で再開、■ で停止して保存します"
        case .recording: (c.audioOnly ? "録音中" : "録画中") + " — ■ を押すと停止して保存します"
        case .idle: ""
        }
        m.tone = .normal
        if c.state == .recording && !recStatus.isEmpty {
            m.status = recStatus
            m.tone = recTone
        }
        if c.state == .recording && now < noticeUntil {
            m.status = notice
            m.tone = noticeTone
        }
        m.alert = c.state == .recording && alert
        m.audioFormat = cfg.audioFormat.uppercased()
        m.recHotkey = hotkeys?.text(.record) ?? ""
        m.audioHotkey = hotkeys?.text(.audio) ?? ""
        m.shotHotkey = hotkeys?.text(.shot) ?? ""
        // screens: the main one first, as Rec Cue! numbers them
        let ds = Devices.displays()
        m.monitors = ds.map { d in
            let f = NSScreen.screens.first { ($0.deviceDescription[NSDeviceDescriptionKey("NSScreenNumber")] as? NSNumber)?
                .uint32Value == d.id }?.frame ?? .zero
            return CGRect(x: f.minX, y: -f.maxY, width: f.width, height: f.height)   // y down, like the window
        }
        m.selected = Devices.screenIndex(cfg.monitor, of: ds.count)
        if let d = ds.indices.contains(m.selected) ? ds[m.selected] : nil {
            m.screenText = "\(m.selected + 1): \(d.main ? "メイン" : d.name) \(d.pixelWidth)×\(d.pixelHeight)"
        }
        m.mic = UiModel.Channel(setting: Self.micLabel(cfg.mic))
        m.sys = UiModel.Channel(setting: cfg.system.lowercased() == "none" ? "なし" : "自動 (すべてのアプリの音)")
        if c.state == .recording || c.state == .saving || checking {
            if let l = meter["mic"] {
                m.mic.mode = cfg.mic.lowercased() == "auto" ? "自動" : cfg.mic.lowercased() == "default" ? "既定" : ""
                m.mic.device = l.name
                m.mic.active = true
                m.mic.db = l.db
                m.mic.hold = hold["mic"]?.db ?? -120
            }
            if let l = meter["system"] {
                m.sys.mode = "自動"
                m.sys.device = "すべてのアプリの音"
                m.sys.active = true
                m.sys.db = l.db
                m.sys.hold = hold["system"]?.db ?? -120
                m.sys.warn = c.state == .recording && l.silentMs > c.silenceWarnMs   // the other party may be missing
            }
        }
        if let f = c.recorder?.file, c.state != .idle {
            m.pathLabel = c.audioOnly ? "録音中" : "録画中"
            m.path = (f.path as NSString).abbreviatingWithTildeInPath
        } else {
            m.pathLabel = "保存先"
            m.path = ((c.lastFile ?? cfg.resolvedOutDir).path as NSString).abbreviatingWithTildeInPath
        }
        m.settings = settings
        if settings {   // the folders and the shortcuts in use
            m.outDir = Self.shown(cfg.resolvedOutDir)
            m.shotDir = cfg.shotDir.isEmpty ? "" : Self.shown(cfg.resolvedShotDir)
            m.keys = Hotkeys.Action.allCases.map { a in
                guard let k = hotkeys?.keys[a], !k.text.isEmpty else { return UiModel.Key() }
                return UiModel.Key(text: k.text, taken: !k.registered)
            }
            m.logsBusy = packing
        }
        m.guideRec = c.state == .recording && !c.paused
        m.topmost = window.level == .floating
        m.pulse = c.state == .recording ? 0.5 + 0.5 * cos(.pi * Date().timeIntervalSince(t0)) : 1
        m.shots = c.shots
        m.shotFlash = now < flashUntil
        // しおり: a passing line first, otherwise the moment's
        let pass = now < passingUntil
        m.comment = pass ? passing : line
        m.commentTone = pass ? passingTone : lineTone
        view.model = m
        view.window?.invalidateCursorRects(for: view)
        window.title = switch c.state {
        case .idle: "Rec Cue!"
        default: "● " + (c.audioOnly ? "録音中" : "録画中") + (c.paused ? " (一時停止)" : "") + " - Rec Cue!"
        }
        // the 100-ms tick: while recording or checking, or until the deck has finished opening / closing
        let moving = c.state != .idle || checking || abs(split - splitTarget) > 0.001
        if moving && timer == nil {
            timer = Timer.scheduledTimer(withTimeInterval: 0.1, repeats: true) { [weak self] _ in
                MainActor.assumeIsolated { self?.tick() }
            }
        } else if !moving {
            timer?.invalidate()
            timer = nil
        }
    }

    /// Once more when something shown for a while (a passing line, a notice, the camera's flash) is over.
    private func refresh(after seconds: Double) {
        Timer.scheduledTimer(withTimeInterval: seconds + 0.05, repeats: false) { [weak self] _ in
            MainActor.assumeIsolated { self?.refresh() }
        }
    }

    /// Every 100 ms while something moves: the deck, the meters, しおり's minute lines.
    private func tick() {
        let c = controller
        stepSplit(splitTarget)
        let now = Self.now()
        let lv = c.state == .recording || c.state == .saving ? (c.recorder?.levels() ?? []) : (c.check?.levels() ?? [])
        for l in lv {
            meter[l.kind] = l
            // the peak hold rises at once; after 1.5 s without a new peak it falls 1.5 dB a tick (15 dB/s)
            if let h = hold[l.kind], l.db < h.db {
                if now - h.at > 1.5 { hold[l.kind] = (max(l.db, h.db - 1.5), h.at) }
            } else {
                hold[l.kind] = (l.db, now)
            }
        }
        if let check = c.check, c.state == .idle { tickCheck(check, since: now - c.checkSince) }
        if c.state == .recording, let r = c.recorder { tickWarnings(r, sources: Set(lv.map(\.kind))) }
        // しおり: every 10 minutes and at the milestones of her lines (minute 1, 5, 15, 30 …), unless she is warning
        if c.state == .recording {
            let minute = Int(c.elapsed) / 60
            if minute != sayMinute {
                sayMinute = minute
                let on: Scene = c.audioOnly ? .audioOn : .recOn
                if warnScene == nil && (minute % 10 == 0 || lines.milestone(on, minute)) {
                    var ctx = context()
                    ctx.n = minute
                    ctx.sec = Double(minute) * 60
                    say(on, ctx)
                }
            }
        }
        refresh()
    }

    /// The recording's status line (first match, as the Windows version): paused; the screen does not come through;
    /// the Mac's sound silent for over a minute; no microphone; no Mac's sound. A warning turns the deck amber, goes
    /// before a passing notice, and しおり says it once it has lasted 2 s (and a word when it is over).
    private func tickWarnings(_ r: Recorder, sources: Set<String>) {
        let c = controller
        let what = c.audioOnly ? "録音中" : "録画中"
        var st = what + " — ■ を押すと停止して保存します", tone = Tone.normal
        var warn: Scene?
        if c.paused {
            st = "一時停止中 — ▶ で再開、■ で停止して保存します"
        } else if !c.audioOnly && !r.videoCapturing {
            (st, tone, warn) = ("画面を取り込めていません (ロック中など)", .warn, .warnScreen)
        } else if sources.contains("system"), (meter["system"]?.silentMs ?? 0) > c.silenceWarnMs {
            (st, tone, warn) = ("PC の音声が 1 分以上無音です。ミュートを確認", .warn, .warnSys)
        } else if !sources.contains("mic") {
            st = what + (r.micProblem == "permission" ? " — マイクの許可がありません" : " — マイクなし")
            tone = r.o.mic.lowercased() == "none" ? .normal : .warn
            if tone == .warn { warn = .warnMic }
        } else if !sources.contains("system") {
            st = what + " — PC の音声なし"
            tone = r.o.system.lowercased() == "none" ? .normal : .warn
            if tone == .warn { warn = .warnNoSys }
        }
        if tone == .warn && !alert { noticeUntil = 0 }   // a warning that just appeared goes before a notice
        alert = tone == .warn
        recStatus = st
        recTone = tone
        if !c.paused && warn != warnScene && (warn == nil || c.elapsed >= 2) {
            say(warn ?? .recover)
            warnScene = warn
        }
    }

    /// The level check: しおり answers the meters (the microphone heard, the Mac's sound heard, no microphone sound for
    /// 8 s), and it stops itself when its time is up.
    private func tickCheck(_ check: LevelCheck, since s: Double) {
        let mic = check.micName != nil
        if s > 0.6 && !checkHeard.mic && mic && (meter["mic"]?.db ?? -120) > -45 {
            checkHeard.mic = true
            say(.checkMic)
        } else if s > 0.6 && !checkHeard.sys && check.system && (meter["system"]?.db ?? -120) > -50 {
            checkHeard.sys = true
            say(.checkSys)
        } else if s > 8 && !checkHeard.mic && !checkHeard.quiet && mic {
            checkHeard.quiet = true
            say(.checkQuiet)
        }
        if s > controller.checkLimit { controller.endCheck(said: true) }
    }

    /// 260 ms ease-in-out, as the Windows version (one tick = 100 ms, so in about three steps).
    private func stepSplit(_ target: Double) {
        guard abs(split - target) > 0.001 else {
            split = target
            return
        }
        let step = 0.1 / 0.26 * 2   // the whole way is up to 2 (from −1 to 1)
        split = split < target ? min(target, split + step) : max(target, split - step)
    }

    /// A folder as the window shows it (~/Movies/画面録画).
    static func shown(_ u: URL) -> String { (u.path as NSString).abbreviatingWithTildeInPath }

    static func clock(_ s: Double) -> String {
        let n = Int(s)
        return String(format: "%02d:%02d:%02d", n / 3600, n / 60 % 60, n % 60)
    }

    static func micLabel(_ v: String) -> String {
        switch v.lowercased() {
        case "auto": return "自動 (Mac の既定の入力)"
        case "default": return "既定のマイク — " + (AVCaptureDevice.default(for: .audio)?.localizedName ?? "(なし)")
        case "none": return "なし"
        default: return v
        }
    }

    // MARK: actions

    private func activate(_ p: Part) {
        let c = controller
        Log.event("ui.click", ok: true, msg: "\(p)")
        switch p {
        case .record: c.state == .recording && !c.audioOnly ? c.stop() : c.start(audioOnly: false)
        case .audio: c.state == .recording && c.audioOnly ? c.stop() : c.start(audioOnly: true)
        case .pause, .audioPause: c.togglePause()
        case .shot: c.screenshot()
        case .open: c.openFolder()
        case .pin:
            let on = window.level != .floating
            window.level = on ? .floating : .normal
            Config.set("topmost", on ? "1" : "0")
            Log.event("ui.topmost", ok: true, msg: on ? "on" : "off")
            sayPassing(on ? .pinOn : .pinOff, 3)
        case .format: popup(at: WindowView.format, [("M4A", "m4a"), ("WAV", "wav")], current: Config.load().0.audioFormat) {
            Config.set("audio_format", $0)
        }
        case .screen:
            let ds = Devices.displays()
            let items = ds.enumerated().map { i, d in ("\(i + 1): \(d.name) \(d.pixelWidth)×\(d.pixelHeight)", "\(i)") }
            popup(at: NSRect(x: 98, y: 120, width: 196, height: 28), items, current: "\(Config.load().0.monitor)") {
                Config.set("monitor", $0)
            }
        case .mic:
            var items: [(String, String)] = [("自動 (Mac の既定の入力)", "auto"), (Self.micLabel("default"), "default"),
                                             ("なし", "none")]
            items += Devices.microphones().map { ($0.localizedName, $0.localizedName) }
            popup(at: NSRect(x: 98, y: 156, width: 196, height: 28), items, current: Config.load().0.mic) {
                Config.set("mic", $0)
                if c.check != nil { c.startCheck() }   // check the newly chosen one
            }
        case .sys:
            popup(at: NSRect(x: 98, y: 192, width: 196, height: 28), [("自動 (すべてのアプリの音)", "auto"), ("なし", "none")],
                  current: Config.load().0.system) {
                Config.set("system", $0)
                if c.check != nil { c.startCheck() }
            }
        case .gear: showSettings(!settings)
        case .back: showSettings(false)
        case .outDir: openDir(shot: false)
        case .shotDir: openDir(shot: true)
        case .outChange: chooseDir(shot: false)
        case .shotChange: chooseDir(shot: true)
        case .logs: exportLogs()
        case .github: openLink(Config.load().0.githubUrl)
        case .site: openLink(Config.load().0.siteUrl)
        case .guide: talk()
        case .check: c.check != nil ? c.endCheck(said: true) : c.startCheck()
        }
        refresh()
    }

    /// The gear: the settings view in place of the deck and the rows (the header, しおり and the band stay), or back.
    func showSettings(_ on: Bool) {
        guard settings != on else { return }
        settings = on
        view.model.hot = nil
        view.model.pressed = nil
        view.model.focus = on ? .back : .gear
        Log.event("ui.view", ok: true, msg: on ? "settings" : "main")
        refresh()
    }

    /// A save folder in Finder (made first when it is not there yet).
    private func openDir(shot: Bool) {
        let cfg = Config.load().0
        let d = shot ? cfg.resolvedShotDir : cfg.resolvedOutDir
        Paths.ensure(d)
        let ok = NSWorkspace.shared.open(d)
        Log.event("ui.open_dir", ok: ok, msg: (shot ? "shot " : "out ") + d.path)
    }

    /// 変更: the Mac's folder picker, a sheet on the window, at the folder in use. The folder must take a file before it
    /// is kept. The default place is kept as an empty value (out_dir: ~/Movies/画面録画, shot_dir: with the
    /// recordings), so it goes on following that place. Used from the next recording / screenshot on.
    private func chooseDir(shot: Bool) {
        guard view.model.enabled(shot ? .shotChange : .outChange) else { return }
        let cfg = Config.load().0
        let cur = shot ? cfg.resolvedShotDir : cfg.resolvedOutDir
        Paths.ensure(cur)   // so that the picker opens in it
        view.model.pressed = nil
        let sw = Stopwatch()
        if let pick = testPick {
            useDir(shot: shot, pick(shot), ms: sw.ms)
            return
        }
        let panel = NSOpenPanel()
        panel.canChooseFiles = false
        panel.canChooseDirectories = true
        panel.canCreateDirectories = true
        panel.allowsMultipleSelection = false
        panel.directoryURL = cur
        panel.message = shot ? "スクショの保存先を選ぶ" : "録画と録音の保存先を選ぶ"
        panel.prompt = "選択"
        panel.beginSheetModal(for: window) { [weak self] r in
            MainActor.assumeIsolated { self?.useDir(shot: shot, r == .OK ? panel.url : nil, ms: sw.ms) }
        }
    }

    /// The folder picked for 変更 (nil: cancelled).
    private func useDir(shot: Bool, _ dir: URL?, ms: Double) {
        let kind = shot ? "shot" : "out"
        guard let dir else {
            Log.event("ui.folder", ok: true, ms: ms, msg: "kind=\(kind) cancelled")
            return
        }
        guard controller.state == .idle else {   // a recording started (shortcut, menu bar) while the picker was open
            Log.event("ui.folder", ok: false, ms: ms, msg: "kind=\(kind) a recording started meanwhile; not changed")
            return
        }
        if let why = Self.writeError(dir) {
            Log.event("ui.folder", ok: false, ms: ms, msg: "kind=\(kind) cannot write in \(dir.path): \(why)")
            passText("このフォルダーには保存できません。ほかの場所をお選びください。", 6, .bad)
            return
        }
        let base = shot ? Config.load().0.resolvedOutDir : Config().resolvedOutDir
        let value = Self.samePath(dir, base) ? "" : dir.path
        let ok = Config.set(shot ? "shot_dir" : "out_dir", value)
        if ok && !shot { controller.forgetLastFile() }   // the main view's 保存先 shows the new folder
        Log.event("ui.folder", ok: ok, ms: ms, msg: "kind=\(kind) default=\(value.isEmpty) \(dir.path)")
        if ok {
            passText(shot ? "スクショの保存先を変えました。" : "保存先を変えました。次の録画から使います。", 4, .good)
        } else {
            passText("保存先を書き込めませんでした。", 6, .bad)
        }
        refresh()
    }

    /// nil when the folder takes a new file (an empty one, removed again); otherwise why not. A read-only or
    /// unreachable place is not kept, so the next recording does not fail to start there.
    static func writeError(_ dir: URL) -> String? {
        let f = dir.appendingPathComponent(".rec-cue-write-test-\(getpid())-\(UInt32.random(in: 0...UInt32.max)).tmp")
        let fd = Darwin.open(f.path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0o600)
        guard fd >= 0 else { return String(cString: strerror(errno)) }
        close(fd)
        unlink(f.path)
        return nil
    }

    /// The same folder (links followed; macOS's disks do not tell case apart).
    static func samePath(_ a: URL, _ b: URL) -> Bool {
        a.resolvingSymlinksInPath().standardizedFileURL.path.lowercased()
            == b.resolvingSymlinksInPath().standardizedFileURL.path.lowercased()
    }

    /// ログを取得: the logs and records of the last week in one ZIP in Downloads, to attach as it is. Written on another
    /// thread (Downloads may be slow to answer); Finder then shows it selected.
    private func exportLogs() {
        guard !packing else { return }
        packing = true
        let dir = testLogsDir ?? LogPack.downloads
        Paths.ensure(dir)
        let zip = LogPack.freeName(dir), about = LogPack.about()
        Log.event("logs.export", ok: true, msg: "start \(zip.path)")
        let box = Unchecked(self)
        LogPack.running.enter()
        DispatchQueue.global(qos: .userInitiated).async {
            let r = LogPack.write(zip, days: LogPack.days, about: about)
            LogPack.running.leave()
            Task { @MainActor in box.value.packed(zip, r) }
        }
        refresh()
    }

    /// The ZIP is there: Finder shows it selected (to drag into a mail or a chat).
    private func packed(_ zip: URL, _ r: LogPack.Result) {
        packing = false
        lastPack = (zip, r)
        Log.event("logs.export", ok: r.error.isEmpty, ms: r.ms, msg: r.json(zip).text)
        if r.error.isEmpty {
            if testLogsDir == nil {
                NSWorkspace.shared.activateFileViewerSelecting([zip])
                Log.event("logs.show", ok: true)
            }
            passText("ログを ZIP にまとめ、ダウンロードに置きました。", 5, .good)
        } else {
            passText("ログをまとめられませんでした。", 8, .bad)
        }
        refresh()
    }

    private func openLink(_ s: String) {
        guard let u = URL(string: s), ["http", "https"].contains(u.scheme?.lowercased() ?? "") else {
            showNotice("アドレスは https:// で始めてください (設定ファイル)", .warn)
            return
        }
        NSWorkspace.shared.open(u)
    }

    /// A popup menu under a field, from its bottom-left, with the current value checked. An item titled "-" is a
    /// separator.
    private func popup(at r: NSRect, _ items: [(String, String)], current: String,
                       _ chose: @escaping (String) -> Void) {
        let menu = NSMenu()
        let box = Unchecked(chose)
        for (title, value) in items {
            if title == "-" {
                menu.addItem(.separator())
                continue
            }
            let it = ClosureMenuItem(title: title) { box.value(value) }
            it.state = value.lowercased() == current.lowercased() ? .on : .off
            menu.addItem(it)
        }
        view.model.pressed = nil
        menu.popUp(positioning: nil, at: NSPoint(x: r.minX, y: r.maxY + 2), in: view)
        refresh()
    }
}

/// A menu item that runs a closure.
final class ClosureMenuItem: NSMenuItem {
    private let run: () -> Void

    init(title: String, _ run: @escaping () -> Void) {
        self.run = run
        super.init(title: title, action: #selector(fire), keyEquivalent: "")
        target = self
    }

    required init(coder: NSCoder) { fatalError("not used") }

    @objc private func fire() { run() }
}
