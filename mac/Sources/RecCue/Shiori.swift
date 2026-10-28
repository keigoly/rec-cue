import AppKit

/// What 湊 しおり says in the bubble — the Windows version's shiori.cpp, reading the same file: `res/shiori-lines.txt`
/// (about 1,100 lines in 30 scenes), then `mac/shiori-mac.txt`, which takes out the lines that are not true on the Mac
/// (the taskbar, MP3, the dot in the screen's corner …) and adds the Mac's own. A line may be limited to a time of day,
/// a day of the week, a month or season, the kind of recording or one moment of it (minute 30, the 5th screenshot);
/// one said lately comes back only after half of the other lines of its scene have been said.
enum Scene: Int, CaseIterable {
    case start, back, talk, tip                              // the window opens / is used again after a while / clicked
    case check, checkMic, checkSys, checkQuiet, checkEnd     // the level check
    case starting, rec, recOn, audio, audioOn                // a recording starts; …On: every 10 min and milestones
    case shot, shotIdle                                      // a screenshot in a recording / outside one
    case warnScreen, warnSys, warnMic, warnNoSys, recover    // the recording needs attention / is fine again
    case saving, saved, failStart, failSave, failShot
    case pinOn, pinOff
    case pause, resume                                       // the recording is paused / goes on

    static let names = ["start", "back", "talk", "tip", "check", "check_mic", "check_sys", "check_quiet", "check_end",
                        "starting", "rec", "rec_on", "audio", "audio_on", "shot", "shot_idle", "warn_screen", "warn_sys",
                        "warn_mic", "warn_nosys", "recover", "saving", "saved", "fail_start", "fail_save", "fail_shot",
                        "pin_on", "pin_off", "pause", "resume"]

    /// Its [header] in the files.
    var name: String { Self.names[rawValue] }

    static func named(_ s: String) -> Scene? { names.firstIndex(of: s).flatMap { Scene(rawValue: $0) } }

    /// The bubble's colours while she says it.
    var tone: Tone {
        switch self {
        case .warnScreen, .warnSys, .warnMic, .warnNoSys, .checkQuiet: return .warn
        case .failStart, .failSave, .failShot: return .bad
        case .checkMic, .checkSys, .shot, .shotIdle, .recover, .saved: return .good
        default: return .normal
        }
    }
}

/// What a line may refer to or depend on.
struct SayContext {
    var audio = false        // the recording is sound only
    var n = 0                // the screenshot's number (shot), or minutes into the recording (rec_on / audio_on)
    var shots = 0            // screenshots in the recording that was saved
    var sec = 0.0            // length of that recording
    var mb = 0.0             // its size
    var autoStop = false     // it stopped by itself
    var clip = false         // the screenshot is on the clipboard too
    var recKey = "", audioKey = "", shotKey = ""   // shortcut texts; a line naming one that is not set is left out
}

final class Lines {
    private struct Line {
        var scene: Scene
        var bands: UInt8 = 0     // times of day (a bit per band), 0 = any
        var days: UInt8 = 0      // days of the week (bit 0 = Sunday), 0 = any
        var months: UInt16 = 0   // bits 1…12, 0 = any
        var flags: UInt16 = 0    // kind of recording, short / long / with screenshots, special days, shortcuts it names
        var n = -1               // only at this minute / screenshot (-1 = any)
        var source: String       // where it is written: "123" (shiori-lines.txt) or "mac:12" (shiori-mac.txt)
        var text = ""
        var said: UInt32 = 0     // the scene's count when it was last said (0 = not yet)
    }

    private enum F {
        static let video: UInt16 = 1 << 0, audio: UInt16 = 1 << 1                       // the kind of recording
        static let short: UInt16 = 1 << 2, long: UInt16 = 1 << 3, shots: UInt16 = 1 << 4   // saved: < 1 min / ≥ 1 h / shots
        static let auto: UInt16 = 1 << 5                                                  // saved: it stopped by itself
        static let newYear: UInt16 = 1 << 6, yearEnd: UInt16 = 1 << 7                     // Jan 1-3 / Dec 28-31
        static let needRec: UInt16 = 1 << 8, needAudio: UInt16 = 1 << 9, needShot: UInt16 = 1 << 10   // {rec_key} …
        static let clip: UInt16 = 1 << 11                                                 // the shot is on the clipboard
        static let prio = auto | newYear | yearEnd   // with @n=, these go before the ordinary lines
        static let more = short | long | shots       // … and these are said twice as often
    }

