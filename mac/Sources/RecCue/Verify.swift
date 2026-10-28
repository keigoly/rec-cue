import AVFoundation

/// `--verify-file`: reads every sample of every track back and reports the same fields as the Windows version
/// (samples, first / last time, the longest step between two samples, size), plus the top-level MP4 boxes so a file
/// written in fragments can be told from one that is not.
enum Verify {
    static func file(_ url: URL, audioOnly: Bool) async -> J {
        let sw = Stopwatch()
        guard FileManager.default.fileExists(atPath: url.path) else {
            return .obj([("ok", .bool(false)), ("error", .str("no such file"))])
        }
        let asset = AVURLAsset(url: url)
        let duration = (try? await asset.load(.duration).seconds) ?? -1
        let tracks: [AVAssetTrack]
        do {
            tracks = try await asset.load(.tracks)
        } catch {
            return .obj([("ok", .bool(false)), ("error", .str("open: \(error.localizedDescription)")),
                         ("boxes", boxes(url))])
        }
        var streams: [J] = []
        var readErrors = 0
        var hasVideo = false, hasAudio = false
        for tr in tracks {
            let kind = tr.mediaType == .video ? "video" : tr.mediaType == .audio ? "audio" : "other"
            var ts: [Double] = []
            var bytes = 0
            do {
                let r = try AVAssetReader(asset: asset)
                let out = AVAssetReaderTrackOutput(track: tr, outputSettings: nil)
                out.alwaysCopiesSampleData = false
                r.add(out)
                if r.startReading() {
                    while let sb = out.copyNextSampleBuffer() {
                        bytes += CMSampleBufferGetTotalSampleSize(sb)
                        times(sb, into: &ts)
                    }
                }
                if r.status == .failed { readErrors += 1 }
            } catch {
                readErrors += 1
            }
            ts.sort { $0 < $1 }
            var gap = 0.0
            if ts.count > 1 { for k in 1..<ts.count { gap = max(gap, ts[k] - ts[k - 1]) } }
            if kind == "video" && !ts.isEmpty { hasVideo = true }
            if kind == "audio" && !ts.isEmpty { hasAudio = true }
            streams.append(.obj([("kind", .str(kind)), ("samples", .i(ts.count)), ("first_s", .n(ts.first ?? 0, 3)),
                                 ("last_s", .n(ts.last ?? 0, 3)), ("max_gap_s", .n(gap, 3)),
                                 ("kbytes", .n(Double(bytes) / 1024.0, 0))]))
        }
        let ok = readErrors == 0 && hasAudio && (hasVideo || audioOnly)
        let mp4 = url.pathExtension.lowercased() != "wav"   // WAV is RIFF, not MP4 boxes
        return .obj([("ok", .bool(ok)), ("ms", .n(sw.ms, 0)), ("duration_s", .n(duration, 2)),
                     ("read_errors", .i(readErrors)), ("streams", .arr(streams)), ("boxes", mp4 ? boxes(url) : .null)])
    }

    /// Presentation times of every sample in the buffer (an AAC buffer holds many packets with one timing entry).
    /// Buffers that only carry a marker (no samples, or no time) are not samples.
    private static func times(_ sb: CMSampleBuffer, into ts: inout [Double]) {
        let n = CMSampleBufferGetNumSamples(sb)
        guard n > 0 else { return }
        var count: CMItemCount = 0
        CMSampleBufferGetSampleTimingInfoArray(sb, entryCount: 0, arrayToFill: nil, entriesNeededOut: &count)
        guard count > 0 else {
            let p = CMSampleBufferGetPresentationTimeStamp(sb)
            if p.isNumeric { ts.append(p.seconds) }
            return
        }
        var infos = [CMSampleTimingInfo](repeating: CMSampleTimingInfo(), count: count)
        CMSampleBufferGetSampleTimingInfoArray(sb, entryCount: count, arrayToFill: &infos, entriesNeededOut: &count)
        if count == n {
            for i in infos where i.presentationTimeStamp.isNumeric { ts.append(i.presentationTimeStamp.seconds) }
        } else if infos[0].presentationTimeStamp.isNumeric {
            let p = infos[0].presentationTimeStamp.seconds
            let d = infos[0].duration.isNumeric ? infos[0].duration.seconds : 0
            for i in 0..<n { ts.append(p + Double(i) * d) }
        }
    }

    /// Counts of the top-level boxes (ftyp / moov / moof / mdat / mfra ...) and whether the last one is cut short.
    static func boxes(_ url: URL) -> J {
        guard let h = try? FileHandle(forReadingFrom: url) else { return .null }
        defer { try? h.close() }
        let size = (try? h.seekToEnd()) ?? 0
        var off: UInt64 = 0
        var counts: [(String, Int)] = []
        var truncated = false
        var n = 0
        while off + 8 <= size && n < 1_000_000 {
            guard (try? h.seek(toOffset: off)) != nil, let hd = try? h.read(upToCount: 16), hd.count >= 8 else { break }
            let b = [UInt8](hd)
            var len = b[0..<4].reduce(UInt64(0)) { $0 << 8 | UInt64($1) }
            let type = String(bytes: b[4..<8], encoding: .ascii) ?? "????"
            if len == 1, b.count >= 16 {
                len = b[8..<16].reduce(UInt64(0)) { $0 << 8 | UInt64($1) }
            } else if len == 0 {
                len = size - off
            }
            if let k = counts.firstIndex(where: { $0.0 == type }) { counts[k].1 += 1 } else { counts.append((type, 1)) }
            if len < 8 || off + len > size {
                truncated = true
                break
            }
            off += len
            n += 1
        }
        return .obj(counts.map { ($0.0, J.i($0.1)) } + [("truncated", .bool(truncated))])
    }
}
