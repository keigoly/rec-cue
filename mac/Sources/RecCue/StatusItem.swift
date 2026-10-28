import AppKit

/// The menu bar icon (the Windows version's taskbar buttons): 録画・録音・一時停止 / 再開・停止・スクショ, also when the
/// window is closed. The dot is grey when idle, red while recording, blue while recording sound only, amber when paused.
@MainActor
final class StatusItem: NSObject, NSMenuDelegate {
    private let item = NSStatusBar.system.statusItem(withLength: NSStatusItem.squareLength)
    private let controller: Controller
    private let openWindow: () -> Void
    private var timer: Timer?

    init(controller: Controller, openWindow: @escaping () -> Void) {
        self.controller = controller
        self.openWindow = openWindow
        super.init()
        let menu = NSMenu()
        menu.delegate = self
        item.menu = menu
        item.button?.toolTip = "Rec Cue!"
        controller.observers.append { [weak self] in self?.refresh() }
        refresh()
    }

    func refresh() {
        let c = controller
        let color: NSColor?
        switch c.state {
        case .idle: color = nil
        case .starting, .saving: color = .systemGray
        case .recording: color = c.paused ? .systemOrange : (c.audioOnly ? .systemBlue : .systemRed)
        }
        item.button?.image = Self.dot(color, paused: c.state == .recording && c.paused)
        // the time in the menu moves while recording; nothing runs when idle (no CPU)
        if c.state == .recording && timer == nil {
            timer = Timer.scheduledTimer(withTimeInterval: 1, repeats: true) { [weak self] _ in
                MainActor.assumeIsolated { self?.refreshTitle() }
            }
        } else if c.state != .recording {
            timer?.invalidate()
            timer = nil
        }
        refreshTitle()
    }

    private func refreshTitle() {
        item.button?.toolTip = "Rec Cue! — " + statusText()
    }

    private func statusText() -> String {
        let c = controller
        let t = Self.clock(c.elapsed)
        switch c.state {
        case .idle: return "待機中"
        case .starting: return "準備中…"
        case .saving: return "保存中…"
        case .recording:
            if c.paused { return "❚❚ 一時停止中 \(t)" }
            return c.audioOnly ? "● 録音中 \(t)" : "● 録画中 \(t)"
        }
    }

    static func clock(_ s: Double) -> String {
        let n = Int(s)
        return n >= 3600 ? String(format: "%d:%02d:%02d", n / 3600, n / 60 % 60, n % 60)
                         : String(format: "%02d:%02d", n / 60, n % 60)
    }

    nonisolated func menuNeedsUpdate(_ menu: NSMenu) {
        MainActor.assumeIsolated { build(menu) }
    }

    private func build(_ menu: NSMenu) {
        menu.removeAllItems()
        let c = controller
        let head = NSMenuItem(title: statusText(), action: nil, keyEquivalent: "")
        head.isEnabled = false
        menu.addItem(head)
        menu.addItem(.separator())
        let idle = c.state == .idle, rec = c.state == .recording
        add(menu, "録画を始める", #selector(record), idle)
        add(menu, "録音だけを始める", #selector(recordSound), idle)
        add(menu, c.paused ? "再開" : "一時停止", #selector(pause), rec)
        add(menu, "停止して保存", #selector(stop), rec || c.state == .starting)
        menu.addItem(.separator())
        add(menu, "スクショ", #selector(shot), c.state != .saving)
        add(menu, "窓を開く", #selector(window), true)
        add(menu, "保存先を開く", #selector(folder), true)
        menu.addItem(.separator())
        add(menu, "Rec Cue! を終了", #selector(quit), idle)
    }

    private func add(_ menu: NSMenu, _ title: String, _ action: Selector, _ enabled: Bool) {
        let m = NSMenuItem(title: title, action: enabled ? action : nil, keyEquivalent: "")
        m.target = self
        m.isEnabled = enabled
        menu.addItem(m)
    }

    @objc private func record() { controller.start(audioOnly: false) }
    @objc private func recordSound() { controller.start(audioOnly: true) }
    @objc private func pause() { controller.togglePause() }
    @objc private func stop() { controller.stop() }
    @objc private func shot() { controller.screenshot() }
    @objc private func window() { openWindow() }
    @objc private func folder() { controller.openFolder() }
    @objc private func quit() { NSApp.terminate(nil) }

    /// A small dot for the menu bar: an outline that follows the menu bar's colour when idle, filled in colour otherwise.
    private static func dot(_ color: NSColor?, paused: Bool) -> NSImage {
        let img = NSImage(size: NSSize(width: 18, height: 18), flipped: false) { r in
            let circle = NSBezierPath(ovalIn: r.insetBy(dx: 4, dy: 4))
            if let color {
                color.setFill()
                circle.fill()
                if paused {
                    NSColor.white.setFill()
                    NSBezierPath(rect: NSRect(x: 7, y: 6.5, width: 1.5, height: 5)).fill()
                    NSBezierPath(rect: NSRect(x: 9.5, y: 6.5, width: 1.5, height: 5)).fill()
                }
            } else {
                NSColor.black.setStroke()
                circle.lineWidth = 1.5
                circle.stroke()
            }
            return true
        }
        img.isTemplate = color == nil
        return img
    }
}
