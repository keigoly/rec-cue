import AppKit
import CoreText
import RecCueObjC

/// Design A ("Executive Dark") of the Windows version, drawn the same way: one view paints everything from a plain
/// view model, and the mouse hits the same rectangles. Sizes are the Windows version's DIPs used as points
/// (480 × 340); colours are sRGB.
enum Pal {
    static func c(_ hex: UInt32, _ a: CGFloat = 1) -> NSColor {
        NSColor(srgbRed: CGFloat((hex >> 16) & 0xff) / 255, green: CGFloat((hex >> 8) & 0xff) / 255,
                blue: CGFloat(hex & 0xff) / 255, alpha: a)
    }
    static let canvas = c(0x13161C), well = c(0x151820), wellBorder = c(0x222733)
    static let meterBox = c(0x161A22), meterBorder = c(0x242A36), meterTrack = c(0x0D1016), tick = c(0x2D3545)
    static let peak = c(0xE2E8F0)
    /// deck tints: plain, screen recording, audio recording, attention — (top, bottom, border, edge)
    static let deck: [(NSColor, NSColor, NSColor, NSColor)] = [
        (c(0x191E28), c(0x151921), c(0x262C3A), c(0x303747)), (c(0x21171D), c(0x17151B), c(0x4A2330), c(0x5E2B3B)),
        (c(0x161C2A), c(0x13161F), c(0x1F3A5F), c(0x2A4F7E)), (c(0x231C16), c(0x181619), c(0x5C431C), c(0x735422)),
    ]
    static let control = c(0x1B202A), controlHot = c(0x232A38), controlDown = c(0x141820)
    static let controlBorder = c(0x2B3240), controlBorderHot = c(0x3B4559), edge = c(0x353D4F)
    static let offFill = c(0x161922), offBorder = c(0x222733), offText = c(0x5A6270)
    static let input = c(0x181C24), inputBorder = c(0x2A3140), inputEdge = c(0x222834)
    static let text = c(0xF2F4F8), text2 = c(0xB4BCCB), text3 = c(0x6E7787), label = c(0xD0D6E2)
    static let fieldText = c(0xC8CDD8), icon = c(0x8C95A6), timerLive = c(0xF8FAFC), hint = c(0x687182)
    static let path = c(0x9AA2B2), pathFile = c(0xE2E8F0), dbLive = c(0xD8DEE9)
    static let cyan = c(0x38BDF8), cyanDeep = c(0x0284C7), cyanText = c(0x7CE0FF), teal = c(0x2DD4BF)
    static let pinOnFill = c(0x132838), pinOnBorder = c(0x26658C), pinOnEdge = c(0x3888B8), flashFill = c(0x162E3E)
    static let toastBg = c(0x112A3A)
    static let crimson = c(0xE11D48), rose = c(0xF43F5E), roseLight = c(0xFB7185), rosePale = c(0xFDA4AF)
    static let recChipBg = c(0x3B121E), recOuter = c(0x2D121B), idleOuter = c(0x11141B), idleRing = c(0x2E3646)
    static let busy = c(0x3A3F48)
    static let blue = c(0x3B82F6), blueHi = c(0x60A5FA), bluePale = c(0xBFDBFE), blueChipBg = c(0x0F213D)
    static let blueOuter = c(0x0E1A2F), blueText = c(0x93C5FD)
    static let amber = c(0xFBBF24), amberText = c(0xFCD34D), warnBg = c(0x2B1F11), warnBorder = c(0xD97706)
    static let silentBorder = c(0x92400E)
}

/// Japanese in Hiragino Sans with proportional kana (`palt`, as Windows draws them: the bubble's one-line budget was
/// measured that way), figures in SF Mono, the brand in the system font.
enum Fonts {
    private static var cache: [String: NSFont] = [:]
    /// weight: 3 normal, 4 medium, 6 semibold, 7 bold
    static func jp(_ size: CGFloat, _ w: Int = 3) -> NSFont {
        let key = "jp\(size)-\(w)"
        if let f = cache[key] { return f }
        let d = NSFontDescriptor(name: "HiraginoSans-W\(w)", size: size).addingAttributes([
            .featureSettings: [[NSFontDescriptor.FeatureKey.typeIdentifier: kTextSpacingType,
                                NSFontDescriptor.FeatureKey.selectorIdentifier: kAltProportionalTextSelector]],
        ])
        let f = NSFont(descriptor: d, size: size) ?? .systemFont(ofSize: size)
        cache[key] = f
        return f
    }
    static func mono(_ size: CGFloat, _ weight: NSFont.Weight = .regular) -> NSFont {
        let key = "mono\(size)-\(weight.rawValue)"
        if let f = cache[key] { return f }
        let f = NSFont.monospacedSystemFont(ofSize: size, weight: weight)
        cache[key] = f
        return f
    }
}

enum Phase { case idle, checking, starting, recording, saving }
enum Tone { case normal, good, warn, bad }
enum Part: Hashable { case record, audio, pause, audioPause, pin, shot, gear, check, open, format, screen, mic, sys, guide }

/// Everything the painter needs (the Windows version's UiModel).
struct UiModel {
    struct Channel {
        var setting = "自動"           // the menu label of the setting
        var mode = ""                   // 自動 / 既定 … when the setting is a preset
        var device = ""                 // the open device ("" = none open)
        var active = false
        var warn = false
        var db: Double = -120
        var hold: Double = -120
    }
    var phase = Phase.idle
    var audioOnly = false
    var paused = false
    var split: Double = 0              // −1 (audio) … 0 … +1 (screen)
    var timer = "00:00:00"
    var status = ""
    var tone = Tone.normal
    var alert = false
    var mic = Channel(setting: "自動 (Mac の既定の入力)")
    var sys = Channel(setting: "自動 (すべてのアプリの音)")
    var screenText = "1: メイン"
    var monitors: [CGRect] = []        // desktop coordinates, the main one first
    var selected = 0
    var pathLabel = "保存先"
    var path = "~/Movies/画面録画"
    var hot: Part?
    var pressed: Part?
    var focus: Part? = .record
    var showFocus = false
    var pulse: Double = 1
    var topmost = false
    var shots = 0
    var shotFlash = false
    var audioFormat = "M4A"
    var recHotkey = "", audioHotkey = "", shotHotkey = ""   // registered shortcuts ("" = none)
    var comment = ""
    var commentTone = Tone.normal
    var guideRec = false

    var screenLive: Bool { phase == .starting || phase == .recording || phase == .saving ? !audioOnly : false }
    var audioLive: Bool { phase == .starting || phase == .recording || phase == .saving ? audioOnly : false }

