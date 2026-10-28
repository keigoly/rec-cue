import AppKit
import AVFoundation
import ScreenCaptureKit

struct RecordOptions {
    var seconds = 10.0         // 0: until stop (the app's ■)
    var fps = 15
    var videoKbps = 2000
    var audioKbps = 128
    var monitor = 0            // 0 = the main screen
    var cursor = true
    var system = "auto"        // auto / none
    var mic = "none"           // none / auto / default / part of a device name
    var testPattern = false
    var syncTest = false       // test: flash the test window and beep at planned moments (needs testPattern)
    var noBeep = false         // sync test without the beeps: flashes only (pictures against the capture)
    var stillPattern = false   // the test window does not move (it changes only when it flashes)
    var out: URL?
    var crashAt: Double?       // test: kill the process this many seconds in (does the file survive?)
    var termAt: Double?        // test: send ourselves SIGTERM this many seconds in (does it stop the normal way?)
    var pauseAt: Double?       // test: pause this many seconds in ...
    var pauseFor = 2.0         // ... for this long
    var endSnapMs: Double?     // test: the file ends this many ms after a tick (its last frame lasts only that long)
    var screenLostAt: Double?  // test: the screen "does not come through" from this many seconds into the file ...
    var screenLostFor = 2.0    // ... for this long (the window's warning)
    var startGapMs: Double?    // test: a wait between fixing t0 and starting the pauses (the window reads the time)
    var audioOnly = false     // "sound only": no picture, the Mac's sound and the microphone into M4A or WAV
    var audioFormat = "m4a"    // m4a / wav (macOS has no MP3 encoder)
    var latencyMs = 150
    var fragmentSeconds = 2.0
    var outDir = Paths.defaultOutDir
    var namePrefix = "Rec Cue"
    var micGainDb = 0.0
    var systemGainDb = 0.0
    var config: J = .null           // what config.ini said, for the log
    var configProblems: [String] = []
}

extension RecordOptions {
    /// The settings from config.ini; the command line then changes what it names.
    init(config c: Config, problems: [String]) {
        self.init()
        fps = c.fps
        videoKbps = c.videoKbps
        audioKbps = c.audioKbps
        audioFormat = c.audioFormat
        monitor = c.monitor
        cursor = c.cursor
        mic = c.mic
        system = c.system
        micGainDb = c.micGainDb
        systemGainDb = c.systemGainDb
        latencyMs = c.latencyMs
        outDir = c.resolvedOutDir
        namePrefix = c.namePrefix
        config = c.json()
        configProblems = problems
    }
}

/// One recording. ScreenCaptureKit keeps the newest frame; a tick at `fps` writes it (the same picture again when the
/// screen did not change, like the Windows version) with the tick's time. Sound goes through the AudioMixer onto the
/// same host clock. A stats line every 10 s, sessions/<sid>.json at the end, then the file is read back.
final class Recorder {
    let o: RecordOptions
    let sid = Log.newSessionId()
    private let tickQueue = DispatchQueue(label: "rec-cue.tick", qos: .userInteractive)
    private let audioQueue = DispatchQueue(label: "rec-cue.audio", qos: .userInteractive)
    private let statsQueue = DispatchQueue(label: "rec-cue.stats", qos: .utility)
    private var capture: Capture?
    private var writer: Writer?     // MP4 (picture and sound) or M4A (sound only)
    private var sound: AudioOut?    // where the mixed sound goes: the writer above, or a WAV file
    private var mixer: AudioMixer?
    private var timers: [DispatchSourceTimer] = []
    private var tickTimer: DispatchSourceTimer?   // tick queue (set again after each pause)
    private var pattern: TestPattern?
    private var sync: SyncTest?
    let pauses = Pauses()
    /// Called once the recording has started (any thread): the app turns its buttons red.
    var onStarted: (() -> Void)?
    /// Called as the recording begins to end, whatever ended it (any thread): the app shows 保存中 at once.
    var onStopping: (() -> Void)?
    private var t0 = CMTime.invalid
    private var path: URL?
    private var width = 0, height = 0
    private var displayName = ""
    private var startedIso = ""

    // tick queue
    private var ticks = 0, newFrames = 0, unchanged = 0, lateTicks = 0, caughtUp = 0, noFrame = 0, pickN = 0
    private var lastK = -1
    private var lastWritten: Capture.Frame?
    private var maxLateMs = 0.0, pickSum = 0.0, pickMin = 0.0, pickMax = 0.0
    private var minLateMs = Double.infinity   // - = a tick written before its time (`delay` after its place)
    private var delay = 0.1    // seconds: tick k is written this long after its place on the timeline
    private var tickLimit = Int.max   // the last tick of the file, once stop() has fixed the end
    // audio queue
    private var audioEnd: CMTime?     // the end of the file, once stop() has fixed it
    private var audioBacklog: [CMSampleBuffer] = []   // mixed sound the writer was not ready for yet
    private var backlogMax = 0
    // stats queue
    private var lastCpu = 0.0, lastWall = 0.0, cpuSum = 0.0, footPeak = 0.0
    private var cpuN = 0
    private var lastSystem: J = .obj([])

