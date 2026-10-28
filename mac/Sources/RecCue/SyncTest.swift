import AVFoundation
import CoreMedia

/// `--sync-test`: the test window flashes white and Rec Cue! beeps at the same planned moments. Both are found twice —
/// as ScreenCaptureKit delivered them, and in the finished file — so the result tells what came in apart from what
/// the recorder made of it. All times are seconds from the start of the file (t0).
///
/// The moments are 1.037 s apart, not 1 s: at 15 fps a whole second is a whole number of frames, and every flash
/// would fall on the same place between two frames. Off by 0.555 frame each time, the frame steps average out.
final class SyncTest {
    static let first = 1.0
    static let spacing = 1.037
    static let beepSeconds = 0.1

    let probe: CGRect                 // pixels of the captured screen where the flash is looked for
    let beeper: Beeper?               // nil with --no-beep: flashes only (pictures against the capture, no sound)
    private(set) var planned: [Double] = []
    private let lock = NSLock()
    private var t0 = CMTime.invalid
    private var flashes: [Double] = []   // ScreenCaptureKit: the first bright frame of each flash
    private var beeps: [Double] = []     // ScreenCaptureKit: the first loud sample of each beep (its own time stamp)
    private var light = LightEdge()
    private var sound = SoundOnset()
    private var lumaMin = 999.0, lumaMax = -1.0, frames = 0   // what the probe saw in the capture

    init(probe: CGRect, beep: Bool) throws {
        self.probe = probe
        beeper = beep ? try Beeper(seconds: Self.beepSeconds) : nil
    }

    /// The planned moments of a recording of `seconds` that starts at `t0` (host clock).
    func plan(t0: CMTime, seconds: Double) -> [CMTime] {
        lock.lock()
        self.t0 = t0
        lock.unlock()
        let n = max(1, Int((seconds - Self.first - 0.5) / Self.spacing) + 1)
        planned = (0..<n).map { Self.first + Double($0) * Self.spacing }
        return planned.map { CMTimeAdd(t0, CMTime(seconds: $0, preferredTimescale: 1_000_000_000)) }
    }

    /// Video queue: every complete frame from ScreenCaptureKit, with its own time stamp.
    func frame(_ pb: CVPixelBuffer, at t: CMTime) {
        let y = Self.luma(pb, probe)
        lock.lock()
        defer { lock.unlock() }
        frames += 1
        lumaMin = min(lumaMin, y)
        lumaMax = max(lumaMax, y)
        if t0.isValid && light.feed(y) { flashes.append(CMTimeSubtract(t, t0).seconds) }
    }

    /// Audio queue: the Mac's sound as it lands on the timeline, `at` = the index its own time stamp gives.
    func sound(_ pcm: AVAudioPCMBuffer, at: Int64) {
        guard let ch = pcm.floatChannelData else { return }
        lock.lock()
        defer { lock.unlock() }
        for i in 0..<Int(pcm.frameLength) where sound.feed(abs(ch[0][i])) {
            beeps.append(Double(at + Int64(i)) / AudioMixer.rate)
        }
    }