    /// Which parts can be used now (disabled ones get no hover, press or click).
    func enabled(_ p: Part) -> Bool {
        let idle = phase == .idle || phase == .checking
        switch p {
        case .record: return idle || (phase == .recording && !audioOnly)
        case .audio: return idle || (phase == .recording && audioOnly)
        case .pause: return phase == .recording && !audioOnly
        case .audioPause: return phase == .recording && audioOnly
        case .screen, .mic, .sys, .format, .check: return idle
        case .pin, .shot, .gear, .open: return true
        case .guide: return phase != .starting && phase != .saving
        }
    }
}

final class WindowView: NSView, NSViewToolTipOwner {
    static let size = NSSize(width: 480, height: 340)
    var model = UiModel() { didSet { needsDisplay = true } }
    var onPart: ((Part) -> Void)?
    /// The tooltip of a part, asked when it is about to show ("" = none).
    var tipText: ((Part) -> String)?
    private var tips: [NSView.ToolTipTag: Part] = [:]
    var guideIdle: NSImage?
    var guideRecImage: NSImage?

    override var isFlipped: Bool { true }
    override var acceptsFirstResponder: Bool { true }
    /// While another app is in front, the first click presses the button (■ on the pinned window during a meeting),
    /// as in the Windows version — not only brings Rec Cue! forward.
    override func acceptsFirstMouse(for event: NSEvent?) -> Bool { true }

    // MARK: geometry (DIP = pt, origin top left)

    static let deckTop: CGFloat = 42, deckBottom: CGFloat = 110
    static func edge(_ split: Double) -> CGFloat {
        let s = max(-1, min(1, split))
        return CGFloat((s >= 0 ? 237 + (396 - 237) * s : 237 + (82 - 237) * -s).rounded())
    }
    static let pin = NSRect(x: 370, y: 8, width: 28, height: 28)
    static let shot = NSRect(x: 404, y: 8, width: 28, height: 28)
    static let gear = NSRect(x: 438, y: 8, width: 28, height: 28)
    static let check = NSRect(x: 14, y: 266, width: 190, height: 30)
    static let open = NSRect(x: 210, y: 266, width: 190, height: 30)
    static let format = NSRect(x: 316, y: 51, width: 48, height: 18)
    static let guideBox = NSRect(x: 402, y: 240, width: 76, height: 98)
    static let bubble = NSRect(x: 14, y: 304, width: 380, height: 28)
    static let rowY: [Part: CGFloat] = [.screen: 120, .mic: 156, .sys: 192]

    func part(at p: NSPoint) -> Part? {
        func circle(_ cx: CGFloat, _ cy: CGFloat, _ r: CGFloat) -> Bool { hypot(p.x - cx, p.y - cy) <= r }
        let m = model
        var hit: Part?
        if circle(50, 76, 28) { hit = .record }
        else if circle(434, 76, 24) { hit = .audio }
        else if circle(100, 76, 19) { hit = .pause }
        else if circle(391, 76, 17) { hit = .audioPause }
        else if Self.pin.contains(p) { hit = .pin }
        else if Self.shot.contains(p) { hit = .shot }
        else if Self.gear.contains(p) { hit = .gear }
        else if Self.check.contains(p) { hit = .check }
        else if Self.open.contains(p) { hit = .open }
        else if Self.format.contains(p) { hit = .format }
        else if NSRect(x: 14, y: 120, width: 452, height: 28).contains(p) { hit = .screen }
        else if NSRect(x: 14, y: 156, width: 280, height: 28).contains(p) { hit = .mic }
        else if NSRect(x: 14, y: 192, width: 280, height: 28).contains(p) { hit = .sys }
        else if Self.guideBox.contains(p) || Self.bubble.contains(p) { hit = .guide }
        guard let h = hit, m.enabled(h) else { return nil }
        if h == .format && !(m.phase == .idle || m.phase == .checking) { return nil }
        return h
    }

    // MARK: mouse

    override func updateTrackingAreas() {
        super.updateTrackingAreas()
        for t in trackingAreas { removeTrackingArea(t) }
        addTrackingArea(NSTrackingArea(rect: bounds, options: [.mouseMoved, .mouseEnteredAndExited, .activeInKeyWindow,
                                                               .inVisibleRect], owner: self))
    }

    override func mouseMoved(with e: NSEvent) {
        let h = part(at: convert(e.locationInWindow, from: nil))
        if h != model.hot { model.hot = h }
    }

    override func mouseExited(with e: NSEvent) {
        model.hot = nil
    }

    override func mouseDown(with e: NSEvent) {
        let h = part(at: convert(e.locationInWindow, from: nil))
        model.pressed = h
        model.hot = h
        if let h { model.focus = h }   // the guide too (no ring is drawn for her; Space / Enter then make her talk)
    }

    override func mouseDragged(with e: NSEvent) {
        let h = part(at: convert(e.locationInWindow, from: nil))
        if h != model.hot { model.hot = h }
    }

    override func mouseUp(with e: NSEvent) {
        let h = part(at: convert(e.locationInWindow, from: nil))
        let p = model.pressed
        model.pressed = nil
        if let h, h == p { onPart?(h) }
    }

    // MARK: keyboard (the Windows version's: Tab / Shift+Tab move between the parts that can be used, Space and Enter
    // press the one with the focus ring, ↓ or F4 open the menu of 画面 / マイク / PC の音声; held keys do not repeat)

    static let tabOrder: [Part] = [.record, .pause, .audioPause, .audio, .format, .pin, .shot, .gear, .screen, .mic, .sys,
                                   .check, .open]

    override func keyDown(with e: NSEvent) {
        if e.isARepeat { return }
        switch Int(e.keyCode) {
        case 48: moveFocus(e.modifierFlags.contains(.shift) ? -1 : 1)   // Tab
        case 49, 36, 76:   // Space, Return, Enter
            if let f = model.focus, model.enabled(f) {
                model.showFocus = model.showFocus || f != .guide
                onPart?(f)
            }
        case 125, 118:   // ↓, F4
            if let f = model.focus, [.screen, .mic, .sys].contains(f), model.enabled(f) {
                model.showFocus = true
                onPart?(f)
            }
        default: super.keyDown(with: e)
        }
    }

    private func moveFocus(_ dir: Int) {
        let order = Self.tabOrder, n = order.count
        var i = model.focus.flatMap { order.firstIndex(of: $0) } ?? (dir > 0 ? n - 1 : 0)
        if model.focus == .guide { i = dir > 0 ? 0 : n - 1 }   // the guide is not in the order: from Pause / Open on
        for _ in 0..<n {
            i = ((i + dir) % n + n) % n
            if model.enabled(order[i]) {
                model.focus = order[i]
                break
            }
        }
        model.showFocus = true
    }

