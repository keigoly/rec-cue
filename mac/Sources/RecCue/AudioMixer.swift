import AVFoundation
import CoreMedia

/// Puts the Mac's sound and the microphone on one 48 kHz timeline by their host-clock timestamps and mixes them into
/// float32 stereo for the AAC track — the Windows version's mixer: one clock, silence where a source has nothing, and
/// `latency` held back so a late buffer still lands in place. Everything runs on one serial queue (the audio queue).
final class AudioMixer {
    static let rate = 48000.0
    static let chunk: Int64 = 480            // 10 ms: levels and silence are measured per chunk
    static let silent: Float = 0.00178       // -55 dBFS

    final class Source {
        let kind: String
        let name: String
        var gain: Float = 1
        fileprivate var ring: [Float]
        fileprivate var next: Int64 = -1     // timeline index the next buffer should start at
        var buffers = 0, gaps = 0, overlaps = 0, formatChanges = 0, badTimes = 0
        var frames: Int64 = 0, pre: Int64 = 0, late: Int64 = 0, ahead: Int64 = 0
        var maxJoin: Int64 = 0                // the largest jitter that was joined up, in frames
        var firstPlaced: Int64 = -1, endPlaced: Int64 = -1   // the span of the timeline this source covered
        var meterPeak: Float = 0                // the loudest since the window last looked (its meter)
        var silentRun: Int64 = 0                // frames below -55 dBFS in a row (a pause starts it again)
        var peak: Float = 0, ss: Double = 0, n: Int64 = 0, chunks = 0, silentChunks = 0
        var error: String?
        var format = ""
        fileprivate var converter: AVAudioConverter?
        fileprivate var inFormat: AVAudioFormat?
        fileprivate var direct = false   // already float32 48 kHz non-interleaved, 1 or 2 channels

        init(kind: String, name: String) {
            self.kind = kind
            self.name = name
            ring = []   // sized by the mixer
        }

        func json() -> J {
            let rms = n > 0 ? (ss / (2.0 * Double(n))).squareRoot() : 0
            return .obj([("kind", .str(kind)), ("name", .str(name)), ("format", .str(format)),
                          ("buffers", .i(buffers)), ("frames", .i(frames)), ("gaps", .i(gaps)),
                          ("overlaps", .i(overlaps)), ("max_join_ms", .n(Double(maxJoin) / 48.0)),
                          ("pre_frames", .i(pre)), ("late_frames", .i(late)),
                          ("ahead_frames", .i(ahead)), ("format_changes", .i(formatChanges)),
                          ("bad_times", .i(badTimes)),
                          ("first_ms", firstPlaced < 0 ? .null : .n(Double(firstPlaced) / 48.0)),
                          ("end_ms", endPlaced < 0 ? .null : .n(Double(endPlaced) / 48.0)),
                          ("peak_db", .n(AudioMixer.db(peak))), ("rms_db", .n(AudioMixer.db(Float(rms)))),
                          ("silent_pct", .n(chunks > 0 ? 100.0 * Double(silentChunks) / Double(chunks) : 100.0, 0)),
                          ("error", .s(error))])
        }
    }

    let capacity: Int        // ring length in frames
    let latency: Int64       // frames held back
    private(set) var sources: [Source]
    private(set) var t0 = CMTime.invalid
    private(set) var mixed: Int64 = 0       // frames mixed (capture positions)
    private(set) var emitted: Int64 = 0     // frames handed out (file positions: paused stretches left out)
    private(set) var cutFrames: Int64 = 0
    private(set) var clipped: Int64 = 0
    private(set) var makeErrors = 0
    /// Every buffer as it arrives (source, float32 48 kHz, the timeline index its own time stamp gives) — the sync test.
    var probe: ((Int, AVAudioPCMBuffer, Int64) -> Void)?
    /// Only the meters (the level check): pump() mixes and measures, but makes no sample buffers.
    var meterOnly = false
    private var fmt: CMAudioFormatDescription?
    /// Mixed sound whose sample buffer could not be made: handed out first next time, so the timeline has no hole.
    private var pending: (buf: [Float], frames: Int, at: Int64)?

