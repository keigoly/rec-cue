import Foundation

/// One JSON object per line in ~/Library/Application Support/Rec Cue/logs/rec-cue-YYYYMMDD.log, the same lines as the
/// Windows version: every line carries ts / sid / phase; events add ok / ms / msg.
enum Log {
    private static let lock = NSLock()
    private static var handle: FileHandle?
    private static var sid = ""
    private static var lastErr = ""
    /// The file the lines go to (the day the app started, as in the Windows version).
    private(set) static var file: URL?
    private static let tsFormat: DateFormatter = {
        let f = DateFormatter()
        f.locale = Locale(identifier: "en_US_POSIX")
        f.dateFormat = "yyyy-MM-dd'T'HH:mm:ss.SSSxxx"
        return f
    }()

    static func open() {
        lock.lock()
        defer { lock.unlock() }
        if handle != nil { return }
        guard Paths.ensure(Paths.logs) else { return }
        let day = DateFormatter()
        day.locale = Locale(identifier: "en_US_POSIX")
        day.dateFormat = "yyyyMMdd"
        let url = Paths.logs.appendingPathComponent("rec-cue-\(day.string(from: Date())).log")
        file = url
        // O_APPEND: every write goes to the end, so two copies running at once (a --verify-file during a recording)
        // do not write over each other's lines
        let fd = Darwin.open(url.path, O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0o644)
        if fd >= 0 { handle = FileHandle(fileDescriptor: fd, closeOnDealloc: true) }
    }

    /// 2026-10-26T23:10:01.234+09:00
    static func nowIso() -> String { tsFormat.string(from: Date()) }

    static func setSession(_ s: String) {
        lock.lock()
        sid = s
        lock.unlock()
    }

    /// 20261026-231001-1a2b
    static func newSessionId() -> String {
        let f = DateFormatter()
        f.locale = Locale(identifier: "en_US_POSIX")
        f.dateFormat = "yyyyMMdd-HHmmss"
        return f.string(from: Date()) + String(format: "-%04x", UInt32.random(in: 0...0xffff))
    }

    static func event(_ phase: String, ok: Bool, ms: Double? = nil, msg: String = "") {
        var kv: [(String, J)] = [("ok", .bool(ok))]
        if let ms { kv.append(("ms", .n(ms))) }
        if !msg.isEmpty { kv.append(("msg", .str(msg))) }
        write(phase, kv)
        if !ok {
            lock.lock()
            lastErr = phase + ": " + msg
            lock.unlock()
        }
    }

    static func line(_ phase: String, _ fields: [(String, J)]) { write(phase, fields) }

    static func lastError() -> String {
        lock.lock()
        defer { lock.unlock() }
        return lastErr
    }

    private static func write(_ phase: String, _ fields: [(String, J)]) {
        lock.lock()
        defer { lock.unlock() }
        let o = J.obj([("ts", .str(nowIso())), ("sid", .str(sid)), ("phase", .str(phase))] + fields)
        if let d = (o.text + "\n").data(using: .utf8) { try? handle?.write(contentsOf: d) }
    }
}

/// Milliseconds since it was made.
struct Stopwatch {
    private let t0 = DispatchTime.now().uptimeNanoseconds
    var ms: Double { Double(DispatchTime.now().uptimeNanoseconds - t0) / 1e6 }
}

/// A value handed to another thread by hand: the code that uses it keeps it safe.
final class Unchecked<T>: @unchecked Sendable {
    let value: T
    init(_ v: T) { value = v }
}

/// True the first time only, on any thread.
final class Once: @unchecked Sendable {
    private let lock = NSLock()
    private var done = false

    func claim() -> Bool {
        lock.withLock {
            if done { return false }
            done = true
            return true
        }
    }
}