    /// Reads the finished file back and compares everything with the plan. Planned moments inside a pause are left
    /// out; the others are looked for in the file where the pauses moved them.
    func result(file url: URL, pauses: Pauses) async -> J {
        let found = await Self.findInFile(url, probe: probe)
        let (sFlash, sBeep, seen) = lock.withLock { (flashes, beeps, (frames, lumaMin, lumaMax)) }
        // each planned moment takes the first detection from 50 ms before to 500 ms after it
        func pick(_ xs: [Double], _ p: Double) -> Double? { xs.first { $0 >= p - 0.05 && $0 <= p + 0.5 } }
        func ms(_ x: Double?, _ p: Double) -> J { x.map { .n(($0 - p) * 1000) } ?? .null }
        // capture time → file time (nil inside a pause)
        func fileSec(_ captureSec: Double) -> Double? {
            let c = Int64((captureSec * Double(Pauses.rate)).rounded())
            if pauses.isCut(c) { return nil }
            return Double(pauses.filePosition(at: c)) / Double(Pauses.rate)
        }
        var cut = 0
        var rows: [J] = []
        var sck: [Double] = [], file: [Double] = [], added: [Double] = [], video: [Double] = [], audio: [Double] = []
        var picture: [Double] = []   // file flash − capture flash: what the recorder did to the picture's time
        var counts = [0, 0, 0, 0]
        for p in planned {
            guard let fp = fileSec(p) else {
                cut += 1
                continue
            }
            let a = pick(sFlash, p), b = pick(sBeep, p), c = pick(found.flashes, fp), d = pick(found.beeps, fp)
            for (k, x) in [a, b, c, d].enumerated() where x != nil { counts[k] += 1 }
            rows.append(.obj([("plan_s", .n(p, 3)), ("file_plan_s", .n(fp, 3)), ("sck_flash_ms", ms(a, p)),
                              ("sck_beep_ms", ms(b, p)), ("file_flash_ms", ms(c, fp)), ("file_beep_ms", ms(d, fp))]))
            if let a, let b { sck.append((a - b) * 1000) }
            if let c, let d { file.append((c - d) * 1000) }
            if let a, let b, let c, let d { added.append(((c - d) - (a - b)) * 1000) }
            if let a, let c, let af = fileSec(a) { picture.append((c - af) * 1000) }
            if let c { video.append((c - fp) * 1000) }
            if let d { audio.append((d - fp) * 1000) }
        }
        return .obj([
            ("events", .i(planned.count)), ("cut_by_pause", .i(cut)),
            ("found", .obj([("sck_flash", .i(counts[0])), ("sck_beep", .i(counts[1])), ("file_flash", .i(counts[2])),
                            ("file_beep", .i(counts[3]))])),
            // every detection, matched to the plan or not (s from t0), to see flashes that came at the wrong time
            ("detected", .obj([("sck_flash", .arr(sFlash.map { .n($0, 3) })),
                               ("file_flash", .arr(found.flashes.map { .n($0, 3) })),
                               ("sck_beep", .i(sBeep.count)), ("file_beep", .i(found.beeps.count))])),
            ("probe_luma", .obj([("frames", .i(seen.0)), ("min", .n(seen.1, 0)), ("max", .n(seen.2, 0))])),
            // plus = the picture comes later than the sound
            ("video_minus_audio_ms", .obj([("file", Self.stats(file)), ("sck", Self.stats(sck)),
                                           ("added_by_recorder", Self.stats(added))])),
            ("file_vs_plan_ms", .obj([("video", Self.stats(video)), ("audio", Self.stats(audio))])),
            // + = the flash shows later in the file than ScreenCaptureKit captured it, − = earlier
            ("flash_file_minus_sck_ms", Self.stats(picture)),
            ("probe", .str("\(Int(probe.minX)),\(Int(probe.minY)) \(Int(probe.width))x\(Int(probe.height))")),
            ("error", .s(found.error)), ("rows", .arr(rows)),
        ])
    }

    static func stats(_ xs: [Double]) -> J {
        guard !xs.isEmpty else { return .null }
        return .obj([("avg", .n(xs.reduce(0, +) / Double(xs.count))), ("min", .n(xs.min()!)), ("max", .n(xs.max()!)),
                     ("n", .i(xs.count))])
    }

    /// Mean luma (Y, video range 16–235) of a rectangle of a 4:2:0 picture; -1 when it cannot be read.
    static func luma(_ pb: CVPixelBuffer, _ r: CGRect) -> Double {
        guard CVPixelBufferLockBaseAddress(pb, .readOnly) == kCVReturnSuccess else { return -1 }
        defer { CVPixelBufferUnlockBaseAddress(pb, .readOnly) }
        guard CVPixelBufferGetPlaneCount(pb) >= 2, let base = CVPixelBufferGetBaseAddressOfPlane(pb, 0) else { return -1 }
        let bpr = CVPixelBufferGetBytesPerRowOfPlane(pb, 0)
        let w = CVPixelBufferGetWidthOfPlane(pb, 0), h = CVPixelBufferGetHeightOfPlane(pb, 0)
        let x0 = max(0, Int(r.minX)), y0 = max(0, Int(r.minY)), x1 = min(w, Int(r.maxX)), y1 = min(h, Int(r.maxY))
        guard x1 > x0, y1 > y0 else { return -1 }
        let p = base.assumingMemoryBound(to: UInt8.self)
        var sum = 0
        for y in y0..<y1 { for x in x0..<x1 { sum += Int(p[y * bpr + x]) } }
        return Double(sum) / Double((x1 - x0) * (y1 - y0))
    }

