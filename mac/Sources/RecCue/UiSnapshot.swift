import AppKit

/// Sample models for --ui-snapshot (the Windows version's README pictures): fixed screens and devices, so nothing of
/// this Mac shows in the picture.
enum UiSnapshot {
    static func image(_ name: String) -> NSImage? {
        Bundle.main.url(forResource: name, withExtension: "png").flatMap { NSImage(contentsOf: $0) }
    }

    /// idle / check / starting / rec / shot / paused / audio / saving / saved / warn
    static func model(_ state: String) -> UiModel {
        var m = UiModel()
        m.monitors = [CGRect(x: 0, y: 0, width: 1920, height: 1080), CGRect(x: 1920, y: -180, width: 2560, height: 1440)]
        m.screenText = "1: メイン 1920×1080"
        m.path = "~/Movies/画面録画"
        m.pulse = 1
        m.recHotkey = "⌃⌥R"   // as if the three shortcuts were registered (the defaults)
        m.audioHotkey = "⌃⌥A"
        m.shotHotkey = "⌃⌥S"
        let file = "~/Movies/画面録画/Rec Cue 2026-10-27 140000"
        func open(_ audio: Bool) {
            m.mic.mode = "自動"
            m.mic.device = "MacBook Air のマイク"
            m.mic.active = true
            m.mic.db = -18
            m.mic.hold = -12
            m.sys.mode = "自動"
            m.sys.device = "すべてのアプリの音"
            m.sys.active = true
            m.sys.db = -24
            m.sys.hold = -20
            m.audioOnly = audio
        }
        switch state {
        case "check":
            open(false)
            m.audioOnly = false
            m.phase = .checking
            m.mic.db = -16
            m.mic.hold = -10
            m.sys.db = -27
            m.sys.hold = -20
        case "starting":
            m.phase = .starting
            m.status = "準備中…"
        case "rec", "warn", "paused", "shot":
            open(false)
            m.phase = .recording
            m.split = 1
            m.timer = "00:03:21"
            m.status = "録画中 — ■ を押すと停止して保存します"
            m.pathLabel = "録画中"
            m.path = file + ".mp4"
            m.guideRec = true
            if state == "shot" {
                m.shots = 3
                m.shotFlash = true
                m.status = "スクショ 3 枚目を保存し、コピーしました"
                m.tone = .good
            }
            if state == "warn" {
                m.status = "PC の音声が 1 分以上無音です。ミュートを確認"
                m.tone = .warn
                m.alert = true
                m.sys.warn = true
                m.sys.db = -120
            }
            if state == "paused" {
                m.paused = true
                m.status = "一時停止中 — ▶ で再開、■ で停止して保存します"
                m.guideRec = false
            }
        case "audio":
            open(true)
            m.phase = .recording
            m.split = -1
            m.timer = "00:12:05"
            m.status = "録音中 — ■ を押すと停止して保存します"
            m.pathLabel = "録音中"
            m.path = file + ".m4a"
            m.guideRec = true
        case "saving":
            open(false)
            m.phase = .saving
            m.split = 1
            m.timer = "00:03:21"
            m.status = "ファイルを仕上げています…"
            m.pathLabel = "録画中"
            m.path = file + ".mp4"
        case "saved":
            m.path = file + ".mp4"
        default:
            break
        }
        return m
    }

    /// しおり's line for the state, as the window would choose it (the same one every time: a fixed seed, and the
    /// moment of --at if given).
    static func say(_ m: inout UiModel, _ state: String, at: DateComponents?) {
        let lines = Lines()
        for p in lines.load() { Log.event("lines", ok: false, msg: p) }
        lines.seed(20261023)
        if let at { lines.setClock(at) }
        var c = SayContext()
        c.recKey = "⌃⌥R"
        c.audioKey = "⌃⌥A"
        c.shotKey = "⌃⌥S"
        let scene: Scene
        switch state {
        case "check": scene = .checkMic
        case "starting": scene = .starting
        case "rec": scene = .rec
        case "shot":
            scene = .shot
            c.n = 3
            c.clip = true
        case "paused": scene = .pause
        case "audio":
            scene = .audio
            c.audio = true
        case "saving": scene = .saving
        case "saved":
            scene = .saved
            c.sec = 201
            c.mb = 48.6
        case "warn": scene = .warnSys
        default: scene = .start
        }
        m.comment = lines.pick(scene, c)
        m.commentTone = scene.tone
    }

    /// "2026-10-22T14:00" → that moment (with its day of the week), for --at.
    static func moment(_ s: String) -> DateComponents? {
        let f = DateFormatter()
        f.locale = Locale(identifier: "en_US_POSIX")
        f.calendar = Calendar(identifier: .gregorian)
        f.dateFormat = "yyyy-MM-dd'T'HH:mm"
        guard let d = f.date(from: s) else { return nil }
        return f.calendar.dateComponents([.hour, .weekday, .month, .day], from: d)
    }
}
