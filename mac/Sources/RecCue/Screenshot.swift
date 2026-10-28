import AppKit
import ImageIO
import ScreenCaptureKit
import UniformTypeIdentifiers

/// スクショ: the screen being recorded (the `monitor` one), without Rec Cue's own windows and without the pointer,
/// as a PNG next to the recordings and on the clipboard — the Windows version's screenshot. SCScreenshotManager
/// (macOS 14+).
enum Screenshot {
    struct Options {
        var monitor = 0
        var dir = Paths.defaultOutDir
        var prefix = "Rec Cue"
        var clipboard = true
        var out: URL?            // the exact file (tests)
        var testPattern = false  // only Rec Cue's own test window (a test that captures nothing else)
        var selfTest = false     // the test window on screen, the screen captured without it: is it really left out?
        var includeSelf = false  // the self test's control: the same capture with Rec Cue! left in (must find it)
    }

    static let magenta = CGColor(red: 1, green: 0, blue: 1, alpha: 1)

    static func take(_ o: Options) async -> J {
        let sw = Stopwatch()
        guard CGPreflightScreenCaptureAccess() else { return fail("no screen recording permission") }
        let displays = await MainActor.run { Devices.displays() }
        guard !displays.isEmpty else { return fail("no screen") }
        let pick = Devices.screenIndex(o.monitor, of: displays.count)
        if pick != o.monitor {
            Log.event("shot.screen", ok: false, msg: "monitor=\(o.monitor) is not there (\(displays.count) screens): the main screen")
        }
        let disp = displays[pick]
        var pattern: TestPattern?
        if o.testPattern || o.selfTest {
            pattern = await MainActor.run {
                let screen = NSScreen.screens.first {
                    ($0.deviceDescription[NSDeviceDescriptionKey("NSScreenNumber")] as? NSNumber)?.uint32Value == disp.id
                }
                let p = TestPattern(on: screen ?? NSScreen.screens[0], animated: false)
                p.show()
                p.fill(Self.magenta)   // easy to find if it ends up in the picture, unlike white
                return p
            }
            try? await Task.sleep(nanoseconds: 300_000_000)
        }
        let result = await capture(o, disp: disp, pattern: pattern, sw: sw)
        if let p = pattern { await MainActor.run { p.close() } }
        return result
    }

    private static func capture(_ o: Options, disp: Devices.Display, pattern: TestPattern?, sw: Stopwatch) async -> J {
        do {
            let content = try await SCShareableContent.excludingDesktopWindows(false, onScreenWindowsOnly: true)
            guard let display = content.displays.first(where: { $0.displayID == disp.id }) else {
                return fail("ScreenCaptureKit does not list screen \(disp.id)")
            }
            let me = content.applications.filter { $0.processID == getpid() }
            // (an "including" filter draws the windows from the picture's top-left corner, not where they are on
            // the screen — fine for a test-window shot, but the self test looks at the window's place on the screen)
            let filter = o.testPattern ? SCContentFilter(display: display, including: me, exceptingWindows: [])
                       : SCContentFilter(display: display, excludingApplications: o.includeSelf ? [] : me,
                                         exceptingWindows: [])
            let scale = CGFloat(filter.pointPixelScale)
            let cfg = SCStreamConfiguration()
            cfg.width = Int((filter.contentRect.width * scale).rounded())
            cfg.height = Int((filter.contentRect.height * scale).rounded())
            cfg.showsCursor = false
            cfg.colorSpaceName = CGColorSpace.sRGB
            let shot = Stopwatch()
            let image = try await SCScreenshotManager.captureImage(contentFilter: filter, configuration: cfg)
            let captureMs = shot.ms
            if o.selfTest, let p = pattern {
                // nothing is saved: only how much of the window's place is the window's magenta
                let probe = await MainActor.run { p.probeRect }
                let found = magentaPct(image, in: probe)
                let ok = o.includeSelf ? found > 50 : (found >= 0 && found < 50)
                Log.event("shot.selftest", ok: ok, ms: sw.ms, msg: String(format: "window place %.0f%% magenta", found))
                let px = pixels(image, probe, 16, 16)
                let avg = px.isEmpty ? [-1.0, -1, -1] : [px.map { Double($0.r) }, px.map { Double($0.g) }, px.map { Double($0.b) }]
                    .map { $0.reduce(0, +) / Double(px.count) }
                return .obj([("ok", .bool(ok)), ("self_test", .bool(true)), ("control", .bool(o.includeSelf)),
                             ("magenta_pct", .n(found, 0)),
                             ("probe", .str("\(Int(probe.minX)),\(Int(probe.minY)) \(Int(probe.width))x\(Int(probe.height))")),
                             ("probe_rgb", .arr(avg.map { .n($0, 0) })), ("visible_pct", .n(visiblePct(image), 1)),
                             ("visible_box", .str(visibleBox(image))),
                             ("width", .i(image.width)), ("height", .i(image.height)), ("capture_ms", .n(captureMs))])
            }
            let url = o.out ?? freeName(o.dir, o.prefix)
            guard Paths.ensure(url.deletingLastPathComponent()) else { return fail("cannot make the folder") }
            let save = Stopwatch()
            guard let dest = CGImageDestinationCreateWithURL(url as CFURL, UTType.png.identifier as CFString, 1, nil)
            else { return fail("cannot write \(url.lastPathComponent)") }
            CGImageDestinationAddImage(dest, image, nil)
            guard CGImageDestinationFinalize(dest) else { return fail("cannot finish \(url.lastPathComponent)") }
            let saveMs = save.ms
            var copied = false
            if o.clipboard {
                copied = await MainActor.run {
                    let pb = NSPasteboard.general
                    pb.clearContents()
                    return pb.writeObjects([NSImage(cgImage: image, size: NSSize(width: CGFloat(image.width) / scale,
                                                                                 height: CGFloat(image.height) / scale))])
                }
            }
            Log.event("shot", ok: true, ms: sw.ms, msg: String(format: "%@ %dx%d capture %.1f ms save %.1f ms",
                                                                url.lastPathComponent, image.width, image.height,
                                                                captureMs, saveMs))
            return .obj([("ok", .bool(true)), ("path", .str(url.path)), ("width", .i(image.width)),
                         ("height", .i(image.height)), ("capture_ms", .n(captureMs)), ("save_ms", .n(saveMs)),
                         ("bytes", .i(Paths.fileBytes(url))), ("clipboard", .bool(copied)),
                         ("visible_pct", .n(visiblePct(image), 1))])
        } catch {
            return fail("\(error.localizedDescription)")
        }
    }

