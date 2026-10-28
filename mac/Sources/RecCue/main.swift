import AppKit
import AVFoundation

// Rec Cue! for macOS — step 1: the recording core, driven from the command line (no window yet).
//
//   rec-cue --list-devices
//   rec-cue --request-access [--mic auto]
//   rec-cue --record [--seconds 10] [--test-pattern] [--system auto|none] [--mic none|auto|<name>] [--monitor 0]
//           [--fps 15] [--video-kbps 2000] [--audio-kbps 128] [--no-cursor] [--out <file.mp4>] [--crash-at <s>]
//           [--term-at <s>]   test: SIGTERM to ourselves, which ends the recording the normal way (as Ctrl-C does)
//           [--pause-at <s> [--pause-for <s>]]   test: pause (❚❚) that many seconds in, for that long (default 2)
//           [--end-snap-ms <ms>]   test: the file ends that many ms after a tick (the last frame is that short)
//           [--screen-lost-at <s> [--screen-lost-for <s>]]   test: the screen counts as not coming through then
//           [--audio-only [--audio-format m4a|wav]]   sound only: the Mac's sound and the microphone, no picture
//           [--sync-test]   with --test-pattern: flash and beep once a second, then measure picture against sound
//           [--no-beep]     sync test with flashes only (silent): the picture in the file against the capture
//           [--still]       the test window does not move (ScreenCaptureKit then sends frames only when it flashes)
//   rec-cue --screenshot [--monitor 0] [--out <file.png>] [--no-clipboard] [--test-pattern]
//   rec-cue --shot-selftest   the test window in magenta, the screen captured without it: is it left out? (saves nothing)
//           [--include-self]   the control: Rec Cue! left in, so the window must be found
//   rec-cue --verify-file <file>
//   rec-cue --lines-check   しおり's lines: per scene, the Mac's changes, load / pick time, any that would not fit
//   rec-cue --hotkey-check  the shortcuts: how settings are read, and whether config.ini's three can be registered
//   ... [--json-out <file>]   the result as one JSON object (also on stdout)
//
// Start it through scripts/run.sh (LaunchServices), so macOS asks about screen recording for Rec Cue! itself.

enum App {
    static var version: String { (Bundle.main.infoDictionary?["CFBundleShortVersionString"] as? String) ?? "dev" }
    /// "27.0.1" (operatingSystemVersionString comes in the user's language)
    static var os: String {
        let v = ProcessInfo.processInfo.operatingSystemVersion
        return "\(v.majorVersion).\(v.minorVersion).\(v.patchVersion)"
    }
}

struct Args {
    var mode = ""
    var verifyPath: String?
    var jsonOut: String?
    var rec = RecordOptions()
    var noClipboard = false
    var snapshotPath: String?
    var uiState = "idle"
    var at: DateComponents?   // --ui-snapshot --at 2026-10-22T14:00: しおり's line for that moment
    var includeSelf = false
    var errors: [String] = []
}