    private var activity: NSObjectProtocol?      // keeps the Mac awake and the app out of App Nap while recording
    private var observers: [(NotificationCenter, NSObjectProtocol)] = []
    // any thread
    private let stopLock = NSLock()
    private var stopBy: String?    // why it ended before its time: user / signal / stream / writer / disk
    private var stopDetail = ""
    private let shotsLock = NSLock()
    private var shots: [J] = []    // screenshots taken while it ran (sessions/<sid>.json)
    private let sysLock = NSLock()
    private var screenLocked = false, sessionInactive = false   // the screen locked / another user in front
    /// Why the recording goes on without the microphone it was told to use (nil: it has it, or none was wanted).
    private(set) var micProblem: String?

    init(_ o: RecordOptions) {
        self.o = o
        Self.liveLock.withLock { Self.liveCount += 1 }
    }

    deinit {
        let left = Self.liveLock.withLock { () -> Int in
            Self.liveCount -= 1
            return Self.liveCount
        }
        Log.event("rec.freed", ok: true, msg: "\(sid): \(left) left")
    }

    /// Recorders alive in this process. A finished one should go: it holds its last frames and its sound (tens of MB
    /// at Retina sizes). Any thread.
    static var live: Int { liveLock.withLock { liveCount } }
    private static let liveLock = NSLock()
    private static var liveCount = 0

    static func hostNow() -> CMTime { CMClockGetTime(CMClockGetHostTimeClock()) }

    /// Ends the recording early, the normal way (the file is finished). Any thread; the first reason counts.
    func requestStop(by: String, _ detail: String) {
        let first = stopLock.withLock { () -> Bool in
            guard stopBy == nil else { return false }
            stopBy = by
            stopDetail = detail
            return true
        }
        // ■ (app), the menu bar's stop (user) and Ctrl-C / kill (signal) are ordinary ends; stream / writer / disk are not
        if first { Log.event("rec.stop_requested", ok: ["app", "user", "signal"].contains(by), msg: "\(by): \(detail)") }
    }

    /// Where the file is now, in seconds (paused stretches left out): the time the window shows.
    var fileSeconds: Double {
        guard t0.isValid else { return 0 }
        return Double(pauses.filePosition(at: pauses.position(at: Self.hostNow()))) / Double(Pauses.rate)
    }

    /// For the window's meters: per source, the loudest since the last call, the device's name, the run of silence.
    func levels() -> [AudioMixer.Level] {
        audioQueue.sync { mixer?.levels() ?? [] }
    }

    /// The file being written.
    var file: URL? { path }

    /// The screen comes through (the window warns when not): not while it is locked or another user's session is in
    /// front, nor when ScreenCaptureKit says its frames are blank or suspended.
    var videoCapturing: Bool {
        let away = sysLock.withLock { screenLocked || sessionInactive }
        if let a = o.screenLostAt, fileSeconds >= a, fileSeconds < a + o.screenLostFor { return false }   // test
        return !away && !(capture?.screenLost ?? false)
    }

    /// A screenshot taken while this recording ran, for its record (any thread): the PNG, and when in the file.
    func noteShot(_ png: String, at: Double) {
        shotsLock.withLock { shots.append(.obj([("path", .str(png)), ("at_s", .n(at))])) }
    }

    /// ❚❚ (any thread): from now until resume() nothing goes into the file; the picture and the sound both skip it.
    func pause() {
        guard t0.isValid else { return }
        let at = pauses.position(at: Self.hostNow())
        guard pauses.pause(at: at) else { return }
        Log.event("session.pause", ok: true, msg: String(format: "#%d at %.2f s", pauses.count,
                                                          Double(pauses.filePosition(at: at)) / Double(Pauses.rate)))
    }

    /// ▶ (any thread).
    func resume() {
        guard t0.isValid, let frames = pauses.resume(at: pauses.position(at: Self.hostNow())) else { return }
        Log.event("session.resume", ok: true, ms: Double(frames) * 1000 / Double(Pauses.rate), msg: "#\(pauses.count)")
        realignTicks()
    }

    /// The ticks keep their places in the file, so a pause moves their moments on the capture clock by its length: the
    /// tick timer is set again to fire `delay` after the next tick's moment. On its old beat it wrote ticks up to half
    /// a frame early (−33 ms at 15 fps after a pause of 2.03 s), and at low frame rates before their moment, past the
    /// end of the file (2026-10-28).
    private func realignTicks() {
        tickQueue.async { [self] in
            guard let timer = tickTimer, let c = captureTime(ofTick: lastK + 1) else { return }
            let firstIn = CMTimeSubtract(CMTimeAdd(c, CMTime(seconds: delay, preferredTimescale: 1_000_000_000)),
                                         Self.hostNow())
            timer.schedule(deadline: .now() + max(0, firstIn.seconds), repeating: .nanoseconds(1_000_000_000 / o.fps),
                           leeway: .milliseconds(1))
        }
    }

