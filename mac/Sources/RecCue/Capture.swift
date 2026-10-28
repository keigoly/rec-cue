import AVFoundation
import ScreenCaptureKit

/// ScreenCaptureKit: one display, the Mac's sound and the microphone in one stream, all stamped on the host clock.
/// The newest frame is kept for the recorder's ticks; sound goes straight to `onAudio` on the audio queue.
final class Capture: NSObject, SCStreamOutput, SCStreamDelegate {
    struct Counts {
        var complete = 0, idle = 0, blank = 0, suspended = 0, started = 0, stopped = 0, other = 0, noImage = 0
    }

    struct Frame {
        let buffer: CVPixelBuffer
        let time: CMTime   // when ScreenCaptureKit captured it (host clock)
        let seq: UInt64
    }

    /// Complete frames kept for the recorder to choose from (newest last). ScreenCaptureKit lends at most
    /// `queueDepth` (8) pictures at a time; these, the last one written and the ones the encoder still holds have to
    /// fit in that (2026-10-27 review: with 4 the worst case was exactly 8).
    static let keep = 3

    let videoQueue = DispatchQueue(label: "rec-cue.capture", qos: .userInteractive)
    let audioQueue: DispatchQueue
    /// 0 = the Mac's sound, 1 = the microphone. Called on `audioQueue`.
    var onAudio: ((CMSampleBuffer, Int) -> Void)?
    var onStop: ((Error) -> Void)?
    /// Every complete frame with its own time stamp, on `videoQueue` (the sync test looks at them).
    var onFrame: ((CVPixelBuffer, CMTime) -> Void)?

    private var stream: SCStream?
    private var dead = false   // the stream stopped by itself (didStopWithError)
    private let lock = NSLock()
    private var counts = Counts()
    private var latest: Frame?
    private var recent: [Frame] = []
    private var seq: UInt64 = 0
    private var lastStatus: SCFrameStatus?

    init(audioQueue: DispatchQueue) {
        self.audioQueue = audioQueue
    }

    func start(filter: SCContentFilter, config: SCStreamConfiguration, system: Bool, mic: Bool) async throws {
        let s = SCStream(filter: filter, configuration: config, delegate: self)
        try s.addStreamOutput(self, type: .screen, sampleHandlerQueue: videoQueue)
        if system { try s.addStreamOutput(self, type: .audio, sampleHandlerQueue: audioQueue) }
        if mic { try s.addStreamOutput(self, type: .microphone, sampleHandlerQueue: audioQueue) }
        stream = s   // before starting, so a half-started stream can still be stopped
        try await s.startCapture()
    }

    /// Stops the stream (not one that already stopped by itself) and stops waiting after `timeout` seconds: the file
    /// is finished either way.
    func stop(timeout: Double = 3) async -> Error? {
        guard let s = stream else { return nil }
        stream = nil
        if lock.withLock({ dead }) { return nil }
        let box = Unchecked(s)
        return await withCheckedContinuation { (cont: CheckedContinuation<Error?, Never>) in
            let once = Once()
            Task {
                var err: Error?
                do { try await box.value.stopCapture() } catch { err = error }
                if once.claim() { cont.resume(returning: err) }
            }
            DispatchQueue.global().asyncAfter(deadline: .now() + timeout) {
                if once.claim() { cont.resume(returning: RecError("stopCapture did not return in \(timeout) s")) }
            }
        }
    }

    func latestFrame() -> Frame? {
        lock.lock()
        defer { lock.unlock() }
        return latest
    }

    /// The last `keep` complete frames, oldest first.
    func recentFrames() -> [Frame] {
        lock.lock()
        defer { lock.unlock() }
        return recent
    }

    /// ScreenCaptureKit's last frame said the screen does not come through (blank, suspended, stopped). A screen
    /// that sits still sends no frames at all, so silence alone does not count.
    var screenLost: Bool {
        lock.withLock { lastStatus == .blank || lastStatus == .suspended || lastStatus == .stopped }
    }

    func snapshot() -> Counts {
        lock.lock()
        defer { lock.unlock() }
        return counts
    }

    func stream(_ stream: SCStream, didOutputSampleBuffer sb: CMSampleBuffer, of type: SCStreamOutputType) {
        switch type {
        case .screen: frame(sb)
        case .audio: onAudio?(sb, 0)
        case .microphone: onAudio?(sb, 1)
        @unknown default: break
        }
    }

    func stream(_ stream: SCStream, didStopWithError error: Error) {
        lock.withLock { dead = true }
        onStop?(error)
    }

    private func frame(_ sb: CMSampleBuffer) {
        let atts = CMSampleBufferGetSampleAttachmentsArray(sb, createIfNecessary: false) as? [[SCStreamFrameInfo: Any]]
        let raw = atts?.first?[.status] as? Int
        var complete: Frame?
        lock.lock()
        if let raw, let status = SCFrameStatus(rawValue: raw) {
            lastStatus = status
            switch status {
            case .complete:
                if let pb = CMSampleBufferGetImageBuffer(sb) {
                    counts.complete += 1
                    seq += 1
                    let f = Frame(buffer: pb, time: CMSampleBufferGetPresentationTimeStamp(sb), seq: seq)
                    latest = f
                    recent.append(f)
                    if recent.count > Self.keep { recent.removeFirst(recent.count - Self.keep) }
                    complete = f
                } else {
                    counts.noImage += 1
                }
            case .idle: counts.idle += 1
            case .blank: counts.blank += 1
            case .suspended: counts.suspended += 1
            case .started: counts.started += 1
            case .stopped: counts.stopped += 1
            @unknown default: counts.other += 1
            }
        } else {
            counts.other += 1
        }
        lock.unlock()
        if let complete { onFrame?(complete.buffer, complete.time) }
    }

    static func countsJson(_ c: Counts) -> J {
        .obj([("complete", .i(c.complete)), ("idle", .i(c.idle)), ("blank", .i(c.blank)),
              ("suspended", .i(c.suspended)), ("started", .i(c.started)), ("stopped", .i(c.stopped)),
              ("no_image", .i(c.noImage)), ("other", .i(c.other))])
    }
}
