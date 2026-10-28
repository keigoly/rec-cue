import AppKit
import AVFoundation
import CoreGraphics

/// The screens and microphones of this Mac, and whether Rec Cue! may record them.
enum Devices {
    struct Display {
        let id: CGDirectDisplayID
        let name: String
        let points: CGSize
        let scale: CGFloat
        let main: Bool
        let builtin: Bool
        var pixelWidth: Int { Int((points.width * scale).rounded()) }
        var pixelHeight: Int { Int((points.height * scale).rounded()) }
    }

    /// Screens in Rec Cue's order: the first is the main screen (the one with the menu bar), then the others as macOS
    /// lists them. `monitor=0` in the settings is the first.
    static func displays() -> [Display] {
        NSScreen.screens.compactMap { s in
            guard let n = s.deviceDescription[NSDeviceDescriptionKey("NSScreenNumber")] as? NSNumber else { return nil }
            let id = CGDirectDisplayID(n.uint32Value)
            return Display(id: id, name: s.localizedName, points: s.frame.size, scale: s.backingScaleFactor,
                           main: id == CGMainDisplayID(), builtin: CGDisplayIsBuiltin(id) != 0)
        }
    }

    /// Which of `count` screens a setting means (0 = the main screen, the one with the menu bar). A screen that is not
    /// there (an external display unplugged) falls back to the main screen, as in the Windows version: the recording,
    /// the screenshot, the level check and the window all go by this.
    static func screenIndex(_ setting: Int, of count: Int) -> Int {
        setting >= 0 && setting < count ? setting : 0
    }

    static func microphones() -> [AVCaptureDevice] {
        AVCaptureDevice.DiscoverySession(deviceTypes: [.microphone, .external], mediaType: .audio,
                                         position: .unspecified).devices
    }

    /// `none` → nil. `auto` / `default` → the Mac's default input. Anything else → the first microphone whose name
    /// contains it.
    static func microphone(_ spec: String) -> AVCaptureDevice? {
        switch spec.lowercased() {
        case "none", "": return nil
        case "auto", "default": return AVCaptureDevice.default(for: .audio)
        default: return microphones().first { $0.localizedName.localizedCaseInsensitiveContains(spec) }
        }
    }

    static func micAccess() -> String {
        switch AVCaptureDevice.authorizationStatus(for: .audio) {
        case .authorized: return "authorized"
        case .denied: return "denied"
        case .restricted: return "restricted"
        case .notDetermined: return "not_determined"
        @unknown default: return "unknown"
        }
    }

    static func listJson() -> J {
        let def = AVCaptureDevice.default(for: .audio)?.uniqueID
        let ds = displays().enumerated().map { i, d in
            J.obj([("index", .i(i + 1)), ("id", .i(d.id)), ("name", .str(d.name)), ("width", .i(d.pixelWidth)),
                   ("height", .i(d.pixelHeight)), ("scale", .n(Double(d.scale))), ("main", .bool(d.main)),
                   ("internal", .bool(d.builtin))])
        }
        let ms = microphones().map { m in
            J.obj([("id", .str(m.uniqueID)), ("name", .str(m.localizedName)), ("default", .bool(m.uniqueID == def))])
        }
        return .obj([("ok", .bool(true)), ("version", .str(App.version)),
                     ("os", .str(App.os)),
                     ("screen_access", .bool(CGPreflightScreenCaptureAccess())), ("mic_access", .str(micAccess())),
                     ("monitors", .arr(ds)), ("mics", .arr(ms))])
    }
}
