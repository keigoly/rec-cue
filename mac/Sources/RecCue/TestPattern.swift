import AppKit
import QuartzCore

/// A small window of our own with a moving bar and a counter. `--test-pattern` records only Rec Cue's own windows,
/// so a test runs ScreenCaptureKit and the encoder for real while nothing else on the screen ends up in the file.
/// The window never takes the focus and lets clicks through. `--sync-test` also flashes it white (`flash`).
@MainActor
final class TestPattern {
    private let panel: NSPanel
    private let screen: NSScreen
    private let bar = CALayer()
    private let label = CATextLayer()
    private let white = CALayer()
    private var timer: Timer?
    private var step = 0
    private let t0 = Date()
    private let animated: Bool

    /// `animated: false` keeps the window still (it changes only when it flashes), so ScreenCaptureKit sends frames
    /// only then — the way an ordinary screen that sits still behaves.
    init(on screen: NSScreen, animated: Bool = true) {
        self.screen = screen
        self.animated = animated
        let size = NSSize(width: 480, height: 270)
        let vf = screen.visibleFrame
        panel = NSPanel(contentRect: NSRect(x: vf.minX + 24, y: vf.minY + 24, width: size.width, height: size.height),
                        styleMask: [.borderless, .nonactivatingPanel], backing: .buffered, defer: false)
        panel.level = .floating
        panel.isOpaque = true
        panel.hasShadow = false
        panel.ignoresMouseEvents = true
        panel.collectionBehavior = [.canJoinAllSpaces, .stationary, .ignoresCycle]
        let v = NSView(frame: NSRect(origin: .zero, size: size))
        v.wantsLayer = true
        v.layer?.backgroundColor = CGColor(red: 0.08, green: 0.09, blue: 0.11, alpha: 1)
        bar.backgroundColor = CGColor(red: 0.79, green: 0.64, blue: 0.35, alpha: 1)
        bar.frame = CGRect(x: 0, y: 0, width: 24, height: size.height)
        v.layer?.addSublayer(bar)
        label.fontSize = 22
        label.foregroundColor = CGColor(gray: 1, alpha: 1)
        label.contentsScale = screen.backingScaleFactor
        label.frame = CGRect(x: 16, y: size.height / 2 - 14, width: size.width - 32, height: 32)
        v.layer?.addSublayer(label)
        white.backgroundColor = CGColor(gray: 1, alpha: 1)
        white.frame = CGRect(origin: .zero, size: size)
        white.isHidden = true
        v.layer?.addSublayer(white)   // on top of everything
        panel.contentView = v
    }

    func show() {
        panel.orderFrontRegardless()
        guard animated else {
            label.string = "Rec Cue! test (still)"
            bar.isHidden = true   // standing still, it would sit on the sync test's probe
            return
        }
        timer = Timer.scheduledTimer(withTimeInterval: 1.0 / 30, repeats: true) { [weak self] _ in
            MainActor.assumeIsolated { self?.advance() }
        }
        advance()
    }

    func close() {
        timer?.invalidate()
        timer = nil
        panel.orderOut(nil)
    }

    /// The whole window white for `seconds`.
    func flash(_ seconds: Double) {
        setWhite(true)
        DispatchQueue.main.asyncAfter(deadline: .now() + seconds) { [weak self] in
            MainActor.assumeIsolated { self?.setWhite(false) }
        }
    }

    /// The whole window in one colour (the screenshot tests: magenta, which an ordinary screen hardly has).
    func fill(_ color: CGColor) {
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        white.backgroundColor = color
        white.isHidden = false
        CATransaction.commit()
    }

    /// Where the sync test looks for the flash, in pixels of the captured screen (origin top left): a 16-point square
    /// near the window's top-left corner, above the counter text (the bar passes it, but the bar is not white).
    var probeRect: CGRect {
        let s = screen.backingScaleFactor, f = panel.frame, sf = screen.frame
        return CGRect(x: (f.minX - sf.minX + 8) * s, y: (sf.maxY - f.maxY + 8) * s, width: 16 * s, height: 16 * s)
    }

    private func setWhite(_ on: Bool) {
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        white.isHidden = !on
        CATransaction.commit()
        Log.event("test.flash", ok: true, msg: on ? "on" : "off")
    }

    private func advance() {
        step += 1
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        let w = panel.frame.width
        bar.frame.origin.x = CGFloat(step * 8).truncatingRemainder(dividingBy: w)
        label.string = String(format: "Rec Cue! test  %.2f s  #%d", Date().timeIntervalSince(t0), step)
        CATransaction.commit()
    }
}