    func run() async -> J {
        Log.setSession(sid)
        startedIso = Log.nowIso()
        Log.event("rec.begin", ok: true, msg: "fps=\(o.fps) video_kbps=\(o.videoKbps) system=\(o.system) mic=\(o.mic) "
                  + "test_pattern=\(o.testPattern) seconds=\(o.seconds)")
        Log.line("config", [("ok", .bool(o.configProblems.isEmpty)), ("values", o.config),
                            ("problems", .arr(o.configProblems.map { .str($0) }))])
        do {
            try await start()
        } catch {
            Log.event("rec.start", ok: false, msg: "\(error)")
            await abandon()
            return .obj([("ok", .bool(false)), ("sid", .str(sid)), ("error", .str("\(error)"))])
        }
        // stopped while it was starting (the menu bar's 停止して保存, quitting): nothing is recorded yet — no file and
        // no failure (a file of a few ms, one frame and no sound, was left and said to have failed)
        if let by = stopLock.withLock({ stopBy }) {
            Log.event("rec.cancel", ok: true, msg: "stopped while starting (\(by)): no file")
            await abandon()
            return .obj([("ok", .bool(true)), ("sid", .str(sid)), ("cancelled", .bool(true)), ("ended_by", .str(by))])
        }
        if let c = o.crashAt {
            DispatchQueue.global().asyncAfter(deadline: .now() + c) {
                Log.event("test.crash", ok: true, msg: "SIGKILL at \(c) s (--crash-at)")
                kill(getpid(), SIGKILL)
            }
        }
        if let t = o.termAt {
            DispatchQueue.global().asyncAfter(deadline: .now() + t) { kill(getpid(), SIGTERM) }
        }
        if let p = o.pauseAt {
            let me = Unchecked(self)
            DispatchQueue.global().asyncAfter(deadline: .now() + p) { me.value.pause() }
            DispatchQueue.global().asyncAfter(deadline: .now() + p + o.pauseFor) { me.value.resume() }
        }
        onStarted?()
        let until = o.seconds > 0 ? DispatchTime.now().uptimeNanoseconds + UInt64(o.seconds * 1e9) : UInt64.max
        while DispatchTime.now().uptimeNanoseconds < until && stopLock.withLock({ stopBy }) == nil {
            try? await Task.sleep(nanoseconds: 50_000_000)
        }
        return await stop()
    }

