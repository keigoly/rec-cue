import Foundation

/// ログを取得 (the Windows version's logpack.cpp): the recent logs, the record of each recording and the settings in one
/// ZIP, to hand over as it is (attached to a mail or a chat). The files are stored, not compressed: the logs of a week
/// are a few MB, and the ZIP is made the same way on both systems.
enum LogPack {
    static let days = 7                 // the logs and records of the last week
    static let fileLimit = 64 << 20     // a single file larger than this is left out
    static let totalLimit = 256 << 20   // ... and so is everything past this (the ZIP stays well under 4 GB)
    /// The ZIP being written: quitting waits for it for a while (the Windows version's waitPacker).
    static let running = DispatchGroup()

    struct Result: Sendable {
        var files = 0      // put in the ZIP (about.json included)
        var skipped = 0    // left out: unreadable, or past the size limits
        var bytes = 0      // the ZIP's size
        var ms = 0.0
        var error = ""     // empty when the ZIP was written

        func json(_ zip: URL) -> J {
            .obj([("ok", .bool(error.isEmpty)), ("files", .i(files)), ("skipped", .i(skipped)), ("bytes", .i(bytes)),
                  ("ms", .n(ms)), ("path", .str(zip.path)), ("error", .str(error))])
        }
    }

    /// The user's Downloads (Rec Cue!'s own folder when there is none).
    static var downloads: URL {
        FileManager.default.urls(for: .downloadsDirectory, in: .userDomainMask).first ?? Paths.appSupport
    }

    /// about.json: what made the ZIP.
    static func about() -> String {
        J.obj([("app", .str("Rec Cue!")), ("version", .str(App.version)), ("platform", .str("macOS")),
               ("macos", .str(App.os)), ("days", .i(days)), ("exported_at", .str(Log.nowIso()))]).text
    }

    /// "Rec Cue ログ 2026-10-30 101500.zip" in `dir`, with " (2)" … when that name is taken.
    static func freeName(_ dir: URL) -> URL {
        let base = "Rec Cue ログ \(Paths.stamp())"
        var url = dir.appendingPathComponent(base + ".zip")
        var n = 2
        while FileManager.default.fileExists(atPath: url.path) && n < 100 {
            url = dir.appendingPathComponent("\(base) (\(n)).zip")
            n += 1
        }
        return url
    }

