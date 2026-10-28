import AppKit
import Carbon.HIToolbox

/// The shortcuts that work in any app (the Windows version's RegisterHotKey): 録画 (hotkey), 録音 (audio_hotkey) and
/// スクショ (shot_hotkey) in config.ini. Carbon's RegisterEventHotKey needs no accessibility permission. A combination
/// another app has taken cannot be registered, and the window says so.
@MainActor
final class Hotkeys {
    enum Action: UInt32, CaseIterable { case record = 1, audio = 2, shot = 3 }

    /// One shortcut: as config.ini writes it, how the Mac shows it ("⌃⌥⇧R"), and whether it works.
    struct Key {
        var setting = ""
        var text = ""          // "" when none is set (or it cannot be read)
        var registered = false
        var mods: UInt32 = 0   // Carbon modifiers
        var code: UInt32 = 0   // virtual key code
        var problem: String?   // "taken" (another app has it) / "bad" (not understood)

        /// The text for menus and tooltips (the Windows version's keyText): the key, なし, or that another app has it.
        var shown: String {
            if registered { return text }
            if problem == "taken" { return text + " (ほかのアプリが使用中)" }
            return "なし"
        }
    }

    private(set) var keys: [Action: Key] = [:]
    private var refs: [EventHotKeyRef] = []
    private var handler: EventHandlerRef?
    private let onPress: (Action) -> Void

    init(onPress: @escaping (Action) -> Void) {
        self.onPress = onPress
        var spec = EventTypeSpec(eventClass: OSType(kEventClassKeyboard), eventKind: UInt32(kEventHotKeyPressed))
        let me = Unmanaged.passUnretained(self).toOpaque()
        InstallEventHandler(GetApplicationEventTarget(), { _, event, user in
            var id = EventHotKeyID()
            GetEventParameter(event, EventParamName(kEventParamDirectObject), EventParamType(typeEventHotKeyID), nil,
                              MemoryLayout<EventHotKeyID>.size, nil, &id)
            guard let user, let action = Action(rawValue: id.id) else { return OSStatus(eventNotHandledErr) }
            let hk = Unmanaged<Hotkeys>.fromOpaque(user).takeUnretainedValue()
            MainActor.assumeIsolated { hk.onPress(action) }
            return noErr
        }, 1, &spec, me, &handler)
    }

    /// (Re)registers the three from config.ini.
    func register(_ cfg: Config) {
        for r in refs { UnregisterEventHotKey(r) }
        refs.removeAll()
        for a in Action.allCases {
            let setting = a == .record ? cfg.hotkey : a == .audio ? cfg.audioHotkey : cfg.shotHotkey
            var k = Self.parse(setting)
            if !k.text.isEmpty {
                var ref: EventHotKeyRef?
                let id = EventHotKeyID(signature: OSType(0x5243_5545), id: a.rawValue)   // 'RCUE'
                // exclusive: a combination another app holds is refused (as Windows refuses it), not shared
                let st = RegisterEventHotKey(k.code, k.mods, id, GetApplicationEventTarget(),
                                             OptionBits(kEventHotKeyExclusive), &ref)
                if st == noErr, let ref {
                    refs.append(ref)
                    k.registered = true
                } else {
                    k.problem = "taken"
                }
                Log.event("ui.hotkey", ok: k.registered, msg: "\(a) \(setting) → \(k.text)" + (st == noErr ? "" : " (\(st))"))
            } else if k.problem == "bad" {
                Log.event("ui.hotkey", ok: false, msg: "\(a) \(setting): not understood (Ctrl / Option / Shift / Cmd + "
                          + "A–Z, 0–9 or F1–F20)")
            }
            keys[a] = k
        }
    }

    func text(_ a: Action) -> String { keys[a].map { $0.registered ? $0.text : "" } ?? "" }

    /// The autotest: as if the shortcut of `a` were pressed — the same hot key event, sent to the app itself.
    static func simulate(_ a: Action) {
        var ev: EventRef?
        guard CreateEvent(nil, OSType(kEventClassKeyboard), UInt32(kEventHotKeyPressed), GetCurrentEventTime(),
                          EventAttributes(kEventAttributeNone), &ev) == noErr, let ev else { return }
        var id = EventHotKeyID(signature: OSType(0x5243_5545), id: a.rawValue)
        SetEventParameter(ev, EventParamName(kEventParamDirectObject), EventParamType(typeEventHotKeyID),
                          MemoryLayout<EventHotKeyID>.size, &id)
        SendEventToEventTarget(ev, GetApplicationEventTarget())
        ReleaseEvent(ev)
    }