func parseArgs(_ a: [String], base: RecordOptions) -> Args {
    var r = Args()
    r.rec = base
    var i = 0
    func val() -> String? {
        i += 1
        return i < a.count ? a[i] : nil
    }
    func int(_ d: Int, _ lo: Int, _ hi: Int) -> Int { min(hi, max(lo, Int(val() ?? "") ?? d)) }
    while i < a.count {
        let s = a[i]
        switch s {
        case "--list-devices": r.mode = "list-devices"
        case "--request-access": r.mode = "request-access"
        case "--screenshot": r.mode = "screenshot"
        case "--ui-autotest": r.mode = "ui-autotest"
        case "--ui-quit-test": r.mode = "ui-quit-test"
        case "--ui-snapshot":
            r.mode = "ui-snapshot"
            r.snapshotPath = val()
        case "--ui-state": r.uiState = val() ?? "idle"
        case "--at":
            let v = val() ?? ""
            r.at = UiSnapshot.moment(v)
            if r.at == nil { r.errors.append("--at wants 2026-10-22T14:00, not \(v)") }
        case "--shot-selftest": r.mode = "shot-selftest"
        case "--lines-check": r.mode = "lines-check"
        case "--hotkey-check": r.mode = "hotkey-check"
        case "--no-clipboard": r.noClipboard = true
        case "--include-self": r.includeSelf = true
        case "--version": r.mode = "version"
        case "--record": r.mode = "record"
        case "--verify-file":
            r.mode = "verify-file"
            r.verifyPath = val()
        case "--seconds": r.rec.seconds = max(1, Double(val() ?? "") ?? r.rec.seconds)
        case "--fps": r.rec.fps = int(r.rec.fps, 1, 60)
        case "--video-kbps": r.rec.videoKbps = int(r.rec.videoKbps, 200, 50000)
        case "--audio-kbps": r.rec.audioKbps = int(r.rec.audioKbps, 64, 320)
        case "--monitor": r.rec.monitor = int(0, 0, 15)
        case "--system": r.rec.system = val() ?? "auto"
        case "--mic": r.rec.mic = val() ?? "none"
        case "--test-pattern": r.rec.testPattern = true
        case "--sync-test": r.rec.syncTest = true
        case "--no-beep": r.rec.noBeep = true
        case "--still": r.rec.stillPattern = true
        case "--no-cursor": r.rec.cursor = false
        case "--out": if let p = val() { r.rec.out = URL(fileURLWithPath: p) }
        case "--crash-at": r.rec.crashAt = Double(val() ?? "")
        case "--term-at": r.rec.termAt = Double(val() ?? "")
        case "--pause-at": r.rec.pauseAt = Double(val() ?? "")
        case "--audio-only": r.rec.audioOnly = true
        case "--audio-format": r.rec.audioFormat = (val() ?? "m4a").lowercased()
        case "--pause-for": r.rec.pauseFor = max(0.1, Double(val() ?? "") ?? 2)
        case "--end-snap-ms": r.rec.endSnapMs = Double(val() ?? "")
        case "--screen-lost-at": r.rec.screenLostAt = Double(val() ?? "")
        case "--screen-lost-for": r.rec.screenLostFor = max(0.1, Double(val() ?? "") ?? 2)
        case "--json-out": r.jsonOut = val()
        default:
            if !s.hasPrefix("-psn_") { r.errors.append("unknown argument \(s)") }
        }
        i += 1
    }
    return r
}

@MainActor
final class AppDelegate: NSObject, NSApplicationDelegate {
    let args: Args
    private var controller: Controller?
    private var status: StatusItem?
    private var mainWindow: MainWindow?
    private var quitAsked: Stopwatch?   // a quit that waits for the file to be finished
    private var uiTest: Bool { args.mode == "ui-autotest" || args.mode == "ui-quit-test" }

    init(_ a: Args) {
        args = a
    }

    func applicationDidFinishLaunching(_ notification: Notification) {
        if args.mode.isEmpty && args.errors.isEmpty {   // opened as an app (Finder, open): the menu bar icon and the window
            startUI()
            return
        }
        if args.mode == "ui-autotest" {
            startUI()
            Task { @MainActor in self.finish(await self.autotest()) }
            return
        }
        if args.mode == "ui-quit-test" {
            startUI()
            Task { @MainActor in await self.quitTest() }
            return
        }
        Task { @MainActor in
            let j = await self.perform()
            self.finish(j)
        }
    }

    private func startUI() {
        let c = Controller()
        controller = c
        status = StatusItem(controller: c, openWindow: { [weak self] in self?.showWindow() })
        mainWindow = MainWindow(controller: c, hotkeys: !uiTest)   // a test takes no keys
        Log.event("ui.open", ok: true, msg: "version \(App.version) macOS \(App.os)")
        if uiTest {
            mainWindow?.window.orderFrontRegardless()   // a test does not take the keyboard from whoever is typing
        } else {
            showWindow()
        }
    }

    private func showWindow() {
        mainWindow?.show()
    }