    private func start() async throws {
        let sw = Stopwatch()
        guard CGPreflightScreenCaptureAccess() else {
            throw RecError("no screen recording permission (System Settings > Privacy & Security > "
                           + "Screen & System Audio Recording)")
        }
        // No idle sleep, no App Nap (timers on a background app can be slowed down) until stop(). Recording the screen,
        // the display stays on too: a meeting followed without touching the Mac went dark in the file. Sound only
        // lets the display sleep.
        var keep: ProcessInfo.ActivityOptions = [.userInitiated, .latencyCritical]
        if !o.audioOnly { keep.insert(.idleDisplaySleepDisabled) }
        activity = ProcessInfo.processInfo.beginActivity(options: keep,
                                                         reason: o.audioOnly ? "Recording sound" : "Recording the screen")
        watchSystem()
        let displays = await MainActor.run { Devices.displays() }   // NSScreen belongs to the main thread
        guard !displays.isEmpty else { throw RecError("no screen") }
        let pick = Devices.screenIndex(o.monitor, of: displays.count)
        if pick != o.monitor {
            Log.event("rec.screen", ok: false, msg: "monitor=\(o.monitor) is not there (\(displays.count) screens): the main screen")
        }
        let disp = displays[pick]
        displayName = disp.name
        if o.testPattern {
            pattern = await MainActor.run {
                let screen = NSScreen.screens.first {
                    ($0.deviceDescription[NSDeviceDescriptionKey("NSScreenNumber")] as? NSNumber)?.uint32Value == disp.id
                }
                let p = TestPattern(on: screen ?? NSScreen.screens[0], animated: !o.stillPattern)
                p.show()
                return p
            }
            try await Task.sleep(nanoseconds: 300_000_000)   // let the window reach the screen
        }
        let content = try await SCShareableContent.excludingDesktopWindows(false, onScreenWindowsOnly: true)
        guard let display = content.displays.first(where: { $0.displayID == disp.id }) else {
            throw RecError("ScreenCaptureKit does not list screen \(disp.id)")
        }
        let me = content.applications.filter { $0.processID == getpid() }
        let filter: SCContentFilter
        if o.testPattern {
            guard !me.isEmpty else { throw RecError("our own window is not listed by ScreenCaptureKit") }
            filter = SCContentFilter(display: display, including: me, exceptingWindows: [])
        } else {
            filter = SCContentFilter(display: display, excludingApplications: me, exceptingWindows: [])
        }
        let scale = CGFloat(filter.pointPixelScale)
        width = Int((filter.contentRect.width * scale).rounded()) & ~1
        height = Int((filter.contentRect.height * scale).rounded()) & ~1

        let cfg = SCStreamConfiguration()
        if o.audioOnly {   // ScreenCaptureKit wants a picture too: the smallest one, once a second, and it is dropped
            width = 2
            height = 2
        }
        cfg.width = width
        cfg.height = height
        cfg.minimumFrameInterval = o.audioOnly ? CMTime(value: 1, timescale: 1)
                                               : CMTime(value: 1, timescale: CMTimeScale(o.fps))
        cfg.pixelFormat = kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange
        cfg.colorSpaceName = CGColorSpace.sRGB
        cfg.colorMatrix = kCVImageBufferYCbCrMatrix_ITU_R_709_2
        cfg.queueDepth = 8   // the most it lends: Capture.keep frames are held, plus what the encoder holds
        cfg.showsCursor = o.cursor
        let system = o.system.lowercased() != "none"
        cfg.capturesAudio = system
        cfg.sampleRate = Int(AudioMixer.rate)
        cfg.channelCount = 2
        cfg.excludesCurrentProcessAudio = !o.testPattern
        // no microphone (none found, or no permission): it records without one, as the Windows version, and the window
        // says so (マイクなし) — a missing microphone does not cost the recording
        var micDev: AVCaptureDevice?
        if o.mic.lowercased() != "none" {
            if let d = Devices.microphone(o.mic) {
                if AVCaptureDevice.authorizationStatus(for: .audio) == .notDetermined {
                    _ = await AVCaptureDevice.requestAccess(for: .audio)
                }
                if AVCaptureDevice.authorizationStatus(for: .audio) == .authorized {
                    micDev = d
                    cfg.captureMicrophone = true
                    cfg.microphoneCaptureDeviceID = d.uniqueID
                } else {
                    micProblem = "permission"
                }
            } else {
                micProblem = "not found"
            }
            if let p = micProblem { Log.event("rec.mic", ok: false, msg: "\(o.mic): \(p) — recording without a microphone") }
        }

        // mixer sources, by stream output (0 = the Mac's sound, 1 = the microphone)
        var sources: [AudioMixer.Source] = []
        var map = [-1, -1]
        if system {
            map[0] = sources.count
            sources.append(.init(kind: "system", name: o.testPattern ? "Rec Cue! only" : "all apps"))
            sources[sources.count - 1].gain = Float(pow(10, o.systemGainDb / 20))
        }
        if let d = micDev {
            map[1] = sources.count
            sources.append(.init(kind: "mic", name: d.localizedName))
            sources[sources.count - 1].gain = Float(pow(10, o.micGainDb / 20))
        }
        let mixer = AudioMixer(sources: sources, latencyMs: o.latencyMs)
        self.mixer = mixer

        let ext = o.audioOnly ? (o.audioFormat == "wav" ? "wav" : "m4a") : "mp4"
        let url = o.out ?? o.outDir.appendingPathComponent("\(o.namePrefix) \(Paths.stamp()).\(ext)")
        guard Paths.ensure(url.deletingLastPathComponent()) else {
            throw RecError("cannot make the folder \(url.deletingLastPathComponent().path)")
        }
        guard !FileManager.default.fileExists(atPath: url.path) else { throw RecError("already exists: \(url.path)") }
        path = url
        if ext == "wav" {
            sound = try WavWriter(url: url)
        } else {
            let w = try Writer(url: url, width: width, height: height, fps: o.fps, videoKbps: o.videoKbps,
                               audioKbps: o.audioKbps, fragmentSeconds: o.fragmentSeconds, video: !o.audioOnly)
            writer = w
            sound = w
            try w.begin()   // before the clock starts: getting the encoders ready takes 20–130 ms
        }

        let cap = Capture(audioQueue: audioQueue)
        cap.onAudio = { [weak self] sb, k in
            guard let self, let mixer = self.mixer, map[k] >= 0 else { return }
            mixer.push(sb, source: map[k])
        }
        cap.onStop = { [weak self] error in
            // the menu bar's stop button, the system, a screen that went away: end the file there
            let e = error as NSError
            Log.event("capture.stopped", ok: false, msg: "\(e.localizedDescription) (\(e.domain) \(e.code))")
            if e.domain == SCStreamErrorDomain && e.code == SCStreamError.Code.userStopped.rawValue {
                self?.requestStop(by: "user", "stopped from the menu bar")
            } else {
                self?.requestStop(by: "stream", "\(e.localizedDescription) (\(e.code))")
            }
        }
        capture = cap
        if o.syncTest {
            guard o.testPattern, system || o.noBeep else {
                throw RecError("--sync-test needs --test-pattern, and --system auto unless --no-beep")
            }
            let probe = await MainActor.run { [pattern] in pattern?.probeRect ?? .zero }
            let s = try SyncTest(probe: probe, beep: !o.noBeep)   // the beeper starts before the capture does
            sync = s
            cap.onFrame = { pb, t in s.frame(pb, at: t) }
            let sys = map[0]
            mixer.probe = { k, pcm, at in if k == sys { s.sound(pcm, at: at) } }
        }
        try await cap.start(filter: filter, config: cfg, system: system, mic: micDev != nil)
        Log.event("capture.start", ok: true, ms: sw.ms,
                  msg: "\(width)x\(height) \(disp.name) scale=\(scale) system=\(system) mic=\(micDev?.localizedName ?? "none")")

        if !o.audioOnly {   // the first tick needs a picture
            let wait = Stopwatch()
            while cap.latestFrame() == nil && wait.ms < 3000 { try await Task.sleep(nanoseconds: 10_000_000) }
            guard cap.latestFrame() != nil else { throw RecError("no frame from ScreenCaptureKit in 3 s") }
            Log.event("capture.first_frame", ok: true, ms: wait.ms)
        }

        // One clock for both tracks, on 1/48000 s so the audio lands on whole samples. The mixer takes sound from t0
        // on right away (sound that came before it started was lost before, 40–120 ms at the start of each file).
        t0 = CMTimeConvertScale(Self.hostNow(), timescale: CMTimeScale(AudioMixer.rate), method: .roundHalfAwayFromZero)
        let start = t0
        if let g = o.startGapMs { try await Task.sleep(nanoseconds: UInt64(g * 1e6)) }   // test
        pauses.start(at: start)
        audioQueue.sync { mixer.start(at: start) }
        writer?.startSession(at: t0)
        Log.event("rec.start", ok: true, ms: sw.ms, msg: url.lastPathComponent)
        lastCpu = Proc.cpuSeconds()
        lastWall = ProcessInfo.processInfo.systemUptime
        // Tick k is written `delay` after its place on the timeline, with the kept frame captured nearest to that
        // place: ScreenCaptureKit sometimes hands a frame over tens of ms after it captured it, and writing "the
        // newest frame now" put such frames one or two ticks late (2026-10-27 sync test). The kept frames reach back
        // keep - 1 frames, which bounds the delay at high frame rates.
        delay = min(0.1, Double(Capture.keep - 1) / Double(o.fps))
        let firstIn = CMTimeSubtract(CMTimeAdd(t0, CMTime(seconds: delay, preferredTimescale: 1_000_000_000)),
                                     Self.hostNow())
        if !o.audioOnly {
            let t = timer(on: tickQueue, every: .nanoseconds(1_000_000_000 / o.fps), leeway: .milliseconds(1),
                          first: .now() + max(0, firstIn.seconds)) { [weak self] in self?.onTick() }
            tickQueue.sync { tickTimer = t }
            timers.append(t)
        }
        timers.append(timer(on: audioQueue, every: .milliseconds(20), leeway: .milliseconds(2)) {
            [weak self] in self?.pumpAudio()
        })
        timers.append(timer(on: statsQueue, every: .seconds(10), leeway: .milliseconds(100), first: .now() + 10) {
            [weak self] in self?.logStats(final: false)
        })
        if let s = sync, let p = pattern {
            for t in s.plan(t0: t0, seconds: o.seconds) {
                s.beeper?.schedule(at: t)
                let wait = max(0, CMTimeSubtract(t, Self.hostNow()).seconds)
                DispatchQueue.main.asyncAfter(deadline: .now() + wait) {
                    MainActor.assumeIsolated { p.flash(SyncTest.beepSeconds) }
                }
            }
            Log.event("sync.plan", ok: true,
                      msg: "\(s.planned.count) flashes and beeps from \(SyncTest.first) s every \(SyncTest.spacing) s")
        }
    }