    // MARK: tooltips

    /// The parts with a tooltip (the Windows version's tool rects; しおり: her picture only).
    func installTips() {
        removeAllToolTips()
        tips.removeAll()
        let rects: [(Part, NSRect)] = [(.record, NSRect(x: 24, y: 50, width: 52, height: 52)),
                                       (.audio, NSRect(x: 412, y: 54, width: 44, height: 44)),
                                       (.pause, NSRect(x: 83, y: 59, width: 34, height: 34)),
                                       (.audioPause, NSRect(x: 376, y: 61, width: 30, height: 30)),
                                       (.pin, Self.pin), (.shot, Self.shot), (.gear, Self.gear), (.format, Self.format),
                                       (.guide, Self.guideBox)]
        for (p, r) in rects { tips[addToolTip(r, owner: self, userData: nil)] = p }
    }

    func view(_ view: NSView, stringForToolTip tag: NSView.ToolTipTag, point: NSPoint,
              userData data: UnsafeMutableRawPointer?) -> String {
        guard let p = tips[tag] else { return "" }
        return tipText?(p) ?? ""
    }

    override func resetCursorRects() {
        // the hand over the parts that can be used (refreshed when the model changes the enabled parts)
        for p in [Part.record, .audio, .pause, .audioPause, .pin, .shot, .gear, .check, .open, .format, .screen, .mic, .sys,
                  .guide] where model.enabled(p) {
            addCursorRect(rect(of: p), cursor: .pointingHand)
        }
    }

    func rect(of p: Part) -> NSRect {
        switch p {
        case .record: return NSRect(x: 22, y: 48, width: 56, height: 56)
        case .audio: return NSRect(x: 410, y: 52, width: 48, height: 48)
        case .pause: return NSRect(x: 81, y: 57, width: 38, height: 38)
        case .audioPause: return NSRect(x: 374, y: 59, width: 34, height: 34)
        case .pin: return Self.pin
        case .shot: return Self.shot
        case .gear: return Self.gear
        case .check: return Self.check
        case .open: return Self.open
        case .format: return Self.format
        case .screen: return NSRect(x: 14, y: 120, width: 452, height: 28)
        case .mic: return NSRect(x: 14, y: 156, width: 280, height: 28)
        case .sys: return NSRect(x: 14, y: 192, width: 280, height: 28)
        case .guide: return Self.guideBox
        }
    }

    // MARK: drawing

    override func draw(_ dirtyRect: NSRect) {
        let m = model
        Pal.canvas.setFill()
        bounds.fill()
        text("Rec Cue!", NSRect(x: 14, y: 8, width: 186, height: 28), .systemFont(ofSize: 13, weight: .bold), Pal.text,
             kern: 0.2)
        iconButton(.pin, m.topmost ? "pin.fill" : "pin", accent: m.topmost)
        iconButton(.shot, "camera", flash: m.shotFlash)
        if m.phase == .recording && m.shots > 0 { counter(m.shots) }
        iconButton(.gear, "gearshape")
        deck(m)
        screenRow(m)
        channelRow(.mic, "mic", "マイク", m.mic, m)
        channelRow(.sys, "speaker.wave.2", "PC の音声", m.sys, m)
        pathWell(m)
        bottomButton(.check, m.phase == .checking ? "stop" : "waveform.path.ecg",
                     m.phase == .checking ? "確認を止める" : "音量を確認", toggled: m.phase == .checking)
        bottomButton(.open, "folder", "保存先を開く", toggled: false)
        bubble(m)
        guide(m)
        if m.phase == .recording {
            (m.paused ? Pal.amber : m.audioOnly ? Pal.blue : Pal.crimson).setFill()
            NSRect(x: 0, y: 0, width: 480, height: 2).fill()
        }
    }

    private func focusRing(_ r: NSRect, _ radius: CGFloat) {
        let p = NSBezierPath(roundedRect: r.insetBy(dx: -2, dy: -2), xRadius: radius + 2, yRadius: radius + 2)
        p.lineWidth = 1.5
        Pal.cyan.setStroke()
        p.stroke()
    }

    private func circleRing(_ cx: CGFloat, _ cy: CGFloat, _ r: CGFloat) {
        let p = NSBezierPath(ovalIn: NSRect(x: cx - r, y: cy - r, width: 2 * r, height: 2 * r))
        p.lineWidth = 1.5
        Pal.cyan.setStroke()
        p.stroke()
    }

    private func showsFocus(_ p: Part) -> Bool { model.showFocus && model.focus == p && model.enabled(p) }

    /// A rounded panel: fill, a 1-pt border inside the rect, and a lighter top edge instead of a shadow.
    private func panel(_ r: NSRect, _ radius: CGFloat, _ fill: NSColor, _ border: NSColor, _ edge: NSColor?) {
        let rr = r.insetBy(dx: 0.5, dy: 0.5)
        let p = NSBezierPath(roundedRect: rr, xRadius: radius, yRadius: radius)
        fill.setFill()
        p.fill()
        border.setStroke()
        p.lineWidth = 1
        p.stroke()
        if let edge {
            edge.setStroke()
            let l = NSBezierPath()
            l.move(to: NSPoint(x: r.minX + radius, y: r.minY + 0.5))
            l.line(to: NSPoint(x: r.maxX - radius, y: r.minY + 0.5))
            l.lineWidth = 1
            l.stroke()
        }
    }

    private func disc(_ cx: CGFloat, _ cy: CGFloat, _ r: CGFloat, _ c: NSColor) {
        c.setFill()
        NSBezierPath(ovalIn: NSRect(x: cx - r, y: cy - r, width: 2 * r, height: 2 * r)).fill()
    }

    private func ring(_ cx: CGFloat, _ cy: CGFloat, _ r: CGFloat, _ c: NSColor, _ w: CGFloat = 2) {
        let p = NSBezierPath(ovalIn: NSRect(x: cx - r, y: cy - r, width: 2 * r, height: 2 * r))
        p.lineWidth = w
        c.setStroke()
        p.stroke()
    }

    private func gradientDisc(_ cx: CGFloat, _ cy: CGFloat, _ r: CGFloat, _ top: NSColor, _ bottom: NSColor) {
        let p = NSBezierPath(ovalIn: NSRect(x: cx - r, y: cy - r, width: 2 * r, height: 2 * r))
        NSGradient(starting: top, ending: bottom)?.draw(in: p, angle: 90)   // flipped view: 90° runs top → bottom
    }

