import CoreMedia
import Foundation

/// Where the mixed sound goes: the MP4 / M4A writer, or the WAV writer. Used from the audio queue, then finish().
protocol AudioOut: AnyObject {
    var url: URL { get }
    var failed: Bool { get }
    var errorText: String { get }
    var audioBuffers: Int { get }
    var audioWaits: Int { get }
    var audioErrors: Int { get }
    var audioFrames: Int64 { get }
    /// False when it cannot take the buffer yet (the caller keeps it and tries again).
    func appendAudio(_ sb: CMSampleBuffer) -> Bool
    func finish(at end: CMTime) async -> (ok: Bool, ms: Double, error: String)
    /// Stops without a file: what was written goes (a start that failed or was called off).
    func cancel()
}

extension Writer: AudioOut {}

/// WAV for "sound only": 16-bit PCM, 48 kHz, stereo, written by hand (as the Windows version does). The header's
/// lengths are written again every second, so a crash leaves a file that plays up to about a second before it.
/// RIFF counts in 32 bits, so it fills up at 4,000,000,000 bytes of sound (5 h 47 min): `full`, and the recorder stops.
final class WavWriter: AudioOut {
    static let limit: UInt64 = 4_000_000_000
    let url: URL
    private let fh: FileHandle
    private var dataBytes: UInt64 = 0
    private var lastHeader = DispatchTime.now().uptimeNanoseconds
    private var error: String?
    private(set) var audioBuffers = 0, audioWaits = 0, audioErrors = 0
    private(set) var audioFrames: Int64 = 0
    private(set) var full = false

    init(url: URL) throws {
        self.url = url
        guard FileManager.default.createFile(atPath: url.path, contents: Self.header(dataBytes: 0)) else {
            throw RecError("cannot make \(url.path)")
        }
        fh = try FileHandle(forWritingTo: url)
        try fh.seekToEnd()
    }

    var failed: Bool { error != nil }
    var errorText: String { error ?? "" }

    static func header(dataBytes: UInt64) -> Data {
        var d = Data()
        func tag(_ s: String) { d.append(contentsOf: Array(s.utf8)) }
        func u32(_ v: UInt32) { withUnsafeBytes(of: v.littleEndian) { d.append(contentsOf: $0) } }
        func u16(_ v: UInt16) { withUnsafeBytes(of: v.littleEndian) { d.append(contentsOf: $0) } }
        tag("RIFF")
        u32(UInt32(truncatingIfNeeded: 36 + dataBytes))
        tag("WAVE")
        tag("fmt ")
        u32(16)
        u16(1)                 // PCM
        u16(2)                 // stereo
        u32(48000)
        u32(48000 * 4)         // bytes per second
        u16(4)                 // bytes per frame
        u16(16)                // bits
        tag("data")
        u32(UInt32(truncatingIfNeeded: dataBytes))
        return d
    }

    /// Audio queue: float32 interleaved stereo from the mixer, written as 16-bit.
    func appendAudio(_ sb: CMSampleBuffer) -> Bool {
        guard error == nil, !full, let bb = CMSampleBufferGetDataBuffer(sb) else {
            audioErrors += 1
            return true
        }
        let n = CMBlockBufferGetDataLength(bb) / 4
        var f = [Float](repeating: 0, count: n)
        let st = f.withUnsafeMutableBytes {
            CMBlockBufferCopyDataBytes(bb, atOffset: 0, dataLength: n * 4, destination: $0.baseAddress!)
        }
        guard st == noErr else {
            audioErrors += 1
            return true
        }
        let bytes = UInt64(n * 2)
        if dataBytes + bytes > Self.limit {
            full = true
            return true
        }
        let pcm = f.map { Int16(max(-32768, min(32767, ($0 * 32767).rounded()))) }
        do {
            try fh.write(contentsOf: pcm.withUnsafeBytes { Data($0) })
        } catch {
            self.error = "write: \(error.localizedDescription)"
            audioErrors += 1
            return true
        }
        dataBytes += bytes
        audioBuffers += 1
        audioFrames += Int64(n / 2)
        if DispatchTime.now().uptimeNanoseconds - lastHeader > 1_000_000_000 { writeHeader() }
        return true
    }

    func finish(at end: CMTime) async -> (ok: Bool, ms: Double, error: String) {
        let sw = Stopwatch()
        writeHeader()
        try? fh.synchronize()
        try? fh.close()
        return (error == nil, sw.ms, errorText)
    }

    func cancel() {
        try? fh.close()
        try? FileManager.default.removeItem(at: url)
    }

    private func writeHeader() {
        lastHeader = DispatchTime.now().uptimeNanoseconds
        do {
            let pos = try fh.offset()
            try fh.seek(toOffset: 0)
            try fh.write(contentsOf: Self.header(dataBytes: dataBytes))
            try fh.seek(toOffset: pos)
        } catch {
            self.error = "header: \(error.localizedDescription)"
        }
    }
}
