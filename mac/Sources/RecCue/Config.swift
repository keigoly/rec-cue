import Foundation

/// ~/Library/Application Support/Rec Cue/config.ini — key=value in UTF-8, the same keys as the Windows version where
/// they mean the same thing. Made with comments on the first run. Read at the start of each recording; the command
/// line wins over it.
struct Config {
    var outDir = ""              // empty: ~/Movies/画面録画
    var namePrefix = "Rec Cue"
    var fps = 15
    var videoKbps = 2000
    var audioKbps = 128
    var audioFormat = "m4a"
    var monitor = 0
    var cursor = true
    var mic = "auto"
    var system = "auto"
    var micGainDb = 0.0
    var systemGainDb = 0.0
    var latencyMs = 150
    var shotDir = ""             // empty: where the recordings go
    var shotClipboard = true
    var topmost = false          // the window stays above the others (the pin)
    var hotkey = "Ctrl+Option+R"        // shortcuts in any app (empty / none: none); three keys, not four
    var audioHotkey = "Ctrl+Option+A"
    var shotHotkey = "Ctrl+Option+S"
    var githubUrl = "https://github.com/keigoly/rec-cue"   // the gear menu's links (http / https only)
    var siteUrl = "https://official.keigoly.jp/"

    static var url: URL { testURL ?? Paths.appSupport.appendingPathComponent("config.ini") }
    /// Tests (--ui-autotest): another config.ini in place of the real one, while the folders are changed.
    static var testURL: URL?

    static let defaultText = """
    ; Rec Cue! の設定 (UTF-8)。; で始まる行はコメントです。変更は次の録画から反映されます。

    ; 保存先。空なら「ムービー/画面録画」(歯車の設定の「変更」で選ぶと書き換わる)
    out_dir=
    ; ファイル名の先頭 (後ろに 2026-10-27 193001 のような開始時刻が付く)
    name_prefix=Rec Cue

    ; 1 秒あたりのコマ数 (1〜60)。会議や資料なら 10〜15 で十分です
    fps=15
    ; 映像のビットレート (kbps)。2000 なら 1 時間でおよそ 0.9 GB
    video_kbps=2000
    ; 音声のビットレート (kbps)。96 / 128 / 160 / 192 (録画の AAC と、録音だけの M4A)
    audio_kbps=128
    ; 録音だけの形式: m4a (AAC。小さく、Mac・iPhone・Windows で再生できる) / wav (無圧縮。1 時間でおよそ 690 MB・
    ;   5 時間 47 分まで)。MP3 は macOS に書き出す部品が無いため選べません。録画はいつも MP4 です
    audio_format=m4a
    ; 録る画面。0 = メインの画面 (メニューバーのある画面)、1 以降 = ほかの画面
    monitor=0
    ; マウスカーソルを映す (1 / 0)
    cursor=1

    ; マイク: auto / default (どちらも Mac の既定の入力) / none / 機器名の一部
    mic=auto
    ; Mac の音 (相手の声やアプリの音): auto (すべてのアプリの音。Rec Cue! 自身の音は除く) / none
    system=auto
    ; 音量の補正 (dB)。-20〜20
    mic_gain_db=0
    system_gain_db=0
    ; 音声を揃えるための待ち時間 (ms)。音が途切れるときだけ大きくする
    latency_ms=150

    ; スクショ (録る画面と同じ画面。Rec Cue! の窓とマウスポインタは写りません) の保存先。空なら録画と同じ場所
    ;   (歯車の設定の「変更」で選ぶと書き換わる)
    shot_dir=
    ; スクショをクリップボードにもコピーする (1 / 0)
    shot_clipboard=1
    ; 窓をほかのアプリより手前に固定する (1 / 0)。窓のピンのボタンでも切り替えられます
    topmost=0

    ; ショートカットキー (どのアプリを使っていても効く)。Ctrl / Option / Shift / Cmd と、A〜Z・0〜9・F1〜F20 を
    ;   + でつなぐ (例 Ctrl+Option+R)。none で使わない。ほかのアプリが使っている組み合わせは使えません
    ; 録画の開始と停止
    hotkey=Ctrl+Option+R
    ; 録音だけの開始と停止
    audio_hotkey=Ctrl+Option+A
    ; スクショ
    shot_hotkey=Ctrl+Option+S

    ; 歯車の設定の「GitHub」「公式サイト」で開くページ
    github_url=https://github.com/keigoly/rec-cue
    site_url=https://official.keigoly.jp/
    """