    @discardableResult
    func text(_ s: String, _ r: NSRect, _ font: NSFont, _ color: NSColor, align: NSTextAlignment = .left,
              kern: CGFloat = 0) -> CGFloat {
        let a = NSMutableAttributedString(string: s, attributes: [.font: font, .foregroundColor: color])
        if kern != 0 { a.addAttribute(.kern, value: kern, range: NSRange(location: 0, length: max(0, a.length - 1))) }
        return draw(a, r, align: align)
    }

    /// One line, vertically centred, cut with "…"; returns the width of the text as drawn.
    @discardableResult
    func draw(_ a: NSAttributedString, _ r: NSRect, align: NSTextAlignment = .left) -> CGFloat {
        let s = NSMutableAttributedString(attributedString: a)
        let ps = NSMutableParagraphStyle()
        ps.alignment = align
        ps.lineBreakMode = .byTruncatingTail
        s.addAttribute(.paragraphStyle, value: ps, range: NSRange(location: 0, length: s.length))
        var size = NSSize.zero
        Self.guarded(s) {
            size = s.size()
            let h = ceil(size.height)
            s.draw(with: NSRect(x: r.minX, y: r.midY - h / 2, width: r.width, height: h),
                   options: [.usesLineFragmentOrigin, .truncatesLastVisibleLine])
        }
        return min(size.width, r.width)
    }

    private static var textFailures = Set<String>()

    /// Measuring or drawing a text: macOS's text drawing very rarely throws an Objective-C exception there (it once
    /// stopped the app 0.3 s into a recording, Sources/RecCueObjC/DEVELOPMENT.md). Then this one text is left out, and
    /// the log says what it was (once per text), instead of the app — and the recording — ending.
    static func guarded(_ s: NSAttributedString, _ body: () -> Void) {
        guard let why = RCCatch(body) else { return }
        let font = s.length > 0 ? (s.attribute(.font, at: 0, effectiveRange: nil) as? NSFont)?.fontName ?? "?" : "-"
        if textFailures.insert(s.string).inserted {
            Log.event("ui.text", ok: false, msg: "\(why) text=\"\(s.string)\" font=\(font)")
        }
    }

    /// An SF Symbol in one colour (monochrome, like the Windows icon font: palette colours also fill inner layers).
    func symbol(_ name: String, _ r: NSRect, _ size: CGFloat, _ color: NSColor, weight: NSFont.Weight = .regular) {
        let cfg = NSImage.SymbolConfiguration(pointSize: size, weight: weight)
        guard let img = NSImage(systemSymbolName: name, accessibilityDescription: nil)?.withSymbolConfiguration(cfg)
        else { return }
        let s = img.size
        let tinted = NSImage(size: s, flipped: false) { box in
            img.draw(in: box)
            color.set()
            box.fill(using: .sourceAtop)
            return true
        }
        tinted.draw(in: NSRect(x: (r.midX - s.width / 2).rounded(), y: (r.midY - s.height / 2).rounded(), width: s.width,
                               height: s.height), from: .zero, operation: .sourceOver, fraction: 1, respectFlipped: true,
                    hints: nil)
    }

    private func iconButton(_ p: Part, _ glyph: String, accent: Bool = false, flash: Bool = false) {
        let r = rect(of: p), hot = model.hot == p, down = hot && model.pressed == p
        var fill = Pal.control, border = Pal.controlBorder, edge: NSColor? = Pal.edge, ink = Pal.fieldText
        if accent {
            fill = Pal.pinOnFill; border = hot ? Pal.cyan : Pal.pinOnBorder; edge = Pal.pinOnEdge; ink = Pal.cyanText
        } else if flash {
            fill = Pal.flashFill; border = Pal.cyan; edge = nil; ink = Pal.cyan
        } else if down {
            fill = Pal.controlDown; edge = nil
        } else if hot {
            fill = Pal.controlHot; border = Pal.controlBorderHot; ink = Pal.text
        }
        panel(r, 5, fill, border, edge)
        symbol(glyph, r.offsetBy(dx: 0, dy: down ? 1 : 0), 13, ink)
        if showsFocus(p) { focusRing(r, 5) }
    }

    private func counter(_ n: Int) {
        disc(430, 10, 9, Pal.canvas)
        disc(430, 10, 8, Pal.cyanDeep)
        text(n > 99 ? "99" : "\(n)", NSRect(x: 422, y: 2, width: 16, height: 16), Fonts.mono(9.5, .bold), .white,
             align: .center)
    }

    // MARK: deck

    private func deck(_ m: UiModel) {
        let edge = Self.edge(m.split)
        let left = NSRect(x: 14, y: Self.deckTop, width: edge - 14, height: 68)
        let right = NSRect(x: edge + 6, y: Self.deckTop, width: 466 - edge - 6, height: 68)
        let recLive = m.phase == .recording && !m.paused
        let lt = m.screenLive && recLive ? (m.alert ? 3 : 1) : 0
        let rt = m.audioLive && recLive ? (m.alert ? 3 : 2) : 0
        deckPanel(left, lt)
        deckPanel(right, rt)
        NSGraphicsContext.saveGraphicsState()
        NSBezierPath(rect: left.insetBy(dx: 1, dy: 1)).addClip()
        screenSide(m, edge)
        NSGraphicsContext.restoreGraphicsState()
        NSGraphicsContext.saveGraphicsState()
        NSBezierPath(rect: right.insetBy(dx: 1, dy: 1)).addClip()
        audioSide(m, right)
        NSGraphicsContext.restoreGraphicsState()
    }

    private func deckPanel(_ r: NSRect, _ tint: Int) {
        guard r.width > 1 else { return }
        let t = Pal.deck[tint]
        let rr = r.insetBy(dx: 0.5, dy: 0.5)
        let p = NSBezierPath(roundedRect: rr, xRadius: 8, yRadius: 8)
        NSGradient(starting: t.0, ending: t.1)?.draw(in: p, angle: 90)
        t.2.setStroke()
        p.lineWidth = 1
        p.stroke()
        if r.width > 20 {
            let l = NSBezierPath()
            l.move(to: NSPoint(x: r.minX + 8, y: r.minY + 0.5))
            l.line(to: NSPoint(x: r.maxX - 8, y: r.minY + 0.5))
            t.3.setStroke()
            l.stroke()
        }
    }