    private enum Kind { case band, day, month, flag }
    private static let tags: [String: (Kind, UInt16)] = {
        var t: [String: (Kind, UInt16)] = [:]
        // times of day: early 4-7, morning 7-11, noon 11-13, afternoon 13-17, evening 17-19, night 19-23, late 23-4
        for (i, s) in ["early", "morning", "noon", "afternoon", "evening", "night", "late"].enumerated() {
            t[s] = (.band, UInt16(1) << i)
        }
        for (i, s) in ["sun", "mon", "tue", "wed", "thu", "fri", "sat"].enumerated() { t[s] = (.day, UInt16(1) << i) }
        t["weekday"] = (.day, 0x3E)
        t["weekend"] = (.day, 0x41)
        for (i, s) in ["jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec"].enumerated() {
            t[s] = (.month, UInt16(1) << (i + 1))
        }
        t["spring"] = (.month, 0b111 << 3)
        t["summer"] = (.month, 0b111 << 6)
        t["autumn"] = (.month, 0b111 << 9)
        t["winter"] = (.month, 1 << 12 | 1 << 1 | 1 << 2)
        for (s, b) in [("video", F.video), ("audio", F.audio), ("short", F.short), ("long", F.long), ("shots", F.shots),
                       ("auto", F.auto), ("newyear", F.newYear), ("yearend", F.yearEnd), ("clip", F.clip)] {
            t[s] = (.flag, b)
        }
        return t
    }()

    /// {names} a line may use, and the shortcut each one needs.
    private static let fields: [String: UInt16] = ["n": 0, "t": 0, "mb": 0, "shots": 0, "rec_key": F.needRec,
                                                   "audio_key": F.needAudio, "shot_key": F.needShot]

    private static let calendar: Calendar = {
        var c = Calendar(identifier: .gregorian)   // weekday 1 = Sunday, months 1…12, whatever the Mac's calendar is
        c.timeZone = .current
        return c
    }()

    private var lines: [Line] = []
    private var byScene = [[Int]](repeating: [], count: Scene.allCases.count)
    private var said = [UInt32](repeating: 0, count: Scene.allCases.count)
    private var rng: UInt64
    private var clock: DateComponents?
    /// Where the last line picked is written ("123" or "mac:12"), for the log.
    private(set) var lastSource = ""
    /// What shiori-mac.txt changed: lines taken out of the shared file, lines added.
    private(set) var macDropped = 0, macAdded = 0

    init() {
        rng = mach_absolute_time() ^ (UInt64(getpid()) << 32) ^ 0x9E37_79B9_7F4A_7C15
        if rng == 0 { rng = 1 }
    }

    /// The same choices every time (--ui-snapshot).
    func seed(_ s: UInt64) { rng = s == 0 ? 1 : s }

    /// Pick for this moment instead of now (--ui-snapshot --at): hour, weekday (1 = Sunday), month and day.
    func setClock(_ c: DateComponents) { clock = c }

    private func next() -> UInt64 {   // xorshift64*
        rng ^= rng >> 12
        rng ^= rng << 25
        rng ^= rng >> 27
        return rng &* 0x2545_F491_4F6C_DD1D
    }

    /// 0 … n-1
    func roll(_ n: Int) -> Int { n > 0 ? Int(next() % UInt64(n)) : 0 }

    // MARK: reading

    /// The lines from the app's Resources. What was wrong comes back: a line with an unknown scene, tag or {name} is
    /// left out, and a line shiori-mac.txt takes out that the shared file no longer has is reported (so a changed
    /// shared line is noticed).
    @discardableResult
    func load() -> [String] {
        func text(_ name: String) -> String? {
            Bundle.main.url(forResource: name, withExtension: "txt").flatMap { try? String(contentsOf: $0, encoding: .utf8) }
        }
        guard let shared = text("shiori-lines") else { return ["shiori-lines.txt is missing"] }
        return load(shared: shared, mac: text("shiori-mac") ?? "")
    }