    /// --hotkey-check: how settings are read, and whether config.ini's three can be registered now (they are let go
    /// again when the check ends).
    static func check() -> J {
        let samples = ["Ctrl+Option+R", "Ctrl+Option+Shift+R", "ctrl+alt+shift+a", "Control+Command+S", "⌃⌥⇧R", "Cmd+F5", "F13",
                       "none", "", "R", "Ctrl+Hyper+R", "Ctrl+Shift+F", "Shift+Ä"]
        let parsed: [J] = samples.map { s in
            let k = parse(s)
            return .obj([("setting", .str(s)), ("text", .str(k.text)), ("problem", .s(k.problem))])
        }
        let hk = Hotkeys { _ in }
        hk.register(Config.load().0)
        let reg: [J] = Action.allCases.map { a in
            let k = hk.keys[a] ?? Key()
            return .obj([("action", .str("\(a)")), ("setting", .str(k.setting)), ("text", .str(k.text)),
                         ("registered", .bool(k.registered)), ("shown", .str(k.shown))])
        }
        hk.register(Config())   // the defaults … then nothing: let them go
        for r in hk.refs { UnregisterEventHotKey(r) }
        hk.refs.removeAll()
        let ok = parsed.count == samples.count && reg.allSatisfy { $0["registered"]?.isTrue == true }
        return .obj([("ok", .bool(ok)), ("parsed", .arr(parsed)), ("config", .arr(reg))])
    }

    /// "Ctrl+Option+Shift+R" (or Alt for Option, Command / Cmd, ⌃⌥⇧⌘ themselves) → the Carbon key and "⌃⌥⇧R";
    /// none / off / empty → no shortcut.
    static func parse(_ setting: String) -> Key {
        var k = Key()
        k.setting = setting
        let t = setting.trimmingCharacters(in: .whitespaces)
        if t.isEmpty || ["none", "off"].contains(t.lowercased()) { return k }
        var parts = t.split(separator: "+").map { $0.trimmingCharacters(in: .whitespaces) }
        // "⌃⌥⇧R" written without +: the symbols are modifiers
        if parts.count == 1, let last = parts.first, last.count > 1, "⌃⌥⇧⌘".contains(last.first!) {
            parts = last.dropLast().map { String($0) } + [String(last.last!)]
        }
        guard let keyName = parts.last?.uppercased() else { return k }
        var mods: UInt32 = 0
        for p in parts.dropLast() {
            switch p.lowercased() {
            case "ctrl", "control", "⌃": mods |= UInt32(controlKey)
            case "alt", "option", "opt", "⌥": mods |= UInt32(optionKey)
            case "shift", "⇧": mods |= UInt32(shiftKey)
            case "cmd", "command", "⌘": mods |= UInt32(cmdKey)
            default:
                k.problem = "bad"
                return k
            }
        }
        // a letter or digit alone would take that key from every app: only F1–F20 may go without a modifier
        guard let code = keyCodes[keyName], mods != 0 || (keyName.count > 1 && keyName.hasPrefix("F")) else {
            k.problem = "bad"
            return k
        }
        k.mods = mods
        k.code = code
        var s = ""
        if mods & UInt32(controlKey) != 0 { s += "⌃" }
        if mods & UInt32(optionKey) != 0 { s += "⌥" }
        if mods & UInt32(shiftKey) != 0 { s += "⇧" }
        if mods & UInt32(cmdKey) != 0 { s += "⌘" }
        k.text = s + keyName
        return k
    }

    /// A–Z, 0–9 (by their place on the keyboard, as macOS numbers them) and F1–F20.
    private static let keyCodes: [String: UInt32] = {
        var m: [String: UInt32] = [:]
        let letters: [(String, Int)] = [
            ("A", kVK_ANSI_A), ("B", kVK_ANSI_B), ("C", kVK_ANSI_C), ("D", kVK_ANSI_D), ("E", kVK_ANSI_E),
            ("F", kVK_ANSI_F), ("G", kVK_ANSI_G), ("H", kVK_ANSI_H), ("I", kVK_ANSI_I), ("J", kVK_ANSI_J),
            ("K", kVK_ANSI_K), ("L", kVK_ANSI_L), ("M", kVK_ANSI_M), ("N", kVK_ANSI_N), ("O", kVK_ANSI_O),
            ("P", kVK_ANSI_P), ("Q", kVK_ANSI_Q), ("R", kVK_ANSI_R), ("S", kVK_ANSI_S), ("T", kVK_ANSI_T),
            ("U", kVK_ANSI_U), ("V", kVK_ANSI_V), ("W", kVK_ANSI_W), ("X", kVK_ANSI_X), ("Y", kVK_ANSI_Y),
            ("Z", kVK_ANSI_Z), ("0", kVK_ANSI_0), ("1", kVK_ANSI_1), ("2", kVK_ANSI_2), ("3", kVK_ANSI_3),
            ("4", kVK_ANSI_4), ("5", kVK_ANSI_5), ("6", kVK_ANSI_6), ("7", kVK_ANSI_7), ("8", kVK_ANSI_8),
            ("9", kVK_ANSI_9),
            ("F1", kVK_F1), ("F2", kVK_F2), ("F3", kVK_F3), ("F4", kVK_F4), ("F5", kVK_F5), ("F6", kVK_F6),
            ("F7", kVK_F7), ("F8", kVK_F8), ("F9", kVK_F9), ("F10", kVK_F10), ("F11", kVK_F11), ("F12", kVK_F12),
            ("F13", kVK_F13), ("F14", kVK_F14), ("F15", kVK_F15), ("F16", kVK_F16), ("F17", kVK_F17),
            ("F18", kVK_F18), ("F19", kVK_F19), ("F20", kVK_F20),
        ]
        for (n, c) in letters { m[n] = UInt32(c) }
        return m
    }()
}