    init(sources: [Source], latencyMs: Int, capacitySeconds: Double = 4) {
        self.sources = sources
        capacity = Int(capacitySeconds * Self.rate)
        latency = Int64(Double(latencyMs) / 1000.0 * Self.rate)
        for s in sources { s.ring = [Float](repeating: 0, count: capacity * 2) }
    }

    static func db(_ lin: Float) -> Double { lin > 0 ? max(-120, 20 * log10(Double(lin))) : -120 }

    /// What the window's meters show of a source: the loudest since it last looked, and how long it has been silent.
    struct Level {
        let kind: String    // system / mic
        let name: String
        let db: Double
        let silentMs: Double
    }

    /// The meters (on the audio queue): each source's loudest since the last call, which starts again.
    func levels() -> [Level] {
        sources.map { s in
            let l = Level(kind: s.kind, name: s.name, db: Self.db(s.meterPeak), silentMs: Double(s.silentRun) / 48)
            s.meterPeak = 0
            return l
        }
    }

    func start(at t: CMTime) {
        t0 = t
    }

    /// One buffer from ScreenCaptureKit.
    func push(_ sb: CMSampleBuffer, source k: Int) {
        guard t0.isValid, k >= 0, k < sources.count else { return }
        let s = sources[k]
        guard let pcm = s.convert(sb) else { return }
        let n = Int64(pcm.frameLength)
        guard n > 0, let ch = pcm.floatChannelData else { return }
        let left = ch[0], right = pcm.format.channelCount > 1 ? ch[1] : ch[0]
        let pts = CMSampleBufferGetPresentationTimeStamp(sb)
        guard pts.isNumeric else {   // no place on the timeline (and Int64(NaN) would stop the process)
            s.badTimes += 1
            return
        }
        s.buffers += 1
        s.frames += n
        let raw = Int64((CMTimeSubtract(pts, t0).seconds * Self.rate).rounded())
        var at = raw
        if s.next >= 0 {
            // up to 10 ms off is clock jitter: join it up so there is no click
            if abs(at - s.next) <= Self.chunk {
                s.maxJoin = max(s.maxJoin, abs(at - s.next))
                at = s.next
            } else if at > s.next {
                s.gaps += 1
            } else {
                s.overlaps += 1
            }
        }
        s.next = at + n
        probe?(k, pcm, raw)
        let cap = Int64(capacity)
        for i in 0..<n {
            let idx = at + i
            if idx < 0 { s.pre += 1; continue }            // before the first frame of the video
            if idx < mixed { s.late += 1; continue }       // already handed out: came later than `latency`
            if idx >= mixed + cap { s.ahead += n - i; break }
            let p = Int(idx % cap) * 2
            s.ring[p] = left[Int(i)]
            s.ring[p + 1] = right[Int(i)]
            if s.firstPlaced < 0 { s.firstPlaced = idx }
            s.endPlaced = max(s.endPlaced, idx + 1)
        }
    }