    func load(shared: String, mac: String) -> [String] {
        var problems: [String] = []
        lines.removeAll()
        byScene = Array(repeating: [], count: Scene.allCases.count)
        said = Array(repeating: 0, count: Scene.allCases.count)
        macDropped = 0
        macAdded = 0
        // the Mac's changes: "- line" takes that line out of its scene, "+ line" adds one
        var drops: [Scene: [String: Int]] = [:]   // the line as written → its line number in shiori-mac.txt
        var adds: [(Scene, String, Int)] = []
        var scene: Scene?
        for (no, t) in Self.rows(mac) {
            if let h = Self.header(t) {
                scene = Scene.named(h)
                if scene == nil { problems.append("shiori-mac.txt:\(no): unknown scene [\(h)] (its changes are skipped)") }
                continue
            }
            guard let s = scene else { continue }
            if t.hasPrefix("- ") {
                drops[s, default: [:]][Self.trim(t.dropFirst(2))] = no
            } else if t.hasPrefix("+ ") {
                adds.append((s, Self.trim(t.dropFirst(2)), no))
            } else {
                problems.append("shiori-mac.txt:\(no): a change starts with \"- \" or \"+ \"")
            }
        }
        scene = nil
        for (no, t) in Self.rows(shared) {
            if let h = Self.header(t) {
                scene = Scene.named(h)
                if scene == nil { problems.append("shiori-lines.txt:\(no): unknown scene [\(h)] (its lines are skipped)") }
                continue
            }
            guard let s = scene else { continue }
            if drops[s]?.removeValue(forKey: t) != nil {
                macDropped += 1
                continue
            }
            add(t, s, "\(no)", &problems, at: "shiori-lines.txt:\(no)")
        }
        for (s, t, no) in adds {
            if add(t, s, "mac:\(no)", &problems, at: "shiori-mac.txt:\(no)") { macAdded += 1 }
        }
        for (s, d) in drops {
            for (t, no) in d.sorted(by: { $0.value < $1.value }) {
                problems.append("shiori-mac.txt:\(no): [\(s.name)] has no such line to take out: \(t)")
            }
        }
        problems.sort()
        return problems
    }

    /// One line of the file: its @tags, then the text. A line that cannot be used is left out and reported.
    @discardableResult
    private func add(_ t: String, _ s: Scene, _ source: String, _ problems: inout [String], at: String) -> Bool {
        var l = Line(scene: s, source: source)
        var rest = Substring(t)
        while rest.hasPrefix("@") {
            let end = rest.firstIndex(where: { $0 == " " || $0 == "\t" }) ?? rest.endIndex
            let tag = String(rest[rest.index(after: rest.startIndex)..<end])
            if tag.hasPrefix("n="), tag.count > 2 {
                l.n = min(9999, Int(tag.dropFirst(2).prefix(while: { $0.isASCII && $0.isNumber })) ?? 0)
            } else if let k = Self.tags[tag] {
                switch k.0 {
                case .band: l.bands |= UInt8(truncatingIfNeeded: k.1)
                case .day: l.days |= UInt8(truncatingIfNeeded: k.1)
                case .month: l.months |= k.1
                case .flag: l.flags |= k.1
                }
            } else {
                problems.append("\(at): unknown tag @\(tag)")
                return false
            }
            rest = rest[end...].drop(while: { $0 == " " || $0 == "\t" })
        }
        guard !rest.isEmpty else {
            problems.append("\(at): tags without a line")
            return false
        }
        var scan = rest
        while let q = scan.firstIndex(of: "{") {
            let after = scan[scan.index(after: q)...]
            let z = after.firstIndex(of: "}")
            let name = z.map { String(after[..<$0]) } ?? ""
            guard let z, let needs = Self.fields[name] else {
                problems.append("\(at): unknown {\(name)}")
                return false
            }
            l.flags |= needs
            scan = after[after.index(after: z)...]
        }
        l.text = String(rest)
        byScene[s.rawValue].append(lines.count)
        lines.append(l)
        return true
    }

    /// The file's lines with their numbers, without the spaces and tabs around them (a full-width space stays, as in
    /// the Windows version); blank lines and # comments left out.
    private static func rows(_ s: String) -> [(Int, String)] {
        let body = s.hasPrefix("\u{FEFF}") ? Substring(s.dropFirst()) : Substring(s)
        var out: [(Int, String)] = []
        for (i, raw) in body.split(omittingEmptySubsequences: false, whereSeparator: { $0 == "\n" || $0 == "\r\n" })
            .enumerated() {
            let t = trim(raw)
            if !t.isEmpty && !t.hasPrefix("#") { out.append((i + 1, t)) }
        }
        return out
    }