    /// Writes `zip` (through a .tmp beside it): logs/rec-cue-*.log and sessions/*.json changed in the last `days` days,
    /// config.ini, and `about` as about.json. A plain ZIP: each file stored (method 0) with its CRC, its UTF-8 name
    /// (flag bit 11) and its time in DOS form, then the central directory. One file at a time is held in memory; the
    /// log of today is read as far as it is written then.
    static func write(_ zip: URL, days: Int, about: String) -> Result {
        let sw = Stopwatch()
        var r = Result()
        let since = Date().addingTimeInterval(-Double(days) * 86400)
        let tmp = URL(fileURLWithPath: zip.path + ".tmp")
        let fd = Darwin.open(tmp.path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0o644)
        guard fd >= 0 else {
            r.error = "cannot create the ZIP (\(String(cString: strerror(errno))))"
            r.ms = sw.ms
            return r
        }
        let h = FileHandle(fileDescriptor: fd, closeOnDealloc: false)
        var failed: String?   // the first write that failed
        var offset = 0, total = 0
        var central = Data()
        func put(_ d: Data) {
            if failed == nil {
                do { try h.write(contentsOf: d) } catch { failed = error.localizedDescription }
            }
            offset += d.count
        }
        func add(_ name: String, _ data: Data, _ written: Date) {
            let (date, time) = dos(written)
            let crc = crc32(data), size = UInt32(data.count), at = UInt32(offset), n = Data(name.utf8)
            var head = Data()
            head.u32(0x0403_4B50)   // local file header
            head.u16(20)            // version needed: 2.0
            head.u16(0x0800)        // the name is UTF-8
            head.u16(0)             // stored
            head.u16(time)
            head.u16(date)
            head.u32(crc)
            head.u32(size)
            head.u32(size)
            head.u16(UInt16(n.count))
            head.u16(0)
            head.append(n)
            central.u32(0x0201_4B50)   // its entry in the central directory
            central.u16(20)            // made by: MS-DOS / 2.0
            central.u16(20)
            central.u16(0x0800)
            central.u16(0)
            central.u16(time)
            central.u16(date)
            central.u32(crc)
            central.u32(size)
            central.u32(size)
            central.u16(UInt16(n.count))
            central.u16(0)   // extra
            central.u16(0)   // comment
            central.u16(0)   // disk
            central.u16(0)   // internal attributes
            central.u32(0)   // external attributes
            central.u32(at)
            central.append(n)
            put(head)
            put(data)
            r.files += 1
        }
        func addFile(_ url: URL, _ name: String) {
            let v = try? url.resourceValues(forKeys: [.fileSizeKey, .contentModificationDateKey])
            guard let size = v?.fileSize, size <= fileLimit, total + size <= totalLimit,
                  let data = try? Data(contentsOf: url), data.count <= fileLimit,
                  total + data.count <= totalLimit else {
                r.skipped += 1
                return
            }
            total += data.count
            add(name, data, v?.contentModificationDate ?? Date())
        }
        for u in changed(Paths.logs, since)
        where u.lastPathComponent.hasPrefix("rec-cue-") && u.pathExtension == "log" {
            addFile(u, "logs/" + u.lastPathComponent)
        }
        for u in changed(Paths.sessions, since) where u.pathExtension == "json" {
            addFile(u, "sessions/" + u.lastPathComponent)
        }
        addFile(Config.url, "config.ini")
        add("about.json", Data(about.utf8), Date())

        let entries = UInt16(min(r.files, 0xFFFF)), start = UInt32(offset)
        var end = Data()
        end.u32(0x0605_4B50)   // end of the central directory
        end.u16(0)
        end.u16(0)
        end.u16(entries)
        end.u16(entries)
        end.u32(UInt32(central.count))
        end.u32(start)
        end.u16(0)
        put(central)
        put(end)
        if close(fd) != 0 && failed == nil { failed = String(cString: strerror(errno)) }
        if let failed {
            r.error = "cannot write the ZIP (\(failed))"
        } else if rename(tmp.path, zip.path) != 0 {
            r.error = "cannot rename the ZIP (\(String(cString: strerror(errno))))"
        }
        if !r.error.isEmpty { unlink(tmp.path) }
        r.bytes = r.error.isEmpty ? offset : 0
        r.ms = sw.ms
        return r
    }

    /// The regular files in `dir` changed since `since`, by name.
    private static func changed(_ dir: URL, _ since: Date) -> [URL] {
        let keys: Set<URLResourceKey> = [.isRegularFileKey, .contentModificationDateKey]
        let all = (try? FileManager.default.contentsOfDirectory(at: dir, includingPropertiesForKeys: Array(keys))) ?? []
        return all.filter { u in
            let v = try? u.resourceValues(forKeys: keys)
            return v?.isRegularFile == true && (v?.contentModificationDate ?? .distantPast) >= since
        }.sorted { $0.lastPathComponent < $1.lastPathComponent }
    }

    /// A time as a ZIP keeps it: the local date and time in MS-DOS form (1980-01-01 when it cannot be told).
    private static func dos(_ d: Date) -> (date: UInt16, time: UInt16) {
        let c = Calendar(identifier: .gregorian).dateComponents(in: .current, from: d)
        guard let y = c.year, (1980...2107).contains(y), let mo = c.month, let da = c.day, let h = c.hour,
              let mi = c.minute, let s = c.second else { return (0x21, 0) }
        return (UInt16((y - 1980) << 9 | mo << 5 | da), UInt16(h << 11 | mi << 5 | s / 2))
    }

    private static let table: [UInt32] = (0..<256).map { i in
        var c = UInt32(i)
        for _ in 0..<8 { c = c & 1 != 0 ? 0xEDB8_8320 ^ (c >> 1) : c >> 1 }
        return c
    }

    static func crc32(_ d: Data) -> UInt32 {
        var c: UInt32 = 0xFFFF_FFFF
        table.withUnsafeBufferPointer { t in
            d.withUnsafeBytes { bytes in
                for b in bytes { c = t[Int((c ^ UInt32(b)) & 0xFF)] ^ (c >> 8) }
            }
        }
        return c ^ 0xFFFF_FFFF
    }
}

private extension Data {
    /// Little-endian, as a ZIP is written.
    mutating func u16(_ v: UInt16) {
        append(UInt8(v & 0xFF))
        append(UInt8(v >> 8))
    }

    mutating func u32(_ v: UInt32) {
        u16(UInt16(v & 0xFFFF))
        u16(UInt16(v >> 16))
    }
}