    /// --ui-autotest: the level check (stopped by its button, then by its time, then by a recording), then what the
    /// menu bar's items do — record, screenshot, pause, resume, stop — on the test window with no microphone; the
    /// check hears only Rec Cue!'s own sound and the screenshot has only its own windows (nothing real is recorded,
    /// heard or taken).
    private func autotest() async -> J {
        guard let c = controller else { return .obj([("ok", .bool(false))]) }
        let out = args.rec.out
        c.quiet = true
        c.testOptions = { o in
            o.testPattern = true
            o.mic = "none"
            o.out = out
        }
        let shotDir = out?.deletingLastPathComponent() ?? Paths.appSupport.appendingPathComponent("test")
        c.testShot = { s in
            s.testPattern = true
            s.clipboard = false
            s.dir = shotDir
        }
        c.testCheck = { o in
            o.ownOnly = true
            o.mic = "none"
        }
        func wait(_ s: Double) async { try? await Task.sleep(nanoseconds: UInt64(s * 1e9)) }
        var states: [J] = []
        func note(_ step: String) {
            states.append(.obj([("step", .str(step)), ("state", .str("\(c.state)")), ("paused", .bool(c.paused)),
                                ("elapsed", .n(c.elapsed, 1)), ("shots", .i(c.shots)), ("checking", .bool(c.check != nil)),
                                ("recorders", .i(Recorder.live)), ("footprint_mb", .n(Proc.memory().footprint, 0))]))
        }
        // the keyboard: Tab goes round the parts that can be used now, Shift+Tab back; Space presses the focused one
        var keys: [J] = []
        if let w = mainWindow {
            var order: [String] = []
            for _ in 0..<12 {
                w.press(48)
                order.append("\(w.shown.focus.map { "\($0)" } ?? "-")")
            }
            var back: [String] = []
            for _ in 0..<3 {
                w.press(48, shift: true)
                back.append("\(w.shown.focus.map { "\($0)" } ?? "-")")
            }
            while w.shown.focus != .check { w.press(48) }
            w.press(49)   // 音量を確認
            let checking = c.check != nil
            w.press(49)   // 確認を止める
            keys = [.obj([("tab", .arr(order.map { .str($0) })), ("shift_tab", .arr(back.map { .str($0) })),
                          ("ring", .bool(w.shown.showFocus)), ("space_started_check", .bool(checking)),
                          ("space_stopped_check", .bool(c.check == nil)), ("first_click", .bool(w.takesFirstClick))])]
            // the tooltips while idle
            keys.append(.obj(([Part.record, .audio, .pause, .pin, .shot, .gear, .format, .guide]).map { p in
                ("\(p)", .str(w.tip(p))) }))
        }
        // the level check: its button ends it; then its time (shortened to 2 s); then a recording takes over
        c.startCheck()
        await wait(1.5)
        note("checking")
        c.endCheck(said: true)
        note("check stopped")
        c.checkLimit = 2
        let since = Stopwatch()
        c.startCheck()
        while c.check != nil && since.ms < 6000 { await wait(0.1) }
        states.append(.obj([("step", .str("check timed out")), ("checking", .bool(c.check != nil)),
                            ("after_s", .n(since.ms / 1000))]))
        c.checkLimit = 60
        c.startCheck()
        await wait(1)
        note("checking again")
        c.start(audioOnly: false)
        note("start")
        // wait until it really records (a locked screen or a slow start must not turn the test into 0 s)
        var waited = 0.0
        while c.state == .starting && waited < 15 {
            await wait(0.1)
            waited += 0.1
        }
        await wait(2)
        note("recording")
        if let w = mainWindow {   // the tooltips while recording
            keys.append(.obj(([Part.record, .audio, .pause]).map { p in ("\(p) (recording)", .str(w.tip(p))) }))
        }
        c.screenshot()
        var shotWait = 0.0
        while c.shots == 0 && shotWait < 5 {
            await wait(0.1)
            shotWait += 0.1
        }
        note("shot")
        c.togglePause()
        await wait(2)
        note("paused")
        c.togglePause()
        await wait(2)
        note("resumed")
        c.stop()
        note("stop")
        while c.state != .idle { await wait(0.1) }
        note("idle")
        let first = c.lastResult
        // the warnings: a microphone that is not there (マイクなし), the screen "not coming through" from 3 s for 2 s,
        // and the Mac's sound silent for over 4 s (60 in use): each said once it has lasted 2 s, the screen's first
        var shown: [J] = []
        if let w = mainWindow {
            c.silenceWarnMs = 4000
            let out2 = out.map { $0.deletingLastPathComponent()
                .appendingPathComponent($0.deletingPathExtension().lastPathComponent + "-warn.mp4") }
            c.testOptions = { o in
                o.testPattern = true
                o.mic = "no-such-microphone"
                o.out = out2
                o.screenLostAt = 3
                o.screenLostFor = 2
            }
            c.start(audioOnly: false)
            var waited = 0.0
            while c.state == .starting && waited < 15 {
                await wait(0.1)
                waited += 0.1
            }
            for t in [1.0, 2.6, 4.0, 6.2, 7.5] {
                while c.state == .recording && c.elapsed < t { await wait(0.05) }
                await wait(0.15)   // a tick or so later
                let m = w.shown
                shown.append(.obj([("t", .n(c.elapsed, 1)), ("status", .str(m.status)), ("tone", .str("\(m.tone)")),
                                   ("alert", .bool(m.alert)), ("sys_warn", .bool(m.sys.warn)),
                                   ("comment_tone", .str("\(m.commentTone)")), ("comment", .str(m.comment))]))
            }
            c.stop()
            while c.state != .idle { await wait(0.1) }
            c.silenceWarnMs = 60000
        }
        let second = c.lastResult
        // the shortcuts, through the same hot key events (keys nobody uses; no real key is pressed): 録画 starts a
        // recording, 録音 meanwhile is refused with a notice, スクショ takes a shot (own windows), 録画 again stops
        var hot: [J] = []
        if let w = mainWindow {
            c.testOptions = { o in
                o.testPattern = true
                o.mic = "none"
                o.out = out.map { $0.deletingLastPathComponent()
                    .appendingPathComponent($0.deletingPathExtension().lastPathComponent + "-keys.mp4") }
                o.startGapMs = 300   // the window reads the time while the recording's clock is being set
            }
            var cfg = Config()
            cfg.hotkey = "Ctrl+Option+Shift+Cmd+F17"
            cfg.audioHotkey = "Ctrl+Option+Shift+Cmd+F18"
            cfg.shotHotkey = "Ctrl+Option+Shift+Cmd+F19"
            w.useHotkeys(cfg)
            func noteKey(_ step: String) {
                hot.append(.obj([("step", .str(step)), ("state", .str("\(c.state)")), ("audio_only", .bool(c.audioOnly)),
                                 ("shots", .i(c.shots)), ("status", .str(w.shown.status)),
                                 ("side_label_key", .str(w.shown.recHotkey))]))
            }
            Hotkeys.simulate(.record)
            var waited = 0.0
            while c.state != .recording && waited < 15 {
                await wait(0.1)
                waited += 0.1
            }
            await wait(1.5)
            noteKey("record key")
            Hotkeys.simulate(.audio)
            await wait(0.3)
            noteKey("audio key while recording")
            let before = c.shots
            Hotkeys.simulate(.shot)
            var shotWait = 0.0
            while c.shots == before && shotWait < 5 {
                await wait(0.1)
                shotWait += 0.1
            }
            noteKey("shot key")
            Hotkeys.simulate(.record)
            while c.state != .idle { await wait(0.1) }
            noteKey("record key again")
        }
        let third = c.lastResult
        // a recording that ends by itself (here SIGTERM; a stopped capture, a full disk or a failed writer go the same
        // way): the window shows 保存中 while the file is finished, and ❚❚ no longer reaches that recording
        c.testOptions = { o in
            o.testPattern = true
            o.mic = "none"
            o.out = out.map { $0.deletingLastPathComponent()
                .appendingPathComponent($0.deletingPathExtension().lastPathComponent + "-self.mp4") }
            o.termAt = 2
        }
        c.start(audioOnly: false)
        var selfWait = 0.0
        while c.state == .starting && selfWait < 15 {
            await wait(0.1)
            selfWait += 0.1
        }
        while c.state == .recording && c.elapsed < 2.1 { await wait(0.05) }
        var selfEnd: [(String, J)] = [("state_after_end", .str("\(c.state)")), ("elapsed", .n(c.elapsed, 2))]
        c.togglePause()
        while c.state != .idle { await wait(0.1) }
        let fourth = c.lastResult
        selfEnd += [("ok", .bool(fourth?["ok"]?.isTrue == true)), ("ended_by", fourth?["ended_by"] ?? .null),
                    ("pauses", fourth?["summary"]?["pauses"] ?? .null)]
        // dialogs: one asked for inside a task (a recording's end, a failed level check) must not hold up the main
        // actor while it is open; a screenshot that fails while the window is hidden must not put a dialog over
        // whatever is in front (it closes itself after 1.5 s and does not take the keyboard)
        var dialogs: [(String, J)] = []
        if let w = mainWindow {
            final class Ran: @unchecked Sendable { var ms = -1.0 }
            let ran = Ran()
            c.quiet = false
            c.testAlertSeconds = 1.5
            let sw = Stopwatch()
            Task { @MainActor in c.alert("Rec Cue! の試験", "1.5 秒で閉じます。") }
            Task { @MainActor in ran.ms = sw.ms }
            await wait(2.5)
            c.quiet = true
            c.testAlertSeconds = nil
            dialogs.append(("other_job_ms", .n(ran.ms, 0)))
            w.window.orderOut(nil)
            c.testShot = { s in
                s.testPattern = true
                s.clipboard = false
                s.dir = URL(fileURLWithPath: "/System/rec-cue-cannot-write-here")
            }
            let asked = c.alerts
            c.screenshot()
            await wait(3)
            dialogs += [("hidden_shot_dialogs", .i(c.alerts - asked)), ("window_shown", .bool(w.window.isVisible))]
        }
        // stopped while it was still starting (the menu bar's 停止して保存, or quitting then): nothing was recorded, so
        // no file is left and no dialog comes — a recording, and sound only (WAV)
        var early: [J] = []
        for audio in [false, true] {
            let file = out.map { $0.deletingLastPathComponent().appendingPathComponent(
                $0.deletingPathExtension().lastPathComponent + (audio ? "-early.wav" : "-early.mp4")) }
            c.testOptions = { o in
                o.testPattern = true
                o.mic = "none"
                o.out = file
                o.audioFormat = "wav"
            }
            let asked = c.alerts
            c.start(audioOnly: audio)
            c.stop()
            while c.state != .idle { await wait(0.1) }
            let r = c.lastResult
            early.append(.obj([("audio_only", .bool(audio)), ("ok", .bool(r?["ok"]?.isTrue == true)),
                               ("cancelled", r?["cancelled"] ?? .null), ("complete", r?["complete"] ?? .null),
                               ("file_left", .bool(file.map { FileManager.default.fileExists(atPath: $0.path) } ?? false)),
                               ("dialogs", .i(c.alerts - asked))]))
        }
        return .obj([("ok", .bool(first?["ok"]?.isTrue == true && second?["ok"]?.isTrue == true
                                  && third?["ok"]?.isTrue == true && fourth?["ok"]?.isTrue == true)),
                     ("steps", .arr(states)), ("hotkeys", .arr(hot)),
                     ("result", first ?? .null), ("warnings", .arr(shown)), ("warn_result", second ?? .null),
                     ("keys", .arr(keys)), ("self_end", .obj(selfEnd)), ("dialogs", .obj(dialogs)),
                     ("early_stop", .arr(early)),
                     // the four recordings are over: none of them should still be held
                     ("recorders_at_end", .i(Recorder.live)), ("footprint_mb_at_end", .n(Proc.memory().footprint, 0))])
    }