    private static func trim(_ s: Substring) -> String {
        func blank(_ c: Character) -> Bool { c == " " || c == "\t" || c == "\r" }
        guard let a = s.firstIndex(where: { !blank($0) }), let b = s.lastIndex(where: { !blank($0) }) else { return "" }
        return String(s[a...b])
    }

    private static func header(_ t: String) -> String? {
        t.count >= 2 && t.hasPrefix("[") && t.hasSuffix("]") ? String(t.dropFirst().dropLast()) : nil
    }

    // MARK: choosing

    private static func band(_ hour: Int) -> Int {
        switch hour {
        case 4..<7: return 0
        case 7..<11: return 1
        case 11..<13: return 2
        case 13..<17: return 3
        case 17..<19: return 4
        case 19..<23: return 5
        default: return 6
        }
    }

    private func fits(_ l: Line, _ c: SayContext, _ now: DateComponents) -> Bool {
        let hour = now.hour ?? 12, day = now.day ?? 15, month = now.month ?? 6, weekday = (now.weekday ?? 2) - 1
        if l.bands != 0 && l.bands & (1 << Self.band(hour)) == 0 { return false }
        if l.days != 0 && l.days & (1 << weekday) == 0 { return false }
        if l.months != 0 && l.months & (1 << month) == 0 { return false }
        if l.n >= 0 && l.n != c.n { return false }
        let f = l.flags
        if (f & F.video != 0 && c.audio) || (f & F.audio != 0 && !c.audio) { return false }
        if (f & F.short != 0 && c.sec >= 60) || (f & F.long != 0 && c.sec < 3600) || (f & F.shots != 0 && c.shots < 1) {
            return false
        }
        if (f & F.auto != 0 && !c.autoStop) || (f & F.clip != 0 && !c.clip) { return false }
        if f & F.newYear != 0 && !(month == 1 && day <= 3) { return false }
        if f & F.yearEnd != 0 && !(month == 12 && day >= 28) { return false }
        if (f & F.needRec != 0 && c.recKey.isEmpty) || (f & F.needAudio != 0 && c.audioKey.isEmpty)
            || (f & F.needShot != 0 && c.shotKey.isEmpty) {
            return false
        }
        return true
    }

    /// A line for the scene at this moment with {n} {t} {mb} {shots} {rec_key} … filled in; "" if none fits.
    func pick(_ s: Scene, _ c: SayContext) -> String {
        let now = clock ?? Self.calendar.dateComponents([.hour, .weekday, .month, .day], from: Date())
        // the lines that fit now; one meant for this very moment (a milestone, a special day, an automatic stop) goes first
        var fit: [Int] = []
        var prio = false
        for i in byScene[s.rawValue] {
            let l = lines[i]
            guard fits(l, c, now) else { continue }
            let p = l.n >= 0 || l.flags & F.prio != 0
            if p && !prio {
                fit.removeAll()
                prio = true
            }
            if p == prio { fit.append(i) }
        }
        guard !fit.isEmpty else {
            lastSource = ""
            return ""
        }
        // not one of the lines said in the last (half as many as fit) times in this scene
        let count = said[s.rawValue], window = UInt32(fit.count / 2)
        var fresh = fit.filter { lines[$0].said == 0 || count - lines[$0].said >= window }
        if fresh.isEmpty { fresh = fit }
        // a line for this time of day, day, season or kind of result is twice as likely as a general one
        func weight(_ l: Line) -> UInt64 { l.bands != 0 || l.days != 0 || l.months != 0 || l.flags & F.more != 0 ? 2 : 1 }
        var r = next() % fresh.reduce(0) { $0 + weight(lines[$1]) }
        var chosen = fresh[fresh.count - 1]
        for i in fresh {
            let w = weight(lines[i])
            if r < w {
                chosen = i
                break
            }
            r -= w
        }
        said[s.rawValue] += 1
        lines[chosen].said = said[s.rawValue]
        lastSource = lines[chosen].source
        return fill(lines[chosen], c)
    }

    /// A line is meant for exactly this minute / screenshot.
    func milestone(_ s: Scene, _ n: Int) -> Bool { byScene[s.rawValue].contains { lines[$0].n == n } }