    private func screenSide(_ m: UiModel, _ edge: CGFloat) {
        recordButton(m)
        let live = m.screenLive
        if m.phase == .idle || m.phase == .checking {
            sideLabel(NSRect(x: 88, y: 42, width: max(0, max(88, edge - 10) - 88), height: 68), "録画",
                      "画面・マイク・PC の音声", key: m.recHotkey, leading: true)
            return
        }
        guard live else { return }
        if m.phase == .starting {
            text(m.timer, NSRect(x: 88, y: 47, width: 134, height: 32), Fonts.mono(26, .bold), Pal.icon)
            status(m, NSRect(x: 88, y: 81, width: 141, height: 22))
            return
        }
        pauseButton(m, cx: 100, r: 17, part: .pause)
        text(m.timer, NSRect(x: 126, y: 47, width: 134, height: 32), Fonts.mono(26, .bold),
             m.paused ? Pal.text3 : Pal.timerLive)
        if m.phase == .recording {
            chip(m.paused ? "PAUSE" : "REC", x: 262)
            shotHint(m, edge)
        }
        status(m, NSRect(x: 126, y: 81, width: edge - 8 - 126, height: 22))
    }

    /// The screenshot shortcut at the top right of a recording, only where it clears the chip (the Windows version's
    /// default text never fits; the Mac's ⌃⌥S does).
    private func shotHint(_ m: UiModel, _ edge: CGFloat) {
        guard !m.shotHotkey.isEmpty else { return }
        let font = Fonts.mono(10), right = edge - 10, clear: CGFloat = 262 + (m.paused ? 66 : 52) + 6
        let tw = Self.width(m.shotHotkey, font)
        guard right - tw >= clear else { return }
        text(m.shotHotkey, NSRect(x: edge - 170, y: 54, width: 160, height: 18), font, Pal.hint, align: .right)
        let x = right - tw - 6
        if x - 14 >= clear { symbol("camera", NSRect(x: x - 14, y: 54, width: 14, height: 18), 9, Pal.hint) }
    }

    private func audioSide(_ m: UiModel, _ right: NSRect) {
        audioButton(m)
        let live = m.audioLive
        if m.phase == .idle || m.phase == .checking {
            let l = min(400, right.minX + 10)
            sideLabel(NSRect(x: l, y: 42, width: 400 - l, height: 68), "録音", "マイクと PC の音声", key: m.audioHotkey,
                      leading: false)
            formatChip(m)
            return
        }
        if !live {   // the screen records: only the grey mic button, and the label while the panel is still wide
            if m.split < 0.99 && m.phase == .starting {
                let l = min(400, right.minX + 10)
                sideLabel(NSRect(x: l, y: 42, width: 400 - l, height: 68), "録音", "マイクと PC の音声", key: m.audioHotkey,
                          leading: false)
            }
            return
        }
        if m.phase == .starting {
            text(m.timer, NSRect(x: 200, y: 47, width: 200, height: 32), Fonts.mono(26, .bold), Pal.icon, align: .right)
            status(m, NSRect(x: max(right.minX + 12, 102), y: 81, width: 404 - max(right.minX + 12, 102), height: 22))
            return
        }
        pauseButton(m, cx: 391, r: 15, part: .audioPause)
        let tr: CGFloat = 366
        let tw = text(m.timer, NSRect(x: tr - 200, y: 47, width: 200, height: 32), Fonts.mono(26, .bold),
                      m.paused ? Pal.text3 : Pal.timerLive, align: .right)
        if m.phase == .recording { chip(m.paused ? "PAUSE" : "AUDIO", x: tr - tw - 10 - 66) }
        let sl = max(right.minX + 12, 102)
        status(m, NSRect(x: sl, y: 81, width: tr + 4 - sl, height: 22))
    }

    private func sideLabel(_ r: NSRect, _ title: String, _ sub: String, key: String, leading: Bool) {
        guard r.width >= 20 else { return }
        let a: NSTextAlignment = leading ? .left : .right
        text(title, NSRect(x: r.minX, y: 50, width: r.width, height: 20), Fonts.jp(14, 6), Pal.text, align: a)
        text(sub, NSRect(x: r.minX, y: 70, width: r.width, height: 16), Fonts.jp(10.5), Pal.text2, align: a)
        if !key.isEmpty { text(key, NSRect(x: r.minX, y: 88, width: r.width, height: 16), Fonts.mono(10), Pal.hint, align: a) }
    }

    private func recordButton(_ m: UiModel) {
        let cx: CGFloat = 50, cy: CGFloat = 76
        let hot = m.hot == .record, down = hot && m.pressed == .record
        let recording = m.screenLive && m.phase == .recording
        disc(cx, cy, 26, m.screenLive && (m.phase == .recording || m.phase == .saving) ? Pal.recOuter : Pal.idleOuter)
        ring(cx, cy, 25, recording ? Pal.crimson : hot && m.enabled(.record) ? Pal.controlBorderHot : Pal.idleRing)
        if recording {
            let h: CGFloat = down ? 9 : 10
            let sq = NSBezierPath(roundedRect: NSRect(x: cx - h, y: cy - h, width: 2 * h, height: 2 * h), xRadius: 4,
                                  yRadius: 4)
            Pal.rose.setFill()
            sq.fill()
            Pal.rosePale.setStroke()
            sq.lineWidth = 1
            sq.stroke()
        } else if m.enabled(.record) {
            let r: CGFloat = down ? 18 : 19
            gradientDisc(cx, cy, r, hot ? Pal.roseLight : Pal.rose, hot ? Pal.rose : Pal.crimson)
            ring(cx, cy, r - 0.5, Pal.roseLight, 1)
            disc(cx, cy, 6, .white)
        } else {
            disc(cx, cy, 17, Pal.busy)
            if m.phase == .saving && m.screenLive {
                Pal.text.withAlphaComponent(0.45).setFill()
                NSBezierPath(roundedRect: NSRect(x: cx - 6, y: cy - 6, width: 12, height: 12), xRadius: 2, yRadius: 2).fill()
            }
        }
        if showsFocus(.record) { circleRing(cx, cy, 29) }
    }