    /// --ui-quit-test: quitting while it records (log-off, restart, the menu's 終了) finishes the file, then the app
    /// goes. The test window, no microphone; the result is written as the app ends (applicationWillTerminate).
    private func quitTest() async {
        guard let c = controller else { return }
        c.quiet = true
        let out = args.rec.out
        c.testOptions = { o in
            o.testPattern = true
            o.mic = "none"
            o.out = out
        }
        c.start(audioOnly: false)
        var waited = 0.0
        while c.state == .starting && waited < 15 {
            try? await Task.sleep(nanoseconds: 100_000_000)
            waited += 0.1
        }
        try? await Task.sleep(nanoseconds: 2_000_000_000)
        Log.event("test.quit", ok: c.state == .recording, msg: "NSApp.terminate while \(c.state)")
        // from the run loop, as the menu's 終了 and a log-off's quit event come: called inside this task, AppKit's
        // wait for the reply ran nested in a main-queue job, and nothing else on the main actor could run
        NSApp.perform(#selector(NSApplication.terminate(_:)), with: nil, afterDelay: 0)
    }

    /// Opened again (Finder, the Dock, `open`): the one that runs shows its window (a second one does not start).
    func applicationShouldHandleReopen(_ sender: NSApplication, hasVisibleWindows flag: Bool) -> Bool {
        if mainWindow != nil { showWindow() }
        return true
    }

    /// Quitting while recording finishes the file first (as the Windows version does at log-off).
    func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
        guard let c = controller, c.state != .idle else { return .terminateNow }
        Log.event("ui.quit_requested", ok: true, msg: "while \(c.state): the file is finished first")
        let asked = Stopwatch()
        quitAsked = asked
        c.stop()
        // AppKit waits for the reply in the modal panel run loop mode: a timer in the default mode never fired there
        // and the app never quit (2026-10-28). In the common modes it does; after 30 s the app goes anyway.
        let t = Timer(timeInterval: 0.1, repeats: true) { t in
            MainActor.assumeIsolated {
                let late = asked.ms > 30000
                guard c.state == .idle || late else { return }
                t.invalidate()
                Log.event("ui.quit_reply", ok: !late, ms: asked.ms, msg: late ? "the file was not finished in 30 s" : "")
                NSApp.reply(toApplicationShouldTerminate: true)
            }
        }
        RunLoop.main.add(t, forMode: .common)
        return .terminateLater
    }