    /// Mixes what is ready at `now` (everything older than `latency`, or everything with `flush`) and returns the
    /// sample buffers to append, oldest first. Frames inside `cuts` (paused stretches, capture positions) are mixed
    /// (the meters go on) but left out of the file, and what follows moves up: buffers are stamped with their place
    /// in the file (`emitted`), not on the capture clock.
    func pump(now: CMTime, flush: Bool = false, cuts: [(Int64, Int64)] = []) -> [CMSampleBuffer] {
        guard t0.isValid else { return [] }
        let nowIdx = Int64((CMTimeSubtract(now, t0).seconds * Self.rate).rounded(.down))
        let ready = flush ? nowIdx : nowIdx - latency
        var outBufs: [CMSampleBuffer] = []
        if let p = pending {
            guard let sb = makeBuffer(p.buf, frames: p.frames, at: p.at) else {
                makeErrors += 1
                return outBufs
            }
            outBufs.append(sb)
            pending = nil
        }
        let cap = Int64(capacity)
        while mixed + Self.chunk <= ready {
            let m = min(ready - mixed, Self.rate.int64)   // at most 1 s per buffer
            let frames = Int(m - m % Self.chunk)
            var buf = [Float](repeating: 0, count: frames * 2)
            for s in sources {
                var off = 0
                while off < frames {
                    var pk: Float = 0
                    var ss: Double = 0
                    for j in off..<(off + Int(Self.chunk)) {
                        let p = Int((mixed + Int64(j)) % cap) * 2
                        let l = s.ring[p] * s.gain, r = s.ring[p + 1] * s.gain
                        s.ring[p] = 0
                        s.ring[p + 1] = 0
                        buf[j * 2] += l
                        buf[j * 2 + 1] += r
                        pk = max(pk, abs(l), abs(r))
                        ss += Double(l * l + r * r)
                    }
                    s.peak = max(s.peak, pk)
                    s.meterPeak = max(s.meterPeak, pk)
                    // the run of silence the window warns about: not counted while paused (as the Windows version)
                    let at = mixed + Int64(off)
                    let held = cuts.contains { at >= $0.0 && at < $0.1 }
                    s.silentRun = pk < Self.silent && !held ? s.silentRun + Self.chunk : 0
                    s.ss += ss
                    s.n += Self.chunk
                    s.chunks += 1
                    if pk < Self.silent { s.silentChunks += 1 }
                    off += Int(Self.chunk)
                }
            }
            for k in 0..<buf.count where abs(buf[k]) > 1 {
                buf[k] = buf[k] > 0 ? 1 : -1
                clipped += 1
            }
            let from = mixed
            mixed += Int64(frames)   // the ring is consumed either way
            if meterOnly { continue }
            var out = buf, n = frames
            if cuts.contains(where: { $0.0 < mixed && $0.1 > from }) {
                var kept: [Float] = []
                kept.reserveCapacity(buf.count)
                for j in 0..<frames {
                    let idx = from + Int64(j)
                    if cuts.contains(where: { idx >= $0.0 && idx < $0.1 }) { continue }
                    kept.append(buf[j * 2])
                    kept.append(buf[j * 2 + 1])
                }
                n = kept.count / 2
                cutFrames += Int64(frames - n)
                out = kept
            }
            if n == 0 { continue }
            let at = emitted
            emitted += Int64(n)
            guard let sb = makeBuffer(out, frames: n, at: at) else {
                makeErrors += 1
                pending = (out, n, at)
                break
            }
            outBufs.append(sb)
        }
        return outBufs
    }

    func json() -> J {
        .obj([("mixed_s", .n(Double(mixed) / Self.rate, 2)), ("cut_s", .n(Double(cutFrames) / Self.rate, 2)),
              ("clipped_samples", .i(clipped)), ("make_errors", .i(makeErrors)),
              ("sources", .arr(sources.map { $0.json() }))])
    }

    private func makeBuffer(_ data: [Float], frames: Int, at idx: Int64) -> CMSampleBuffer? {
        if fmt == nil {
            var asbd = AudioStreamBasicDescription(
                mSampleRate: Self.rate, mFormatID: kAudioFormatLinearPCM,
                mFormatFlags: kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked, mBytesPerPacket: 8,
                mFramesPerPacket: 1, mBytesPerFrame: 8, mChannelsPerFrame: 2, mBitsPerChannel: 32, mReserved: 0)
            CMAudioFormatDescriptionCreate(allocator: kCFAllocatorDefault, asbd: &asbd, layoutSize: 0, layout: nil,
                                           magicCookieSize: 0, magicCookie: nil, extensions: nil,
                                           formatDescriptionOut: &fmt)
        }
        guard let fmt else { return nil }
        let bytes = frames * 8
        var block: CMBlockBuffer?
        guard CMBlockBufferCreateWithMemoryBlock(allocator: kCFAllocatorDefault, memoryBlock: nil, blockLength: bytes,
                                                 blockAllocator: kCFAllocatorDefault, customBlockSource: nil,
                                                 offsetToData: 0, dataLength: bytes,
                                                 flags: kCMBlockBufferAssureMemoryNowFlag,
                                                 blockBufferOut: &block) == noErr, let block else { return nil }
        let st = data.withUnsafeBytes { p in
            CMBlockBufferReplaceDataBytes(with: p.baseAddress!, blockBuffer: block, offsetIntoDestination: 0,
                                          dataLength: bytes)
        }
        guard st == noErr else { return nil }
        var sb: CMSampleBuffer?
        let pts = CMTimeAdd(t0, CMTime(value: idx, timescale: CMTimeScale(Self.rate)))
        guard CMAudioSampleBufferCreateReadyWithPacketDescriptions(
            allocator: kCFAllocatorDefault, dataBuffer: block, formatDescription: fmt, sampleCount: frames,
            presentationTimeStamp: pts, packetDescriptions: nil, sampleBufferOut: &sb) == noErr else { return nil }
        return sb
    }
}