    /// The settings, and what was wrong in the file (unknown keys, values out of range — those keep the default).
    static func load() -> (Config, [String]) {
        var c = Config()
        var problems: [String] = []
        if !FileManager.default.fileExists(atPath: url.path) {
            Paths.ensure(Paths.appSupport)
            do {
                try (defaultText + "\n").write(to: url, atomically: true, encoding: .utf8)
            } catch {
                problems.append("cannot write \(url.path): \(error.localizedDescription)")
            }
            return (c, problems)
        }
        guard let text = try? String(contentsOf: url, encoding: .utf8) else {
            return (c, ["cannot read \(url.path) as UTF-8: the defaults are used"])
        }
        func int(_ k: String, _ v: String, _ lo: Int, _ hi: Int) -> Int? {
            guard let n = Int(v), n >= lo, n <= hi else {
                problems.append("\(k)=\(v): not \(lo)…\(hi)")
                return nil
            }
            return n
        }
        func db(_ k: String, _ v: String) -> Double? {
            guard let d = Double(v), d >= -20, d <= 20 else {
                problems.append("\(k)=\(v): not -20…20")
                return nil
            }
            return d
        }
        for (i, raw) in text.components(separatedBy: .newlines).enumerated() {
            let line = raw.trimmingCharacters(in: .whitespaces)
            if line.isEmpty || line.hasPrefix(";") || line.hasPrefix("#") { continue }
            guard let eq = line.firstIndex(of: "=") else {
                problems.append("line \(i + 1): no =")
                continue
            }
            let k = line[..<eq].trimmingCharacters(in: .whitespaces).lowercased()
            let v = line[line.index(after: eq)...].trimmingCharacters(in: .whitespaces)
            switch k {
            case "out_dir": c.outDir = v
            case "name_prefix": c.namePrefix = v.isEmpty ? c.namePrefix : v
            case "fps": c.fps = int(k, v, 1, 60) ?? c.fps
            case "video_kbps": c.videoKbps = int(k, v, 200, 50000) ?? c.videoKbps
            case "audio_kbps": c.audioKbps = int(k, v, 64, 320) ?? c.audioKbps
            case "audio_format":
                if ["m4a", "wav"].contains(v.lowercased()) {
                    c.audioFormat = v.lowercased()
                } else {
                    problems.append("audio_format=\(v): m4a or wav (macOS cannot write MP3)")
                }
            case "monitor": c.monitor = int(k, v, 0, 15) ?? c.monitor
            case "cursor": c.cursor = v != "0"
            case "mic": c.mic = v.isEmpty ? c.mic : v
            case "system": c.system = v.isEmpty ? c.system : v
            case "mic_gain_db": c.micGainDb = db(k, v) ?? c.micGainDb
            case "system_gain_db": c.systemGainDb = db(k, v) ?? c.systemGainDb
            case "latency_ms": c.latencyMs = int(k, v, 50, 1000) ?? c.latencyMs
            case "shot_dir": c.shotDir = v
            case "shot_clipboard": c.shotClipboard = v != "0"
            case "topmost": c.topmost = ["1", "true", "yes", "on"].contains(v.lowercased())
            case "hotkey": c.hotkey = v
            case "audio_hotkey": c.audioHotkey = v
            case "shot_hotkey": c.shotHotkey = v
            case "github_url": c.githubUrl = v.isEmpty ? c.githubUrl : v
            case "site_url": c.siteUrl = v.isEmpty ? c.siteUrl : v
            default: problems.append("unknown key \(k) (line \(i + 1))")
            }
        }
        return (c, problems)
    }

    /// Sets one key in config.ini and keeps everything else, comments included (adds the line if it is missing).
    @discardableResult
    static func set(_ key: String, _ value: String) -> Bool {
        let text = (try? String(contentsOf: url, encoding: .utf8)) ?? defaultText
        var lines = text.components(separatedBy: "\n")
        if let i = lines.firstIndex(where: { $0.trimmingCharacters(in: .whitespaces).lowercased().hasPrefix(key + "=") }) {
            lines[i] = "\(key)=\(value)"
        } else {
            lines.append("\(key)=\(value)")
        }
        do {
            try lines.joined(separator: "\n").write(to: url, atomically: true, encoding: .utf8)
            Log.event("ui.config", ok: true, msg: "\(key)=\(value)")
            return true
        } catch {
            Log.event("ui.config", ok: false, msg: "\(key)=\(value): \(error.localizedDescription)")
            return false
        }
    }

    /// The folder the files go to (out_dir with ~ expanded, or ~/Movies/画面録画).
    var resolvedOutDir: URL {
        outDir.isEmpty ? Paths.defaultOutDir
                       : URL(fileURLWithPath: (outDir as NSString).expandingTildeInPath, isDirectory: true)
    }

    /// Where screenshots go (shot_dir, or where the recordings go).
    var resolvedShotDir: URL {
        shotDir.isEmpty ? resolvedOutDir
                        : URL(fileURLWithPath: (shotDir as NSString).expandingTildeInPath, isDirectory: true)
    }

    func json() -> J {
        .obj([("out_dir", .str(outDir)), ("name_prefix", .str(namePrefix)), ("fps", .i(fps)),
              ("video_kbps", .i(videoKbps)), ("audio_kbps", .i(audioKbps)), ("audio_format", .str(audioFormat)),
              ("monitor", .i(monitor)), ("cursor", .bool(cursor)), ("mic", .str(mic)), ("system", .str(system)),
              ("mic_gain_db", .n(micGainDb)), ("system_gain_db", .n(systemGainDb)), ("latency_ms", .i(latencyMs)),
              ("shot_dir", .str(shotDir)), ("shot_clipboard", .bool(shotClipboard)), ("topmost", .bool(topmost)),
              ("hotkey", .str(hotkey)), ("audio_hotkey", .str(audioHotkey)), ("shot_hotkey", .str(shotHotkey))])
    }
}