    func applicationWillTerminate(_ notification: Notification) {
        if let q = quitAsked { Log.event("ui.quit", ok: true, ms: q.ms) }
        guard args.mode == "ui-quit-test", let p = args.jsonOut else { return }
        let r = controller?.lastResult
        let j = J.obj([("ok", .bool(quitAsked != nil && r?["ok"]?.isTrue == true)), ("quit_ms", .n(quitAsked?.ms ?? -1, 0)),
                       ("result", r ?? .null)])
        try? (j.text + "\n").write(toFile: p, atomically: true, encoding: .utf8)
    }

    private func perform() async -> J {
        if !args.errors.isEmpty {
            return .obj([("ok", .bool(false)), ("error", .str(args.errors.joined(separator: "; ")))])
        }
        switch args.mode {
        case "version":
            return .obj([("ok", .bool(true)), ("version", .str(App.version))])
        case "lines-check":
            return Lines.check()
        case "hotkey-check":
            return Hotkeys.check()
        case "list-devices":
            return await MainActor.run { Devices.listJson() }   // NSScreen belongs to the main thread
        case "request-access":
            // the screen, and with --mic (not none) the microphone too, so a test does not stop at the question
            let granted = CGRequestScreenCaptureAccess()
            var ok = granted
            var kv: [(String, J)] = [("screen_access", .bool(granted))]
            if args.rec.mic.lowercased() != "none" {
                ok = await AVCaptureDevice.requestAccess(for: .audio) && ok
                kv.append(("mic_access", .str(Devices.micAccess())))
            }
            return .obj([("ok", .bool(ok))] + kv
                        + [("hint", granted ? .null : .str("System Settings > Privacy & Security > Screen & System "
                                                           + "Audio Recording: turn on Rec Cue!, then run again"))])
        case "ui-snapshot":
            guard let p = args.snapshotPath else { return .obj([("ok", .bool(false)), ("error", .str("no file"))]) }
            var m = UiSnapshot.model(args.uiState)
            UiSnapshot.say(&m, args.uiState, at: args.at)
            guard let png = WindowView.snapshot(m, scale: 2, idle: UiSnapshot.image("guide-idle"),
                                                rec: UiSnapshot.image("guide-rec")) else {
                return .obj([("ok", .bool(false)), ("error", .str("cannot draw"))])
            }
            do {
                try png.write(to: URL(fileURLWithPath: p))
            } catch {
                return .obj([("ok", .bool(false)), ("error", .str("\(error.localizedDescription)"))])
            }
            return .obj([("ok", .bool(true)), ("path", .str(p)), ("state", .str(args.uiState)), ("bytes", .i(png.count))])
        case "screenshot", "shot-selftest":
            var s = Screenshot.Options()
            s.monitor = args.rec.monitor
            s.dir = settings.resolvedShotDir
            s.prefix = args.rec.namePrefix
            s.clipboard = settings.shotClipboard && !args.noClipboard
            s.out = args.rec.out
            s.testPattern = args.rec.testPattern
            s.selfTest = args.mode == "shot-selftest"
            s.includeSelf = args.includeSelf
            return await Screenshot.take(s)
        case "verify-file":
            guard let p = args.verifyPath else { return .obj([("ok", .bool(false)), ("error", .str("no file"))]) }
            let url = URL(fileURLWithPath: p)
            let ext = url.pathExtension.lowercased()
            return await Verify.file(url, audioOnly: ext == "m4a" || ext == "wav")
        case "record":
            guard ["m4a", "wav"].contains(args.rec.audioFormat) else {
                return .obj([("ok", .bool(false)), ("error", .str("--audio-format is m4a or wav (macOS cannot write MP3)"))])
            }
            let r = Recorder(args.rec)
            Running.set(r)
            defer { Running.set(nil) }
            return await r.run()
        default:
            return .obj([("ok", .bool(false)), ("error", .str("say what to do: --list-devices / --request-access / "
                                                             + "--record / --verify-file"))])
        }
    }

