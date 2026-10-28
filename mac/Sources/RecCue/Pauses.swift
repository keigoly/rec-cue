import CoreMedia
import Foundation

/// The pauses of a recording. A paused stretch is cut out of the picture and the sound alike, so the file is one
/// continuous piece (the Windows version's pause). Positions are 48 kHz frames from t0 on the capture (host) clock,
/// so the picture and the sound lose exactly the same stretch. Any thread.
final class Pauses {
    static let rate: Int64 = 48000
    private let lock = NSLock()
    private var t0 = CMTime.invalid
    private var spans: [(from: Int64, to: Int64?)] = []   // in order; only the last can still be open

    func start(at t: CMTime) {
        lock.withLock { t0 = t }
    }

    /// The capture position of a host time, in frames from t0 (0 until start(at:): the window reads the time while
    /// the recording sets its clock, and an invalid t0 made it NaN, which crashed the app — 2026-10-28).
    func position(at t: CMTime) -> Int64 {
        let start = lock.withLock { t0 }
        guard start.isValid && t.isValid else { return 0 }
        return Int64((CMTimeSubtract(t, start).seconds * Double(Self.rate)).rounded())
    }

    var paused: Bool { lock.withLock { spans.last.map { $0.to == nil } ?? false } }
    var count: Int { lock.withLock { spans.count } }

    /// Starts a pause at capture position `at`; false when already paused.
    func pause(at: Int64) -> Bool {
        lock.withLock {
            if let last = spans.last {
                guard let end = last.to else { return false }
                spans.append((max(at, end), nil))
            } else {
                spans.append((max(at, 0), nil))
            }
            return true
        }
    }

    /// Ends the pause going on at `at`: its length in frames, or nil when not paused.
    func resume(at: Int64) -> Int64? {
        lock.withLock {
            guard let last = spans.last, last.to == nil else { return nil }
            let end = max(at, last.from)
            spans[spans.count - 1].to = end
            return end - last.from
        }
    }

    /// Frames cut before capture position `c` (a pause going on counts up to `c`).
    func cut(before c: Int64) -> Int64 {
        lock.withLock { spans.reduce(0) { $0 + max(0, min(c, $1.to ?? c) - $1.from) } }
    }

    /// The file position of capture position `c` (inside a pause: where that pause began).
    func filePosition(at c: Int64) -> Int64 { c - cut(before: c) }

    /// The capture position of file position `f`, or nil when `f` is at or after the start of the pause going on.
    func capturePosition(atFile f: Int64) -> Int64? {
        lock.withLock {
            var c = f
            for s in spans where s.from <= c {
                guard let end = s.to else { return nil }
                c += end - s.from
            }
            return c
        }
    }

    func isCut(_ c: Int64) -> Bool {
        lock.withLock { spans.contains { c >= $0.from && c < ($0.to ?? Int64.max) } }
    }

    /// The first pause that starts after capture position `c`.
    func nextStart(after c: Int64) -> Int64? {
        lock.withLock { spans.first { $0.from > c }?.from }
    }

    /// The cut stretches as half-open ranges of capture positions (a pause going on runs to Int64.max).
    func cuts() -> [(Int64, Int64)] {
        lock.withLock { spans.map { ($0.from, $0.to ?? Int64.max) } }
    }
}