    private func timer(on q: DispatchQueue, every: DispatchTimeInterval, leeway: DispatchTimeInterval,
                       first: DispatchTime = .now(), _ f: @escaping () -> Void) -> DispatchSourceTimer {
        let t = DispatchSource.makeTimerSource(flags: .strict, queue: q)
        t.schedule(deadline: first, repeating: every, leeway: leeway)
        t.setEventHandler(handler: f)
        t.resume()
        return t
    }

    /// Tick queue: write every tick whose time has come (`delay` after its place, give or take half a frame). A late
    /// timer writes the ticks it missed too, so the file keeps one frame per tick.
    private func onTick() {
        let now = Self.hostNow()
        // ticks are counted on the file's timeline: while paused it stands still
        let back = pauses.position(at: now) - Int64(delay * Double(Pauses.rate))
        let fileSec = Double(pauses.filePosition(at: back)) / Double(Pauses.rate)
        let due = min(tickLimit, Int((fileSec * Double(o.fps)).rounded()))
        if due > lastK + 1 { caughtUp += due - lastK - 1 }
        while lastK < due {
            guard let c = captureTime(ofTick: lastK + 1) else { break }   // in the pause going on now
            lastK += 1
            write(lastK, capturedAt: c, now: now)
        }
        if let writer, writer.failed { requestStop(by: "writer", writer.errorText) }
    }

    /// Where on the capture clock tick k of the file is (nil: in the pause going on now).
    private func captureTime(ofTick k: Int) -> CMTime? {
        let f = Int64(k) * Pauses.rate / Int64(o.fps)
        guard let c = pauses.capturePosition(atFile: f) else { return nil }
        return CMTimeAdd(t0, CMTime(value: c, timescale: CMTimeScale(Pauses.rate)))
    }

    /// Tick queue: tick k (its place in the file), shown with the newest kept frame captured no later than half a
    /// frame after the tick's moment `c` on the capture clock (sample and hold). "The nearest frame" showed a change
    /// early when frames come sparsely (a still screen sends none): with a frame from before and one from up to
    /// `delay` after, the later one was nearer for ticks well before it was captured. A frame captured after a pause
    /// began never shows in a tick from before it.
    private func write(_ k: Int, capturedAt c: CMTime, now: CMTime?) {
        guard let cap = capture, let writer else { return }
        ticks += 1
        let at = CMTimeAdd(t0, CMTime(value: Int64(k), timescale: CMTimeScale(o.fps)))
        if let now {
            let lateMs = (CMTimeSubtract(now, c).seconds - delay) * 1000
            if lateMs > 500.0 / Double(o.fps) { lateTicks += 1 }   // more than half a frame after the plan
            maxLateMs = max(maxLateMs, lateMs)
            minLateMs = min(minLateMs, lateMs)
        }
        let kept = cap.recentFrames()   // oldest first
        var limit = CMTimeAdd(c, CMTime(value: 1, timescale: CMTimeScale(2 * o.fps)))
        if let s = pauses.nextStart(after: pauses.position(at: c)) {
            let pauseAt = CMTimeAdd(t0, CMTime(value: s - 1, timescale: CMTimeScale(Pauses.rate)))
            limit = CMTimeMinimum(limit, pauseAt)
        }
        var pick = kept.last { CMTimeCompare($0.time, limit) <= 0 }
        if pick == nil {   // all kept frames are newer (a late tick): hold the last picture rather than show one early
            pick = lastWritten ?? kept.first
        }
        // never step back to an older picture than the one already written
        if let p = pick, let w = lastWritten, p.seq < w.seq { pick = w }
        guard let f = pick else {
            noFrame += 1
            return
        }
        if f.seq != lastWritten?.seq {
            newFrames += 1
            let off = CMTimeSubtract(f.time, c).seconds * 1000   // + = captured after the tick's moment
            if pickN == 0 {
                pickMin = off
                pickMax = off
            }
            pickSum += off
            pickN += 1
            pickMin = min(pickMin, off)
            pickMax = max(pickMax, off)
        } else {
            unchanged += 1
        }
        lastWritten = f
        writer.appendVideo(f.buffer, at: at)
    }