    private func audioButton(_ m: UiModel) {
        let cx: CGFloat = 434, cy: CGFloat = 76
        let hot = m.hot == .audio, down = hot && m.pressed == .audio
        let recording = m.audioLive && m.phase == .recording
        disc(cx, cy, 22, m.audioLive && (m.phase == .recording || m.phase == .saving) ? Pal.blueOuter : Pal.idleOuter)
        ring(cx, cy, 21, recording ? Pal.blue : hot && m.enabled(.audio) ? Pal.controlBorderHot : Pal.idleRing)
        if recording {
            let h: CGFloat = down ? 7.5 : 8.5
            let sq = NSBezierPath(roundedRect: NSRect(x: cx - h, y: cy - h, width: 2 * h, height: 2 * h), xRadius: 3,
                                  yRadius: 3)
            Pal.blueHi.setFill()
            sq.fill()
            Pal.bluePale.setStroke()
            sq.lineWidth = 1
            sq.stroke()
        } else if m.enabled(.audio) {
            let r: CGFloat = down ? 15 : 16
            gradientDisc(cx, cy, r, hot ? Pal.blueText : Pal.blueHi, hot ? Pal.blueHi : Pal.blue)
            ring(cx, cy, r - 0.5, Pal.blueText, 1)
            symbol("mic.fill", NSRect(x: 422, y: down ? 65 : 64, width: 24, height: 24), 13, .white, weight: .medium)
        } else {
            disc(cx, cy, 15, Pal.busy)
            symbol("mic.fill", NSRect(x: 422, y: 64, width: 24, height: 24), 13, Pal.text3, weight: .medium)
        }
        if showsFocus(.audio) { circleRing(cx, cy, 25) }
    }

    private func pauseButton(_ m: UiModel, cx: CGFloat, r: CGFloat, part: Part) {
        let cy: CGFloat = 76, k = r / 17
        let enabled = m.enabled(part), hot = m.hot == part, down = hot && m.pressed == part
        disc(cx, cy, r, m.paused ? Pal.warnBg : Pal.idleOuter)
        ring(cx, cy, r - 1, !enabled ? Pal.offBorder : m.paused ? Pal.amber : hot ? Pal.controlBorderHot : Pal.idleRing)
        let dy: CGFloat = down ? 1 : 0
        if m.paused && m.phase == .recording {
            let t = NSBezierPath()
            t.move(to: NSPoint(x: cx - 4.5 * k, y: cy - 6.5 * k + dy))
            t.line(to: NSPoint(x: cx + 6.5 * k, y: cy + dy))
            t.line(to: NSPoint(x: cx - 4.5 * k, y: cy + 6.5 * k + dy))
            t.close()
            (hot ? NSColor.white : Pal.amberText).setFill()
            t.fill()
        } else {
            (!enabled ? Pal.offText : hot ? Pal.text : Pal.label).setFill()
            for x in [cx - 1.75 * k - 3.5 * k, cx + 1.75 * k] {
                NSBezierPath(roundedRect: NSRect(x: x, y: cy - 6 * k + dy, width: 3.5 * k, height: 12 * k), xRadius: 1,
                             yRadius: 1).fill()
            }
        }
        if showsFocus(part) { circleRing(cx, cy, r + 3) }
    }

    private func chip(_ kind: String, x: CGFloat) {
        let w: CGFloat = kind == "REC" ? 52 : 66
        let r = NSRect(x: x, y: 53, width: w, height: 20)
        let a = 0.35 + 0.65 * model.pulse
        switch kind {
        case "REC":
            panel(r, 4, Pal.recChipBg, Pal.crimson, nil)
            disc(x + 11, 63, 3, Pal.rose.withAlphaComponent(a))
            text("REC", NSRect(x: x + 15, y: 53, width: 34, height: 20), Fonts.mono(11, .bold), Pal.rosePale,
                 align: .center, kern: 0.8)
        case "AUDIO":
            panel(r, 4, Pal.blueChipBg, Pal.blue, nil)
            disc(x + 11, 63, 3, Pal.blueHi.withAlphaComponent(a))
            text("AUDIO", NSRect(x: x + 15, y: 53, width: 48, height: 20), Fonts.mono(11, .bold), Pal.bluePale,
                 align: .center, kern: 0.8)
        default:
            panel(r, 4, Pal.warnBg, Pal.warnBorder, nil)
            Pal.amber.withAlphaComponent(a).setFill()
            NSRect(x: x + 7, y: 59.5, width: 2, height: 7).fill()
            NSRect(x: x + 11, y: 59.5, width: 2, height: 7).fill()
            text("PAUSE", NSRect(x: x + 15, y: 53, width: 48, height: 20), Fonts.mono(11, .bold), Pal.amberText,
                 align: .center, kern: 0.8)
        }
    }

    private func status(_ m: UiModel, _ r: NSRect) {
        guard !m.status.isEmpty, r.width > 10 else { return }
        if m.tone == .normal {
            text(m.status, r, Fonts.jp(11.5), Pal.text2)
            return
        }
        let (fill, border, ink, glyph): (NSColor, NSColor, NSColor, String) = switch m.tone {
        case .good: (Pal.toastBg, Pal.cyanDeep, Pal.cyanText, "checkmark")
        case .warn: (Pal.warnBg, Pal.warnBorder, Pal.amberText, "exclamationmark.triangle")
        default: (Pal.recChipBg, Pal.crimson, Pal.rosePale, "exclamationmark.triangle")
        }
        panel(r, 4, fill, border, nil)
        symbol(glyph, NSRect(x: r.minX + 6, y: r.minY, width: 14, height: r.height), 9, ink)
        text(m.status, NSRect(x: r.minX + 24, y: r.minY, width: r.width - 31, height: r.height),
             Fonts.jp(m.tone == .warn ? 11 : 11.5, 4), ink)
    }

    private func formatChip(_ m: UiModel) {
        let r = Self.format, hot = m.hot == .format, down = hot && m.pressed == .format
        panel(r, 4, down ? Pal.controlDown : hot ? Pal.controlHot : Pal.blueChipBg, hot || down ? Pal.blueHi : Pal.c(0x1F3A5F),
              nil)
        let dy: CGFloat = down ? 1 : 0
        text(m.audioFormat, NSRect(x: 319, y: 51 + dy, width: 32, height: 18), Fonts.mono(11, .bold),
             hot ? Pal.bluePale : Pal.blueText, align: .center)
        symbol("chevron.down", NSRect(x: 349, y: 51 + dy, width: 12, height: 18), 7, hot ? Pal.text2 : Pal.text3,
               weight: .bold)
        if showsFocus(.format) { focusRing(r, 4) }
    }

    // MARK: rows

    private func rowBase(_ p: Part, _ glyph: String, _ label: String, _ y: CGFloat, iconColor: NSColor, labelColor: NSColor) {
        symbol(glyph, NSRect(x: 14, y: y, width: 18, height: 28), 12, iconColor)
        text(label, NSRect(x: 34, y: y, width: 62, height: 28), Fonts.jp(11.5, 6), labelColor)
    }