    private func fill(_ l: Line, _ c: SayContext) -> String {
        var out = ""
        var rest = Substring(l.text)
        while let q = rest.firstIndex(of: "{"), let z = rest[q...].firstIndex(of: "}") {
            out += rest[..<q]
            switch rest[rest.index(after: q)..<z] {
            case "n": out += "\(c.n)"
            case "t": out += Self.span(c.sec)
            case "mb": out += String(format: "%.1f", c.mb)
            case "shots": out += "\(c.shots)"
            case "rec_key": out += c.recKey
            case "audio_key": out += c.audioKey
            case "shot_key": out += c.shotKey
            default: break
            }
            rest = rest[rest.index(after: z)...]
        }
        return out + rest
    }

    /// 45 秒 / 12 分 / 1 時間 / 1 時間 5 分
    static func span(_ sec: Double) -> String {
        let s = Int(max(0, sec).rounded())
        if s < 60 { return "\(s) 秒" }
        let m = s / 60
        if m < 60 { return "\(m) 分" }
        return "\(m / 60) 時間" + (m % 60 > 0 ? " \(m % 60) 分" : "")
    }

    // MARK: --lines-check

    var count: Int { lines.count }

    /// Line i filled in with the longest values, to measure it.
    private func longest(_ i: Int) -> String {
        var c = SayContext()
        c.n = 100
        c.shots = 100
        c.sec = 12 * 3600 + 59 * 60   // 12 時間 59 分
        c.mb = 9999.9
        c.recKey = "⌃⌥⇧⌘F12"
        c.audioKey = c.recKey
        c.shotKey = c.recKey
        return fill(lines[i], c)
    }

    /// --lines-check: how many lines per scene, the time to read them and to pick one, and every line (with the
    /// longest values) that would not fit in her bubble.
    @MainActor
    static func check() -> J {
        let lines = Lines()
        let sw = Stopwatch()
        let problems = lines.load()
        let loadMs = sw.ms
        let room = WindowView.commentRoom
        var per = [Int](repeating: 0, count: Scene.allCases.count)
        var widths: [(CGFloat, Int)] = []
        func entry(_ i: Int, _ w: CGFloat) -> J {
            .obj([("line", .str(lines.lines[i].source)), ("scene", .str(lines.lines[i].scene.name)), ("pt", .n(Double(w))),
                  ("text", .str(lines.longest(i)))])
        }
        var over: [J] = []
        for i in 0..<lines.count {
            per[lines.lines[i].scene.rawValue] += 1
            let w = WindowView.commentWidth(lines.longest(i))
            widths.append((w, i))
            if w > room { over.append(entry(i, w)) }
        }
        widths.sort { $0.0 > $1.0 }
        var c = SayContext()
        c.recKey = "⌃⌥R"
        c.audioKey = "⌃⌥A"
        c.shotKey = "⌃⌥S"
        let picks = 20000
        var empty = 0
        let ps = Stopwatch()
        for i in 0..<picks {
            c.n = i % 200
            c.audio = (i / 7) % 2 == 1
            if lines.pick(Scene(rawValue: i % Scene.allCases.count) ?? .start, c).isEmpty { empty += 1 }
        }
        let pickUs = ps.ms * 1000 / Double(picks)
        // the guard around the window's text: an exception thrown while measuring is caught (the app goes on)
        var after = false
        WindowView.guarded(NSAttributedString(string: "guard test")) {
            NSException(name: .invalidArgumentException, reason: "--lines-check: a test exception").raise()
        }
        after = true
        let ok = lines.count > 0 && problems.isEmpty && over.isEmpty && after
        Log.event("lines.check", ok: ok, ms: loadMs, msg: "lines=\(lines.count) problems=\(problems.count) over=\(over.count)")
        return .obj([("ok", .bool(ok)), ("lines", .i(lines.count)), ("mac_dropped", .i(lines.macDropped)),
                     ("mac_added", .i(lines.macAdded)), ("load_ms", .n(loadMs, 2)), ("pick_us", .n(pickUs, 2)),
                     ("empty_picks", .i(empty)), ("room_pt", .n(Double(room))), ("text_guard", .bool(after)),
                     ("scenes", .obj(Scene.allCases.map { ($0.name, .i(per[$0.rawValue])) })),
                     ("problems", .arr(problems.map { .str($0) })), ("over", .arr(over)),
                     ("widest", .arr(widths.prefix(12).map { entry($0.1, $0.0) }))])
    }
}