    /// Audio queue: mix what is ready and hand it to the writer, oldest first. What the writer is not ready for waits
    /// for the next pump — nothing is skipped. Once the end is fixed, nothing past it; `flush` = everything up to it.
    private func pumpAudio(flush: Bool = false) {
        guard let mixer, let sound else { return }
        var now = Self.hostNow()
        if let end = audioEnd {
            now = flush ? end : CMTimeMinimum(now, CMTimeAdd(end, CMTime(value: Int64(o.latencyMs), timescale: 1000)))
        }
        audioBacklog += mixer.pump(now: now, flush: flush, cuts: pauses.cuts())
        while let sb = audioBacklog.first, sound.appendAudio(sb) { audioBacklog.removeFirst() }
        backlogMax = max(backlogMax, audioBacklog.count)
        if let wav = sound as? WavWriter, wav.full {
            requestStop(by: "wav_full", "4,000,000,000 bytes of sound: RIFF counts no further")
        } else if sound.failed {
            requestStop(by: "writer", sound.errorText)
        } else if audioBacklog.count > 150 {   // 3 s of 20 ms pumps that the writer did not take
            requestStop(by: "writer", "the writer has not taken sound for 3 s")
        }
    }

    /// Tick queue.
    private func videoJson() -> J {
        let c = capture?.snapshot() ?? Capture.Counts()
        return .obj([("ticks", .i(ticks)), ("new", .i(newFrames)), ("unchanged", .i(unchanged)),
                     ("written", .i(writer?.videoSamples ?? 0)), ("dropped", .i(writer?.videoDrops ?? 0)),
                     ("errors", .i(writer?.videoErrors ?? 0)), ("delay_ms", .n(delay * 1000, 0)),
                     ("late_ticks", .i(lateTicks)), ("caught_up", .i(caughtUp)), ("no_frame", .i(noFrame)),
                     ("max_late_ms", .n(maxLateMs, 0)), ("min_late_ms", .n(minLateMs.isFinite ? minLateMs : 0, 1)),
                     ("max_write_ms", .n(writer?.maxVideoWriteMs ?? 0)),
                     // when the picked frames were captured, against their place on the timeline (+ = after it)
                     ("pick_ms", .obj([("avg", .n(pickN > 0 ? pickSum / Double(pickN) : 0)), ("min", .n(pickMin)),
                                       ("max", .n(pickMax))])),
                     ("sck", Capture.countsJson(c))])
    }

    /// Audio queue.
    private func audioJson() -> J {
        guard let mixer, let writer = sound else { return .null }
        var kv: [(String, J)] = []
        if case .obj(let m) = mixer.json() { kv = m }
        kv += [("written_samples", .i(writer.audioFrames)), ("buffers", .i(writer.audioBuffers)),
               ("waits", .i(writer.audioWaits)), ("backlog", .i(audioBacklog.count)), ("backlog_max", .i(backlogMax)),
               ("errors", .i(writer.audioErrors))]
        return .obj(kv)
    }

    /// Stats queue.
    private func logStats(final: Bool) {
        let cpu = Proc.cpuSeconds(), wall = ProcessInfo.processInfo.systemUptime
        let cpuPct = wall > lastWall ? 100 * (cpu - lastCpu) / ((wall - lastWall) * Double(Proc.ncpu)) : 0
        lastCpu = cpu
        lastWall = wall
        cpuSum += cpuPct
        cpuN += 1
        let mem = Proc.memory()
        footPeak = max(footPeak, mem.footprint)
        let disk = path.map(Paths.freeMB) ?? -1
        lastSystem = .obj([("free_mb", .n(Proc.systemFreeMB(), 0)), ("disk_free_mb", .n(disk, 0))])
        if !final && disk >= 0 && disk < 500 { requestStop(by: "disk", String(format: "%.0f MB left", disk)) }
        let v = tickQueue.sync { videoJson() }
        let a = audioQueue.sync { audioJson() }
        // t: where the file is now (paused stretches left out), as in the Windows version
        let now = pauses.position(at: Self.hostNow())
        let t = t0.isValid ? Double(pauses.filePosition(at: now)) / Double(Pauses.rate) : 0
        let pausedSec = t0.isValid ? Double(pauses.cut(before: now)) / Double(Pauses.rate) : 0
        Log.line(final ? "stats.final" : "stats", [
            ("t", .n(t)), ("paused", .bool(pauses.paused)), ("paused_s", .n(pausedSec)), ("video", v), ("audio", a),
            ("proc", .obj([("footprint_mb", .n(mem.footprint, 0)), ("rss_mb", .n(mem.resident, 0)),
                           ("cpu_pct", .n(cpuPct)), ("assertions", .arr(Proc.assertions().map { .str($0) }))])),
            ("system", lastSystem), ("file_mb", .n(Double(path.map(Paths.fileBytes) ?? 0) / 1048576.0)),
        ])
    }