    private func field(_ p: Part, _ y: CGFloat, warn: Bool, _ a: NSAttributedString) {
        let r = NSRect(x: 98, y: y, width: 196, height: 28)
        let enabled = model.enabled(p), hot = model.hot == p, down = hot && model.pressed == p
        panel(r, 5, down ? Pal.controlDown : hot ? Pal.controlHot : Pal.input,
              warn ? Pal.warnBorder : hot || down ? Pal.controlBorderHot : Pal.inputBorder, warn ? nil : Pal.inputEdge)
        draw(a, NSRect(x: 106, y: y, width: 166, height: 28))
        symbol("chevron.down", NSRect(x: 274, y: y, width: 14, height: 28), 7,
               !enabled ? Pal.offText : hot ? Pal.text2 : Pal.text3, weight: .bold)
        if showsFocus(p) { focusRing(r, 5) }
    }

    private func screenRow(_ m: UiModel) {
        let y: CGFloat = 120
        rowBase(.screen, "desktopcomputer", "画面", y, iconColor: m.screenLive ? Pal.cyan : Pal.icon, labelColor: Pal.label)
        let color = m.screenLive ? Pal.cyanText : m.enabled(.screen) ? Pal.fieldText : Pal.text3
        let font = m.screenLive ? Fonts.jp(11, 6) : Fonts.jp(11)
        field(.screen, y, warn: false, NSAttributedString(string: m.screenText, attributes: [.font: font,
                                                                                            .foregroundColor: color]))
        // the screens as they are arranged: the one recorded in cyan (crimson while it records)
        let box = NSRect(x: 302, y: y, width: 164, height: 28)
        panel(box, 5, Pal.meterBox, Pal.meterBorder, nil)
        guard !m.monitors.isEmpty else { return }
        let u = m.monitors.reduce(m.monitors[0]) { $0.union($1) }
        let k = min(148 / u.width, 23 / u.height)
        let ox = (384 - u.width * k / 2).rounded(), oy = (134 - u.height * k / 2).rounded()
        for (i, mr) in m.monitors.enumerated() {
            let r = NSRect(x: ox + (mr.minX - u.minX) * k, y: oy + (mr.minY - u.minY) * k, width: mr.width * k,
                           height: mr.height * k).insetBy(dx: 0.75, dy: 0.75)
            let sel = i == m.selected, live = m.screenLive && m.phase != .starting
            let p = NSBezierPath(roundedRect: r, xRadius: 1.5, yRadius: 1.5)
            (sel ? (live ? Pal.recChipBg : Pal.pinOnFill) : Pal.control).setFill()
            p.fill()
            (sel ? (live ? Pal.crimson : Pal.cyan) : Pal.controlBorderHot).setStroke()
            p.lineWidth = 1
            p.stroke()
            if r.width >= 9 && r.height >= 9 {
                text("\(i + 1)", r, Fonts.mono(9.5, .bold), sel ? (live ? Pal.rosePale : Pal.cyanText) : Pal.text3,
                     align: .center)
            }
        }
    }

    private func channelRow(_ p: Part, _ glyph: String, _ label: String, _ ch: UiModel.Channel, _ m: UiModel) {
        guard let y = Self.rowY[p] else { return }
        let open = !ch.device.isEmpty
        rowBase(p, glyph, label, y, iconColor: ch.warn ? Pal.amber : open && ch.active ? Pal.cyan : Pal.icon,
                labelColor: ch.warn ? Pal.amber : Pal.label)
        let a = NSMutableAttributedString()
        let base = m.enabled(p) || open ? Pal.fieldText : Pal.text3
        if open {
            let lead = ch.mode.isEmpty ? "" : ch.mode + " → "
            a.append(NSAttributedString(string: lead, attributes: [.font: Fonts.jp(11), .foregroundColor: base]))
            a.append(NSAttributedString(string: ch.device, attributes: [.font: Fonts.jp(11, 6),
                                                                        .foregroundColor: ch.warn ? Pal.amberText : Pal.cyanText]))
        } else {
            a.append(NSAttributedString(string: ch.setting, attributes: [.font: Fonts.jp(11), .foregroundColor: base]))
        }
        field(p, y, warn: ch.warn, a)
        meter(y, ch, open: open)
    }

    private func meter(_ y: CGFloat, _ ch: UiModel.Channel, open: Bool) {
        panel(NSRect(x: 302, y: y, width: 112, height: 28), 5, Pal.meterBox, ch.warn ? Pal.silentBorder : Pal.meterBorder,
              nil)
        let track = NSRect(x: 310, y: y + 8, width: 96, height: 6)
        Pal.meterTrack.setFill()
        NSBezierPath(roundedRect: track, xRadius: 3, yRadius: 3).fill()
        if open && ch.active {
            let w = 96 * max(0, min(1, (ch.db + 60) / 60))
            if w > 0.5 {
                NSGraphicsContext.saveGraphicsState()
                NSBezierPath(roundedRect: NSRect(x: 310, y: y + 8, width: w, height: 6), xRadius: 2, yRadius: 2).addClip()
                NSGradient(colors: [Pal.cyanDeep, Pal.cyan, Pal.teal], atLocations: [0, 0.78, 1],
                           colorSpace: .sRGB)?.draw(in: track, angle: 0)
                NSGraphicsContext.restoreGraphicsState()
            }
            if ch.hold > ch.db && ch.hold > -60 {
                let x = min(406, 310 + 96 * max(0, min(1, (ch.hold + 60) / 60)))
                Pal.peak.setFill()
                NSRect(x: x - 2, y: y + 8, width: 2, height: 6).fill()
            }
        }
        Pal.tick.setFill()
        for i in 0..<7 { NSRect(x: 310 + min(95, CGFloat(16 * i)), y: y + 17, width: 1, height: 2).fill() }
        let dbRect = NSRect(x: 418, y: y, width: 48, height: 28)
        if !open {
            text("—", dbRect, Fonts.mono(11, .semibold), Pal.hint, align: .right)
        } else if ch.db <= -89 {
            if ch.warn {
                panel(NSRect(x: 422, y: y + 3, width: 44, height: 22), 4, Pal.warnBg, Pal.silentBorder, nil)
                text("無音", NSRect(x: 422, y: y, width: 44, height: 28), Fonts.jp(11, 6), Pal.amber, align: .center)
            } else {
                text("無音", dbRect, Fonts.jp(11, 6), Pal.dbLive, align: .center)
            }
        } else {
            let v = Int(ch.db.rounded())
            text(v >= 0 ? "0 dB" : "\u{2212}\(-v) dB", dbRect, Fonts.mono(11, .semibold), Pal.dbLive, align: .right)
        }
    }

    // MARK: path, buttons, bubble, guide

