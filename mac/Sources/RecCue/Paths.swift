import Foundation

/// Where the Mac version keeps its files, the macOS way: settings, logs and session records in
/// ~/Library/Application Support/Rec Cue, recordings in ~/Movies/画面録画 (the Windows version: ビデオ\画面録画).
enum Paths {
    static let appSupport: URL = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
        .appendingPathComponent("Rec Cue", isDirectory: true)
    static var logs: URL { appSupport.appendingPathComponent("logs", isDirectory: true) }
    static var sessions: URL { appSupport.appendingPathComponent("sessions", isDirectory: true) }
    static var test: URL { appSupport.appendingPathComponent("test", isDirectory: true) }
    static var defaultOutDir: URL {
        FileManager.default.urls(for: .moviesDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("画面録画", isDirectory: true)
    }

    @discardableResult
    static func ensure(_ dir: URL) -> Bool {
        do {
            try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
            return true
        } catch {
            return false
        }
    }

    /// "2026-10-26 231001", for file names (the same as the Windows version).
    static func stamp(_ d: Date = Date()) -> String {
        let f = DateFormatter()
        f.locale = Locale(identifier: "en_US_POSIX")
        f.dateFormat = "yyyy-MM-dd HHmmss"
        return f.string(from: d)
    }

    static func fileBytes(_ url: URL) -> Int64 {
        ((try? FileManager.default.attributesOfItem(atPath: url.path))?[.size] as? NSNumber)?.int64Value ?? 0
    }

    /// Free space where the file is written, in MB (-1 when macOS does not say).
    static func freeMB(_ file: URL) -> Double {
        let v = try? file.deletingLastPathComponent().resourceValues(forKeys: [.volumeAvailableCapacityForImportantUsageKey])
        guard let bytes = v?.volumeAvailableCapacityForImportantUsage else { return -1 }
        return Double(bytes) / 1048576.0
    }
}