    private func stop() async -> J {
        let sw = Stopwatch()
        onStopping?()
        // The file ends here. Capturing goes on for the tick delay and the sound latency, so the last pictures and
        // sound still arrive and the timers write them as usual (stopping the capture at once lost the last ~30 ms
        // of sound); then whatever is left is written.
        let stopAt = CMTimeConvertScale(Self.hostNow(), timescale: CMTimeScale(AudioMixer.rate),
                                        method: .roundHalfAwayFromZero)
        // The end of the file, in whole samples on the file's timeline (paused stretches left out; stopped while
        // paused, the file ends where the pause began). The last tick is strictly before it: with seconds × fps, an
        // end that fell exactly on a tick (60 fps: every 800 samples) kept a frame that endSession then cut off.
        var endFrames = pauses.filePosition(at: pauses.position(at: stopAt))
        if let snap = o.endSnapMs, Pauses.rate % Int64(o.fps) == 0 {   // test: an end just after a tick
            let tick = Pauses.rate / Int64(o.fps), s = Int64((snap * Double(Pauses.rate) / 1000).rounded())
            endFrames = max(s, (endFrames - s) / tick * tick + s)
        }
        let fileEnd = CMTimeAdd(t0, CMTime(value: endFrames, timescale: CMTimeScale(Pauses.rate)))
        let last = Int((endFrames * Int64(o.fps) + Pauses.rate - 1) / Pauses.rate) - 1
        tickQueue.sync { tickLimit = last }
        audioQueue.sync { audioEnd = stopAt }
        try? await Task.sleep(nanoseconds: UInt64((max(delay, Double(o.latencyMs) / 1000) + 0.03) * 1e9))
        for t in timers { t.cancel() }
        tickQueue.sync {}   // a tick or pump already running finishes first
        audioQueue.sync {}
        statsQueue.sync {}
        timers.removeAll()
        let capErr = await capture?.stop()
        if !o.audioOnly {
            tickQueue.sync {
                while lastK < last, let c = captureTime(ofTick: lastK + 1) {
                    lastK += 1
                    write(lastK, capturedAt: c, now: nil)
                }
            }
        }
        sync?.beeper?.stop()
        // the rest of the sound, and what the writer was not ready for yet (up to 2 s)
        var tries = 0
        while audioQueue.sync(execute: { pumpAudio(flush: true); return !audioBacklog.isEmpty }) && tries < 200 {
            tries += 1
            try? await Task.sleep(nanoseconds: 10_000_000)
        }
        let fin = await sound?.finish(at: fileEnd) ?? (ok: false, ms: 0, error: "no writer")
        statsQueue.sync { logStats(final: true) }
        await closePattern()
        endWatch()
        let dur = CMTimeSubtract(fileEnd, t0).seconds
        let pausedSec = Double(pauses.cut(before: pauses.position(at: stopAt))) / Double(Pauses.rate)
        let bytes = path.map(Paths.fileBytes) ?? 0
        let v = tickQueue.sync { videoJson() }
        let a = audioQueue.sync { audioJson() }
        let errors = tickQueue.sync { writer?.videoErrors ?? 0 } + audioQueue.sync { writer?.audioErrors ?? 0 }
        var video: [(String, J)] = [("width", .i(width)), ("height", .i(height)), ("fps", .i(o.fps)),
                                    ("display", .str(displayName)), ("test_pattern", .bool(o.testPattern))]
        if case .obj(let kv) = v { video += kv }
        let (by, detail) = stopLock.withLock { (stopBy, stopDetail) }
        let failed = by == "stream" || by == "writer" || by == "disk"   // app (■) / user / signal / time / wav_full: normal
        let summary = J.obj([
            ("sid", .str(sid)), ("version", .str(App.version)),
            ("platform", .str("macOS " + App.os)),
            ("path", .s(path?.path)), ("started", .str(startedIso)), ("ended", .str(Log.nowIso())),
            ("ok", .bool(fin.ok && !failed)), ("ended_by", .str(by ?? "time")),
            ("stop_detail", by == nil ? .null : .str(detail)),
            ("audio_only", .bool(o.audioOnly)), ("audio_file", o.audioOnly ? .str(path?.pathExtension ?? "") : .null),
            ("mic_problem", .s(micProblem)),
            ("duration_s", .n(dur, 2)), ("paused_s", .n(pausedSec)),
            ("pauses", .i(pauses.count)), ("file_bytes", .i(bytes)),
            ("avg_kbps", .n(dur > 0 ? Double(bytes) * 8 / 1000 / dur : 0, 0)), ("finalize_ms", .n(fin.ms)),
            ("finalize_error", fin.error.isEmpty ? .null : .str(fin.error)),
            ("capture_error", .s(capErr.map { "\($0)" })), ("video", o.audioOnly ? .null : .obj(video)), ("audio", a),
            ("perf", .obj([("footprint_peak_mb", .n(footPeak, 0)),
                           ("cpu_avg_pct", .n(cpuN > 0 ? cpuSum / Double(cpuN) : 0)), ("write_errors", .i(errors))])),
            ("system_at_end", lastSystem), ("screenshots", .arr(shotsLock.withLock { shots })),
        ])
        if Paths.ensure(Paths.sessions) {
            try? (summary.text + "\n").write(to: Paths.sessions.appendingPathComponent("\(sid).json"), atomically: true,
                                             encoding: .utf8)
        }
        Log.event("rec.stop", ok: fin.ok && !failed, ms: sw.ms,
                  msg: String(format: "%.2f s %.1f MB finalize %.0f ms ended by %@", dur, Double(bytes) / 1048576.0,
                              fin.ms, by ?? "time") + (fin.error.isEmpty ? "" : " " + fin.error))
        guard let path else { return .obj([("ok", .bool(false)), ("sid", .str(sid)), ("summary", summary)]) }
        let check = await Verify.file(path, audioOnly: o.audioOnly)
        Log.line("verify", [("result", check)])
        // everything written is in the file: one picture per tick, and sound to the end
        let written = tickQueue.sync { writer?.videoSamples ?? 0 }
        let vs = Self.stream(check, "video"), au = Self.stream(check, "audio")
        let complete = (o.audioOnly || vs?.samples == Int64(written)) && au.map { abs($0.last - dur) < 0.1 } == true
        Log.event("rec.complete", ok: complete, msg: "video \(vs?.samples ?? -1) of \(written), sound to "
                  + String(format: "%.3f s of %.3f s", au?.last ?? -1, dur))
        var out: [(String, J)] = [("ok", .bool(fin.ok && !failed && check["ok"]?.isTrue == true && complete)),
                                  ("sid", .str(sid)), ("path", .str(path.path)), ("ended_by", .str(by ?? "time")),
                                  ("complete", .bool(complete)), ("summary", summary), ("verify", check)]
        if let s = sync {
            let r = await s.result(file: path, pauses: pauses)
            Log.line("sync", [("result", r)])
            out.append(("sync", r))
        }
        return .obj(out)
    }

