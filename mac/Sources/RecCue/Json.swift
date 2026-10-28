import Foundation

/// A JSON value with its keys in a fixed order, written the way the Windows version writes its log lines and results.
indirect enum J {
    case obj([(String, J)])
    case arr([J])
    case str(String)
    case int(Int64)
    case num(Double, Int)   // value, digits after the point
    case bool(Bool)
    case null

    static func n(_ v: Double, _ digits: Int = 1) -> J { .num(v, digits) }
    static func i<T: BinaryInteger>(_ v: T) -> J { .int(Int64(v)) }
    static func s(_ v: String?) -> J { v.map { .str($0) } ?? .null }

    var text: String {
        switch self {
        case .obj(let kv): return "{" + kv.map { J.quote($0.0) + ":" + $0.1.text }.joined(separator: ",") + "}"
        case .arr(let a): return "[" + a.map { $0.text }.joined(separator: ",") + "]"
        case .str(let s): return J.quote(s)
        case .int(let v): return String(v)
        case .num(let v, let d): return v.isFinite ? String(format: "%.\(d)f", v) : "null"
        case .bool(let b): return b ? "true" : "false"
        case .null: return "null"
        }
    }

    /// The value of a key of an object (for reading back our own results).
    subscript(key: String) -> J? {
        if case .obj(let kv) = self { return kv.first { $0.0 == key }?.1 }
        return nil
    }

    var isTrue: Bool { if case .bool(true) = self { return true }; return false }

    static func quote(_ s: String) -> String {
        var o = "\""
        for u in s.unicodeScalars {
            switch u {
            case "\"": o += "\\\""
            case "\\": o += "\\\\"
            case "\n": o += "\\n"
            case "\r": o += "\\r"
            case "\t": o += "\\t"
            default:
                if u.value < 0x20 { o += String(format: "\\u%04x", u.value) } else { o.unicodeScalars.append(u) }
            }
        }
        return o + "\""
    }
}