    private func pathWell(_ m: UiModel) {
        let r = NSRect(x: 14, y: 230, width: 386, height: 26)
        panel(r, 5, Pal.well, Pal.wellBorder, nil)
        let ink = m.phase == .recording ? (m.audioOnly ? Pal.blueText : Pal.rosePale) : Pal.icon
        symbol("folder", NSRect(x: 21, y: 230, width: 14, height: 26), 9, ink)
        let bw = text(m.pathLabel, NSRect(x: 40, y: 230, width: 64, height: 26), Fonts.jp(11, 6), ink)
        let a = NSMutableAttributedString(string: m.path, attributes: [.font: Fonts.mono(11), .foregroundColor: Pal.path])
        if let slash = m.path.lastIndex(of: "/"), m.path[slash...].contains(".") {
            let from = m.path.distance(from: m.path.startIndex, to: m.path.index(after: slash))
            a.addAttributes([.font: Fonts.mono(11, .semibold), .foregroundColor: Pal.pathFile],
                            range: NSRange(location: from, length: (m.path as NSString).length - from))
        }
        // keep the file name visible: cut the middle of the path rather than its end
        let s = NSMutableAttributedString(attributedString: a)
        let ps = NSMutableParagraphStyle()
        ps.lineBreakMode = .byTruncatingMiddle
        s.addAttribute(.paragraphStyle, value: ps, range: NSRange(location: 0, length: s.length))
        Self.guarded(s) {
            let h = ceil(s.size().height)
            s.draw(with: NSRect(x: 47 + bw, y: 243 - h / 2, width: 392 - 47 - bw, height: h),
                   options: [.usesLineFragmentOrigin, .truncatesLastVisibleLine])
        }
    }

    private func bottomButton(_ p: Part, _ glyph: String, _ label: String, toggled: Bool) {
        let r = rect(of: p), enabled = model.enabled(p), hot = model.hot == p, down = hot && model.pressed == p
        var fill = Pal.control, border = Pal.controlBorder, edge: NSColor? = Pal.edge, ink = Pal.label
        if !enabled {
            fill = Pal.offFill; border = Pal.offBorder; edge = nil; ink = Pal.offText
        } else if toggled {
            fill = Pal.pinOnFill; border = hot ? Pal.cyan : Pal.pinOnBorder; edge = Pal.pinOnEdge; ink = Pal.cyanText
        } else if down {
            fill = Pal.controlDown; edge = nil
        } else if hot {
            fill = Pal.controlHot; border = Pal.controlBorderHot
        }
        panel(r, 5, fill, border, edge)
        let font = Fonts.jp(11.5, 4)
        let tw = Self.width(label, font)
        let x = (r.midX - (19 + tw) / 2).rounded(), dy: CGFloat = down ? 1 : 0
        symbol(glyph, NSRect(x: x, y: r.minY + dy, width: 13, height: r.height), 11, ink)
        text(label, NSRect(x: x + 19, y: r.minY + dy, width: r.maxX - 4 - x - 19, height: r.height), font, ink)
        if showsFocus(p) { focusRing(r, 5) }
    }

    /// The room for her line in the bubble (after her name), and a line's width: --lines-check measures with these.
    static var commentRoom: CGFloat { 386 - 32 - nameWidth }
    private static var nameWidth: CGFloat { width("しおり", Fonts.jp(10.5, 7)) }
    static func commentWidth(_ s: String) -> CGFloat { width(s, Fonts.jp(11.5)) }
    private static func width(_ s: String, _ font: NSFont) -> CGFloat {
        let a = NSAttributedString(string: s, attributes: [.font: font])
        var w: CGFloat = 0
        guarded(a) { w = a.size().width }
        return w
    }

    private func bubble(_ m: UiModel) {
        guard !m.comment.isEmpty else { return }
        let r = Self.bubble
        let hover = m.hot == .guide
        let (fill, border, ink): (NSColor, NSColor, NSColor) = switch m.commentTone {
        case .good: (Pal.control, Pal.cyanDeep, Pal.cyanText)
        case .warn: (Pal.warnBg, Pal.warnBorder, Pal.amberText)
        case .bad: (Pal.recChipBg, Pal.crimson, Pal.rosePale)
        case .normal: (Pal.control, hover ? Pal.controlBorderHot : Pal.controlBorder, Pal.label)
        }
        panel(r, 8, fill, border, m.commentTone == .normal ? Pal.edge : nil)
        let tail = NSBezierPath()
        tail.move(to: NSPoint(x: 392.5, y: 312))
        tail.line(to: NSPoint(x: 401.5, y: 318))
        tail.line(to: NSPoint(x: 392.5, y: 324))
        tail.close()
        fill.setFill()
        tail.fill()
        let edge = NSBezierPath()
        edge.move(to: NSPoint(x: 393.5, y: 312.5))
        edge.line(to: NSPoint(x: 401.5, y: 318))
        edge.line(to: NSPoint(x: 393.5, y: 323.5))
        edge.lineWidth = 1
        border.setStroke()
        edge.stroke()
        let nw = text("しおり", NSRect(x: 24, y: 304, width: 50, height: 28), Fonts.jp(10.5, 7), Pal.roseLight)
        text(m.comment, NSRect(x: 32 + nw, y: 304, width: 386 - 32 - nw, height: 28), Fonts.jp(11.5), ink)
    }

    private func guide(_ m: UiModel) {
        guard let img = m.guideRec ? guideRecImage : guideIdle, img.size.height > 0 else { return }
        let h: CGFloat = 96, w = h * img.size.width / img.size.height
        let s = window?.backingScaleFactor ?? 2
        let x = ((440 - w / 2) * s).rounded() / s, y = ((338 - h) * s).rounded() / s
        img.draw(in: NSRect(x: x, y: y, width: w, height: h), from: .zero, operation: .sourceOver, fraction: 1,
                 respectFlipped: true, hints: [.interpolation: NSImageInterpolation.high])
    }

    /// The window drawn into a PNG without showing it (the Windows version's --ui-snapshot): `scale` 2 = Retina.
    static func snapshot(_ m: UiModel, scale: CGFloat, idle: NSImage?, rec: NSImage?) -> Data? {
        let v = WindowView(frame: NSRect(origin: .zero, size: size))
        v.model = m
        v.guideIdle = idle
        v.guideRecImage = rec
        guard let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: Int(size.width * scale),
                                         pixelsHigh: Int(size.height * scale), bitsPerSample: 8, samplesPerPixel: 4,
                                         hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB, bytesPerRow: 0,
                                         bitsPerPixel: 0)?.retagging(with: .sRGB) else { return nil }
        rep.size = size
        v.cacheDisplay(in: v.bounds, to: rep)
        return rep.representation(using: .png, properties: [:])
    }
}