private extension Double {
    var int64: Int64 { Int64(self) }
}

extension AudioMixer.Source {
    /// The buffer as float32 non-interleaved at 48 kHz, 1 or 2 channels. The Mac's sound already comes that way
    /// (we ask ScreenCaptureKit for it); a microphone comes in its own format and goes through a converter, which
    /// takes the first two channels of a device with more. Every nil leaves the reason in `error`.
    fileprivate func convert(_ sb: CMSampleBuffer) -> AVAudioPCMBuffer? {
        guard let fd = CMSampleBufferGetFormatDescription(sb),
              let asbd = CMAudioFormatDescriptionGetStreamBasicDescription(fd)?.pointee else {
            error = "no audio format"
            return nil
        }
        let n = CMSampleBufferGetNumSamples(sb)
        guard n > 0 else { return nil }   // nothing to place
        // (from the format description, not the stream description: that one fails for more than 2 channels)
        let inFmt = AVAudioFormat(cmAudioFormatDescription: fd)
        guard let inBuf = AVAudioPCMBuffer(pcmFormat: inFmt, frameCapacity: AVAudioFrameCount(n)) else {
            error = "not PCM: \(inFmt)"
            return nil
        }
        inBuf.frameLength = AVAudioFrameCount(n)
        let st = CMSampleBufferCopyPCMDataIntoAudioBufferList(sb, at: 0, frameCount: Int32(n),
                                                              into: inBuf.mutableAudioBufferList)
        guard st == noErr else {
            error = "copy \(st)"
            return nil
        }
        if inFormat == nil || inFormat != inFmt {
            if inFormat != nil { formatChanges += 1 }
            inFormat = inFmt
            format = String(format: "%.0f Hz %u ch %@%@", inFmt.sampleRate, inFmt.channelCount,
                            inFmt.commonFormat == .pcmFormatFloat32 ? "float32" : "\(asbd.mBitsPerChannel) bit",
                            inFmt.isInterleaved ? " interleaved" : "")
            converter = nil
            direct = inFmt.commonFormat == .pcmFormatFloat32 && inFmt.sampleRate == AudioMixer.rate
                && !inFmt.isInterleaved && inFmt.channelCount <= 2
            if !direct {
                if let out = AVAudioFormat(commonFormat: .pcmFormatFloat32, sampleRate: AudioMixer.rate,
                                           channels: min(2, inFmt.channelCount), interleaved: false),
                   let c = AVAudioConverter(from: inFmt, to: out) {
                    if inFmt.channelCount > 2 { c.channelMap = [0, 1] }
                    converter = c
                } else {
                    error = "no converter for \(format)"
                }
            }
        }
        if direct { return inBuf }
        guard let conv = converter else { return nil }   // never hand on sound in a format the mixer does not read
        let cap = AVAudioFrameCount(Double(n) * AudioMixer.rate / inFmt.sampleRate) + 64
        guard let outBuf = AVAudioPCMBuffer(pcmFormat: conv.outputFormat, frameCapacity: cap) else {
            error = "no buffer for the converted sound"
            return nil
        }
        var fed = false
        var err: NSError?
        let status = conv.convert(to: outBuf, error: &err) { _, outStatus in
            if fed {
                outStatus.pointee = .noDataNow
                return nil
            }
            fed = true
            outStatus.pointee = .haveData
            return inBuf
        }
        if status == .error {
            error = "convert: \(err?.localizedDescription ?? "?")"
            return nil
        }
        return outBuf
    }
}
