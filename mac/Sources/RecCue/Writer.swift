import AVFoundation

/// AVAssetWriter: H.264 (VideoToolbox) + AAC in an MP4 written in fragments, so a recording that ends in a crash
/// still plays up to the last fragment (the Windows version's fmp4). Video is appended from the tick queue and audio
/// from the audio queue; each side keeps its own counters.
final class Writer {
    let url: URL
    private let writer: AVAssetWriter
    private let videoIn: AVAssetWriterInput?
    private let adaptor: AVAssetWriterInputPixelBufferAdaptor?
    private let audioIn: AVAssetWriterInput

    // tick queue
    private(set) var videoSamples = 0, videoDrops = 0, videoErrors = 0
    private(set) var maxVideoWriteMs = 0.0
    // audio queue
    private(set) var audioBuffers = 0, audioWaits = 0, audioErrors = 0
    private(set) var audioFrames: Int64 = 0

    init(url: URL, width: Int, height: Int, fps: Int, videoKbps: Int, audioKbps: Int, fragmentSeconds: Double,
         video: Bool) throws {
        self.url = url
        writer = try AVAssetWriter(outputURL: url, fileType: video ? .mp4 : .m4a)
        writer.shouldOptimizeForNetworkUse = false
        writer.movieFragmentInterval = CMTime(seconds: fragmentSeconds, preferredTimescale: 600)
        // the file's clocks (the movie's and the video track's) in whole sound samples, as the end is fixed: at the
        // default 1/600 s an end less than half a unit after the last frame's start was rounded down onto it, and that
        // frame fell out of the file (`complete` false, 2 recordings in 40)
        writer.movieTimeScale = CMTimeScale(AudioMixer.rate)
        if video {
            let settings: [String: Any] = [
                AVVideoCodecKey: AVVideoCodecType.h264,
                AVVideoWidthKey: width,
                AVVideoHeightKey: height,
                AVVideoColorPropertiesKey: [
                    AVVideoColorPrimariesKey: AVVideoColorPrimaries_ITU_R_709_2,
                    AVVideoTransferFunctionKey: AVVideoTransferFunction_ITU_R_709_2,
                    AVVideoYCbCrMatrixKey: AVVideoYCbCrMatrix_ITU_R_709_2,
                ],
                AVVideoCompressionPropertiesKey: [
                    AVVideoAverageBitRateKey: videoKbps * 1000,
                    AVVideoExpectedSourceFrameRateKey: fps,
                    AVVideoMaxKeyFrameIntervalKey: fps * 2,
                    AVVideoProfileLevelKey: AVVideoProfileLevelH264HighAutoLevel,
                    AVVideoAllowFrameReorderingKey: false,
                ],
            ]
            let v = AVAssetWriterInput(mediaType: .video, outputSettings: settings)
            v.expectsMediaDataInRealTime = true
            v.mediaTimeScale = CMTimeScale(AudioMixer.rate)
            guard writer.canAdd(v) else { throw RecError("the writer does not take the video settings") }
            writer.add(v)
            videoIn = v
            adaptor = AVAssetWriterInputPixelBufferAdaptor(assetWriterInput: v, sourcePixelBufferAttributes: nil)
        } else {
            videoIn = nil
            adaptor = nil
        }
        let a = AVAssetWriterInput(mediaType: .audio, outputSettings: [
            AVFormatIDKey: kAudioFormatMPEG4AAC,
            AVSampleRateKey: 48000,
            AVNumberOfChannelsKey: 2,
            AVEncoderBitRateKey: audioKbps * 1000,
        ])
        a.expectsMediaDataInRealTime = true
        guard writer.canAdd(a) else { throw RecError("the writer does not take the audio settings") }
        writer.add(a)
        audioIn = a
    }

    /// Gets the encoders ready (tens of ms), before the clock of the recording starts.
    func begin() throws {
        guard writer.startWriting() else { throw RecError("startWriting: \(errorText)") }
    }

    func startSession(at t: CMTime) {
        writer.startSession(atSourceTime: t)
    }

    /// A start that failed after begin(): give up the file.
    func cancel() {
        if writer.status == .writing { writer.cancelWriting() }
        try? FileManager.default.removeItem(at: url)
    }

    var errorText: String { writer.error.map { "\($0.localizedDescription) (\(($0 as NSError).code))" } ?? "" }
    var failed: Bool { writer.status == .failed }

    /// Tick queue.
    func appendVideo(_ pb: CVPixelBuffer, at t: CMTime) {
        guard let videoIn, let adaptor, writer.status == .writing else {
            videoErrors += 1
            return
        }
        guard videoIn.isReadyForMoreMediaData else {
            videoDrops += 1
            return
        }
        let sw = Stopwatch()
        if adaptor.append(pb, withPresentationTime: t) {
            videoSamples += 1
        } else {
            videoErrors += 1
        }
        maxVideoWriteMs = max(maxVideoWriteMs, sw.ms)
    }

    /// Audio queue. False when the input is not ready: the caller keeps the buffer and tries again. Sound must not be
    /// skipped — AAC is written back to back, so a hole in the time stamps would move everything after it earlier.
    func appendAudio(_ sb: CMSampleBuffer) -> Bool {
        guard writer.status == .writing else {
            audioErrors += 1
            return true
        }
        guard audioIn.isReadyForMoreMediaData else {
            audioWaits += 1
            return false
        }
        if audioIn.append(sb) {
            audioBuffers += 1
            audioFrames += Int64(CMSampleBufferGetNumSamples(sb))
        } else {
            audioErrors += 1
        }
        return true
    }

    func finish(at end: CMTime) async -> (ok: Bool, ms: Double, error: String) {
        let sw = Stopwatch()
        guard writer.status == .writing else { return (false, 0, "status \(writer.status.rawValue) \(errorText)") }
        videoIn?.markAsFinished()
        audioIn.markAsFinished()
        writer.endSession(atSourceTime: end)
        await writer.finishWriting()
        return (writer.status == .completed, sw.ms, errorText)
    }
}

struct RecError: Error, CustomStringConvertible {
    let description: String
    init(_ s: String) { description = s }
}