    /// A start that failed or was called off: everything stops and the file goes (a WAV was left at 44 bytes).
    private func abandon() async {
        for t in timers { t.cancel() }
        tickQueue.sync {}   // a tick or pump already running finishes first
        audioQueue.sync {}
        statsQueue.sync {}
        _ = await capture?.stop()
        sync?.beeper?.stop()
        sound?.cancel()
        await closePattern()
        endWatch()
    }

    private func closePattern() async {
        guard let p = pattern else { return }
        pattern = nil
        await MainActor.run { p.close() }
    }

    /// Sleep, wake and screen changes go into the log (a stream that ends because of them ends the file, see onStop).
    private func watchSystem() {
        let ws = NSWorkspace.shared.notificationCenter
        let events: [(NotificationCenter, Notification.Name, String)] = [
            (ws, NSWorkspace.willSleepNotification, "system.will_sleep"),
            (ws, NSWorkspace.didWakeNotification, "system.did_wake"),
            (ws, NSWorkspace.screensDidSleepNotification, "system.screens_sleep"),
            (ws, NSWorkspace.screensDidWakeNotification, "system.screens_wake"),
            (ws, NSWorkspace.sessionDidResignActiveNotification, "system.session_inactive"),
            (ws, NSWorkspace.sessionDidBecomeActiveNotification, "system.session_active"),
            (DistributedNotificationCenter.default(), Notification.Name("com.apple.screenIsLocked"), "system.screen_locked"),
            (DistributedNotificationCenter.default(), Notification.Name("com.apple.screenIsUnlocked"),
             "system.screen_unlocked"),
            (NotificationCenter.default, NSApplication.didChangeScreenParametersNotification, "system.screens_changed"),
        ]
        for (center, name, phase) in events {
            observers.append((center, center.addObserver(forName: name, object: nil, queue: nil) { [weak self] _ in
                Log.event(phase, ok: true)
                self?.noteSystem(phase)
            }))
        }
    }

    /// The screen's state for videoCapturing (any thread).
    private func noteSystem(_ phase: String) {
        sysLock.withLock {
            switch phase {
            case "system.screen_locked": screenLocked = true
            case "system.screen_unlocked": screenLocked = false
            case "system.session_inactive": sessionInactive = true
            case "system.session_active": sessionInactive = false
            default: break
            }
        }
    }

    private func endWatch() {
        for (center, o) in observers { center.removeObserver(o) }
        observers.removeAll()
        if let a = activity { ProcessInfo.processInfo.endActivity(a) }
        activity = nil
    }

    /// The sample count and the last sample's time of a kind of stream in a `--verify-file` result.
    private static func stream(_ v: J, _ kind: String) -> (samples: Int64, last: Double)? {
        guard case .arr(let xs)? = v["streams"] else { return nil }
        for x in xs {
            guard case .str(let k)? = x["kind"], k == kind else { continue }
            guard case .int(let n)? = x["samples"], case .num(let last, _)? = x["last_s"] else { return nil }
            return (n, last)
        }
        return nil
    }
}