    /// Decodes the file: the first bright frame of each flash and the first loud sample of each beep, in file time.
    static func findInFile(_ url: URL, probe: CGRect) async -> (flashes: [Double], beeps: [Double], error: String?) {
        let asset = AVURLAsset(url: url)
        guard let tracks = try? await asset.load(.tracks) else { return ([], [], "cannot open the file") }
        var flashes: [Double] = [], beeps: [Double] = []
        var error: String?
        if let v = tracks.first(where: { $0.mediaType == .video }) {
            do {
                let r = try AVAssetReader(asset: asset)
                let out = AVAssetReaderTrackOutput(track: v, outputSettings: [
                    kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange,
                ])
                r.add(out)
                r.startReading()
                var edge = LightEdge()
                while let sb = out.copyNextSampleBuffer() {
                    guard let pb = CMSampleBufferGetImageBuffer(sb) else { continue }
                    if edge.feed(luma(pb, probe)) { flashes.append(CMSampleBufferGetPresentationTimeStamp(sb).seconds) }
                }
                if r.status == .failed { error = "video: \(r.error?.localizedDescription ?? "?")" }
            } catch {
                return ([], [], "video: \(error.localizedDescription)")
            }
        }
        if let a = tracks.first(where: { $0.mediaType == .audio }) {
            do {
                let r = try AVAssetReader(asset: asset)
                let out = AVAssetReaderTrackOutput(track: a, outputSettings: [
                    AVFormatIDKey: kAudioFormatLinearPCM, AVLinearPCMBitDepthKey: 32, AVLinearPCMIsFloatKey: true,
                    AVLinearPCMIsNonInterleaved: false, AVLinearPCMIsBigEndianKey: false, AVSampleRateKey: 48000,
                    AVNumberOfChannelsKey: 2,
                ])
                r.add(out)
                r.startReading()
                var onset = SoundOnset()
                while let sb = out.copyNextSampleBuffer() {
                    let pts = CMSampleBufferGetPresentationTimeStamp(sb).seconds
                    guard let bb = CMSampleBufferGetDataBuffer(sb) else { continue }
                    let n = CMBlockBufferGetDataLength(bb) / 4
                    var f = [Float](repeating: 0, count: n)
                    let st = f.withUnsafeMutableBytes {
                        CMBlockBufferCopyDataBytes(bb, atOffset: 0, dataLength: n * 4, destination: $0.baseAddress!)
                    }
                    guard st == noErr else { continue }
                    var i = 0
                    while i < n {   // left channel
                        if onset.feed(abs(f[i])) { beeps.append(pts + Double(i / 2) / AudioMixer.rate) }
                        i += 2
                    }
                }
                if r.status == .failed { error = "audio: \(r.error?.localizedDescription ?? "?")" }
            } catch {
                return (flashes, [], "audio: \(error.localizedDescription)")
            }
        }
        return (flashes, beeps, error)
    }
}

/// A flash starts at the first bright picture (Y > 200) after a dark one (Y < 120).
struct LightEdge {
    private var dark = false

    mutating func feed(_ y: Double) -> Bool {
        if y < 0 { return false }
        if y < 120 {
            dark = true
            return false
        }
        if y > 200 && dark {
            dark = false
            return true
        }
        return false
    }
}

/// A beep starts at the first sample above -20 dBFS that follows at least 100 ms below it (the Windows version's rule).
struct SoundOnset {
    private var quiet = 4800

    mutating func feed(_ v: Float) -> Bool {
        if v > 0.1 {
            let onset = quiet >= 4800
            quiet = 0
            return onset
        }
        quiet += 1
        return false
    }
}

/// Short beeps (1 kHz, -12 dBFS, 5 ms fades) played by Rec Cue! itself at given host times.
final class Beeper {
    private let engine = AVAudioEngine()
    private let player = AVAudioPlayerNode()
    private let buffer: AVAudioPCMBuffer

    init(seconds: Double) throws {
        guard let fmt = AVAudioFormat(standardFormatWithSampleRate: AudioMixer.rate, channels: 2),
              let b = AVAudioPCMBuffer(pcmFormat: fmt, frameCapacity: AVAudioFrameCount(seconds * AudioMixer.rate)),
              let ch = b.floatChannelData else { throw RecError("cannot make the beep") }
        let n = Int(b.frameCapacity)
        b.frameLength = AVAudioFrameCount(n)
        let fade = Int(0.005 * AudioMixer.rate)
        for i in 0..<n {
            let env = Float(min(1.0, Double(min(i, n - 1 - i)) / Double(fade)))
            let v = 0.25 * env * Float(sin(2 * Double.pi * 1000 * Double(i) / AudioMixer.rate))
            ch[0][i] = v
            ch[1][i] = v
        }
        buffer = b
        engine.attach(player)
        engine.connect(player, to: engine.mainMixerNode, format: fmt)
        try engine.start()
        player.play()
    }

    func schedule(at t: CMTime) {
        player.scheduleBuffer(buffer, at: AVAudioTime(hostTime: CMClockConvertHostTimeToSystemUnits(t)), options: [],
                              completionHandler: nil)
    }

    func stop() {
        player.stop()
        engine.stop()
    }
}
