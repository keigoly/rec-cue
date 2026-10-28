import AVFoundation
import ScreenCaptureKit

/// 音量を確認 (the Windows version's level check): the microphone and the Mac's sound are opened as for a recording
/// and mixed for the meters, but nothing is written. ScreenCaptureKit wants a picture too: the smallest one, once a
/// second, dropped. While it runs macOS shows its screen recording indicator.
@MainActor
final class LevelCheck {
    struct Options {
        var mic = "auto"
        var system = "auto"
        var micGainDb = 0.0
        var systemGainDb = 0.0
        var monitor = 0
        var ownOnly = false   // tests: only Rec Cue!'s own sound, no other app's
    }

    let o: Options
    private let audioQueue = DispatchQueue(label: "rec-cue.check", qos: .userInitiated)
    private var capture: Capture?
    private var mixer: AudioMixer?
    private var timer: DispatchSourceTimer?
    private var stopped = false
    private var running = false   // the stream has started (a stop while it starts is done when it has)
    /// What was opened (for the log and the window): the microphone's name, and whether the Mac's sound is on.
    private(set) var micName: String?
    private(set) var system = false

    init(_ o: Options) {
        self.o = o
    }

    func start() async throws {
        let sw = Stopwatch()
        guard CGPreflightScreenCaptureAccess() else { throw RecError("no screen recording permission") }
        let displays = Devices.displays()
        guard !displays.isEmpty else { throw RecError("no screen") }
        let disp = displays[Devices.screenIndex(o.monitor, of: displays.count)]
        let content = try await SCShareableContent.excludingDesktopWindows(false, onScreenWindowsOnly: true)
        guard let display = content.displays.first(where: { $0.displayID == disp.id }) else {
            throw RecError("ScreenCaptureKit does not list screen \(disp.id)")
        }
        let me = content.applications.filter { $0.processID == getpid() }
        let filter = o.ownOnly ? SCContentFilter(display: display, including: me, exceptingWindows: [])
                               : SCContentFilter(display: display, excludingApplications: me, exceptingWindows: [])
        let cfg = SCStreamConfiguration()
        cfg.width = 2
        cfg.height = 2
        cfg.minimumFrameInterval = CMTime(value: 1, timescale: 1)
        cfg.queueDepth = 3
        system = o.system.lowercased() != "none"
        cfg.capturesAudio = system
        cfg.sampleRate = Int(AudioMixer.rate)
        cfg.channelCount = 2
        cfg.excludesCurrentProcessAudio = !o.ownOnly
        var micDev: AVCaptureDevice?
        if o.mic.lowercased() != "none", let d = Devices.microphone(o.mic) {
            if AVCaptureDevice.authorizationStatus(for: .audio) == .notDetermined {
                _ = await AVCaptureDevice.requestAccess(for: .audio)
            }
            if AVCaptureDevice.authorizationStatus(for: .audio) == .authorized {
                micDev = d
                cfg.captureMicrophone = true
                cfg.microphoneCaptureDeviceID = d.uniqueID
            }
        }
        micName = micDev?.localizedName
        // the same mixer as a recording (gains, formats, the meters' peaks), with the mixed sound dropped
        var sources: [AudioMixer.Source] = []
        var map = [-1, -1]
        if system {
            map[0] = sources.count
            sources.append(.init(kind: "system", name: "all apps"))
            sources[sources.count - 1].gain = Float(pow(10, o.systemGainDb / 20))
        }
        if let d = micDev {
            map[1] = sources.count
            sources.append(.init(kind: "mic", name: d.localizedName))
            sources[sources.count - 1].gain = Float(pow(10, o.micGainDb / 20))
        }
        let mixer = AudioMixer(sources: sources, latencyMs: 60)
        mixer.meterOnly = true
        self.mixer = mixer
        let cap = Capture(audioQueue: audioQueue)
        cap.onAudio = { sb, k in
            if map[k] >= 0 { mixer.push(sb, source: map[k]) }
        }
        capture = cap
        audioQueue.sync { mixer.start(at: Recorder.hostNow()) }
        try await cap.start(filter: filter, config: cfg, system: system, mic: micDev != nil)
        if stopped {   // stopped while it was starting
            capture = nil
            _ = await cap.stop()
            Log.event("check.start", ok: true, ms: sw.ms, msg: "stopped while starting")
            return
        }
        running = true
        let t = DispatchSource.makeTimerSource(queue: audioQueue)
        t.schedule(deadline: .now() + 0.05, repeating: 0.05, leeway: .milliseconds(10))
        t.setEventHandler { _ = mixer.pump(now: Recorder.hostNow()) }
        t.resume()
        timer = t
        Log.event("check.start", ok: true, ms: sw.ms, msg: "system=\(system) mic=\(micName ?? "none")")
    }

    /// For the window's meters: per source, the loudest since the last call and the device's name.
    func levels() -> [AudioMixer.Level] {
        guard let mixer else { return [] }
        return audioQueue.sync { mixer.levels() }
    }

    func stop() async {
        guard !stopped else { return }
        stopped = true
        timer?.cancel()
        timer = nil
        let sources = mixer.map { m in audioQueue.sync { m.sources.map { $0.json() } } } ?? []
        if running, let c = capture {
            capture = nil
            _ = await c.stop()
        }
        Log.line("check.stop", [("sources", .arr(sources))])
    }
}