    private static func fail(_ msg: String) -> J {
        Log.event("shot", ok: false, msg: msg)
        return .obj([("ok", .bool(false)), ("error", .str(msg))])
    }

    /// "Rec Cue 2026-10-27 012233.png", with " (2)" … when that name is taken.
    private static func freeName(_ dir: URL, _ prefix: String) -> URL {
        let base = "\(prefix) \(Paths.stamp())"
        var url = dir.appendingPathComponent(base + ".png")
        var n = 2
        while FileManager.default.fileExists(atPath: url.path) {
            url = dir.appendingPathComponent("\(base) (\(n)).png")
            n += 1
        }
        return url
    }

    /// The share of a small copy of the picture that is not black (a test's sanity check: a test-window shot shows
    /// the window and nothing else).
    private static func visiblePct(_ image: CGImage) -> Double {
        let px = pixels(image, CGRect(x: 0, y: 0, width: image.width, height: image.height), 160, 100)
        guard !px.isEmpty else { return -1 }
        return 100 * Double(px.filter { max($0.r, $0.g, $0.b) > 10 }.count) / Double(px.count)
    }

    /// Where the non-black part of the picture is, in pixels (origin top left): "x,y wxh".
    private static func visibleBox(_ image: CGImage) -> String {
        let w = 160, h = 100
        let px = pixels(image, CGRect(x: 0, y: 0, width: image.width, height: image.height), w, h)
        guard !px.isEmpty else { return "?" }
        var x0 = w, y0 = h, x1 = -1, y1 = -1
        for i in 0..<(w * h) where max(px[i].r, px[i].g, px[i].b) > 10 {
            x0 = min(x0, i % w); x1 = max(x1, i % w); y0 = min(y0, i / w); y1 = max(y1, i / w)
        }
        guard x1 >= 0 else { return "none" }
        let sx = Double(image.width) / Double(w), sy = Double(image.height) / Double(h)
        return "\(Int(Double(x0) * sx)),\(Int(Double(y0) * sy)) \(Int(Double(x1 - x0 + 1) * sx))x\(Int(Double(y1 - y0 + 1) * sy))"
    }

    /// The share of the test window's magenta in a rectangle of the picture (pixels, origin top left).
    private static func magentaPct(_ image: CGImage, in r: CGRect) -> Double {
        let px = pixels(image, r, 16, 16)
        guard !px.isEmpty else { return -1 }
        return 100 * Double(px.filter { $0.r > 200 && $0.g < 80 && $0.b > 200 }.count) / Double(px.count)
    }

    /// The pixels of a rectangle, scaled to w × h.
    private static func pixels(_ image: CGImage, _ r: CGRect, _ w: Int, _ h: Int) -> [(r: UInt8, g: UInt8, b: UInt8)] {
        guard let part = image.cropping(to: r),
              let ctx = CGContext(data: nil, width: w, height: h, bitsPerComponent: 8, bytesPerRow: w * 4,
                                  space: CGColorSpaceCreateDeviceRGB(),
                                  bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { return [] }
        ctx.interpolationQuality = .low
        ctx.draw(part, in: CGRect(x: 0, y: 0, width: w, height: h))
        guard let data = ctx.data else { return [] }
        let p = data.assumingMemoryBound(to: UInt8.self)
        return (0..<(w * h)).map { (p[$0 * 4], p[$0 * 4 + 1], p[$0 * 4 + 2]) }
    }
}