    private func finish(_ j: J) {
        let text = j.text
        print(text)
        fflush(stdout)
        if let p = args.jsonOut { try? (text + "\n").write(toFile: p, atomically: true, encoding: .utf8) }
        exit(j["ok"]?.isTrue == true ? 0 : 1)
    }
}

/// The recording in progress, for the signal handlers.
enum Running {
    private static let lock = NSLock()
    private static var recorder: Recorder?

    static func set(_ r: Recorder?) { lock.withLock { recorder = r } }
    static func get() -> Recorder? { lock.withLock { recorder } }
}

Log.open()
// Ctrl-C and kill end a recording the normal way, so the file is finished; with nothing recording they just quit.
signal(SIGINT, SIG_IGN)
signal(SIGTERM, SIG_IGN)
let signalSources: [DispatchSourceSignal] = [SIGINT, SIGTERM].map { sig in
    let s = DispatchSource.makeSignalSource(signal: sig, queue: .main)
    s.setEventHandler {
        if let r = Running.get() {
            r.requestStop(by: "signal", sig == SIGINT ? "SIGINT" : "SIGTERM")
        } else {
            exit(128 + sig)
        }
    }
    s.resume()
    return s
}
let (settings, settingsProblems) = Config.load()
let parsed = parseArgs(Array(CommandLine.arguments.dropFirst()),
                      base: RecordOptions(config: settings, problems: settingsProblems))
MainActor.assumeIsolated {   // top-level code runs on the main thread
    let app = NSApplication.shared
    app.setActivationPolicy(.accessory)
    let delegate = AppDelegate(parsed)
    app.delegate = delegate
    app.run()   // until the app quits (delegate stays alive until then)
}
