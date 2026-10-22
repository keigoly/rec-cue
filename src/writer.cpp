#include "writer.h"

#include <codecapi.h>
#include <mferror.h>
#include <mftransform.h>

#include <cstring>

namespace rc {

namespace {

std::wstring attrString(IMFAttributes* a, REFGUID key) {
    UINT32 len = 0;
    if (FAILED(a->GetStringLength(key, &len))) return {};
    std::wstring s(len + 1, L'\0');
    if (FAILED(a->GetString(key, s.data(), len + 1, &len))) return {};
    s.resize(len);
    return s;
}

#define TRY(step, expr)                                                         \
    do {                                                                        \
        HRESULT hr_ = (expr);                                                   \
        if (FAILED(hr_)) {                                                      \
            Log::event("writer.open", false, sw.ms(), std::string(step) + (d3d ? " (gpu)" : " (sysmem)"), hr_); \
            return hr_;                                                         \
        }                                                                       \
    } while (0)

}  // namespace

Writer::~Writer() { close(); }

HRESULT Writer::open(const WriterParams& p, ID3D11Device* dev, ID3D11DeviceContext* ctx) {
    dev_ = Com<ID3D11Device>::from(dev);
    ctx_ = Com<ID3D11DeviceContext>::from(ctx);
    w_ = p.width;
    h_ = p.height;
    HRESULT hr = E_FAIL;
    if (p.video && p.d3d && dev) {
        hr = openOnce(p, true);
        if (FAILED(hr)) close();
    }
    if (FAILED(hr)) hr = openOnce(p, false);
    if (FAILED(hr)) close();
    return hr;
}

HRESULT Writer::openOnce(const WriterParams& p, bool d3d) {
    Stopwatch sw;
    d3d_ = d3d;
    video_ = p.video;
    finalized_ = false;
    fragmented_ = p.fragmented;
    path_ = p.path;
    Com<IMFAttributes> a;
    TRY("attributes", MFCreateAttributes(a.put(), 6));
    a->SetGUID(MF_TRANSCODE_CONTAINERTYPE, p.fragmented ? MFTranscodeContainerType_FMPEG4 : MFTranscodeContainerType_MPEG4);
    a->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, p.hardware ? TRUE : FALSE);
    a->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, TRUE);
    if (d3d) {
        UINT token = 0;
        TRY("device manager", MFCreateDXGIDeviceManager(&token, mgr_.put()));
        TRY("device manager reset", mgr_->ResetDevice(dev_.get(), token));
        a->SetUnknown(MF_SINK_WRITER_D3D_MANAGER, mgr_.get());
    }
    TRY("create file", MFCreateSinkWriterFromURL(p.path.c_str(), nullptr, a.get(), sw_.put()));
    if (p.video) TRY("video", openVideo(p, sw));

    Com<IMFMediaType> ao;
    TRY("audio type", MFCreateMediaType(ao.put()));
    ao->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    ao->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
    ao->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    ao->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 48000);
    ao->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
    ao->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, p.audioBytesPerSec);
    ao->SetUINT32(MF_MT_AAC_PAYLOAD_TYPE, 0);
    ao->SetUINT32(MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION, 0x29);
    TRY("add audio stream", sw_->AddStream(ao.get(), &as_));

    Com<IMFMediaType> ai;
    TRY("audio input type", MFCreateMediaType(ai.put()));
    ai->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    ai->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    ai->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    ai->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 48000);
    ai->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
    ai->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 4);
    ai->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 48000 * 4);
    ai->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
    TRY("set audio input", sw_->SetInputMediaType(as_, ai.get(), nullptr));

    TRY("begin writing", sw_->BeginWriting());

    if (d3d) {
        TRY("sample pool", MFCreateVideoSampleAllocatorEx(__uuidof(IMFVideoSampleAllocatorEx), alloc_.put_void()));
        TRY("sample pool device", alloc_->SetDirectXManager(mgr_.get()));
        Com<IMFAttributes> aa;
        TRY("sample pool attributes", MFCreateAttributes(aa.put(), 2));
        aa->SetUINT32(MF_SA_D3D11_BINDFLAGS, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
        aa->SetUINT32(MF_SA_D3D11_USAGE, D3D11_USAGE_DEFAULT);
        TRY("sample pool init", alloc_->InitializeSampleAllocatorEx(2, 4, aa.get(), videoIn_.get()));  // 8 MB each at 1080p
    }
    describe();
    if (p.video)
        Log::event("writer.open", true, sw.ms(),
                   fmt("%s %ux%u@%u %u kbps / aac %u kbps / %s / %s", d3d ? "gpu" : "sysmem", w_, h_, p.fps,
                       p.videoBitrate / 1000, p.audioBytesPerSec * 8 / 1000, p.fragmented ? "fmp4" : "mp4",
                       pipeline.c_str()));
    else
        Log::event("writer.open", true, sw.ms(),
                   fmt("audio only / aac %u kbps / %s / %s", p.audioBytesPerSec * 8 / 1000, p.fragmented ? "fmp4" : "mp4",
                       pipeline.c_str()));
    return S_OK;
}

// H.264 output and BGRA input of the video stream (only when recording the screen).
HRESULT Writer::openVideo(const WriterParams& p, const Stopwatch& sw) {
    const bool d3d = d3d_;
    Com<IMFMediaType> vo;
    TRY("video type", MFCreateMediaType(vo.put()));
    vo->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    vo->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    vo->SetUINT32(MF_MT_AVG_BITRATE, p.videoBitrate);
    vo->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    vo->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_High);
    MFSetAttributeSize(vo.get(), MF_MT_FRAME_SIZE, w_, h_);
    MFSetAttributeRatio(vo.get(), MF_MT_FRAME_RATE, p.fps, 1);
    MFSetAttributeRatio(vo.get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    vo->SetUINT32(MF_MT_YUV_MATRIX, MFVideoTransferMatrix_BT709);
    vo->SetUINT32(MF_MT_VIDEO_PRIMARIES, MFVideoPrimaries_BT709);
    vo->SetUINT32(MF_MT_TRANSFER_FUNCTION, MFVideoTransFunc_709);
    vo->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_16_235);
    TRY("add video stream", sw_->AddStream(vo.get(), &vs_));

    TRY("video input type", MFCreateMediaType(videoIn_.put()));
    videoIn_->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    videoIn_->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_ARGB32);
    videoIn_->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    MFSetAttributeSize(videoIn_.get(), MF_MT_FRAME_SIZE, w_, h_);
    MFSetAttributeRatio(videoIn_.get(), MF_MT_FRAME_RATE, p.fps, 1);
    MFSetAttributeRatio(videoIn_.get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    videoIn_->SetUINT32(MF_MT_DEFAULT_STRIDE, w_ * 4);
    videoIn_->SetUINT32(MF_MT_FIXED_SIZE_SAMPLES, TRUE);
    videoIn_->SetUINT32(MF_MT_SAMPLE_SIZE, w_ * h_ * 4);
    videoIn_->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
    videoIn_->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_0_255);
    Com<IMFAttributes> enc;
    TRY("encoder params", MFCreateAttributes(enc.put(), 3));
    enc->SetUINT32(CODECAPI_AVEncCommonRateControlMode, eAVEncCommonRateControlMode_UnconstrainedVBR);
    enc->SetUINT32(CODECAPI_AVEncCommonMeanBitRate, p.videoBitrate);
    enc->SetUINT32(CODECAPI_AVEncMPVGOPSize, p.fps * 2);
    HRESULT hr = sw_->SetInputMediaType(vs_, videoIn_.get(), enc.get());
    if (FAILED(hr)) {
        Log::event("writer.video_params", false, sw.ms(), "encoder rejected the rate-control settings, using its defaults", hr);
        hr = sw_->SetInputMediaType(vs_, videoIn_.get(), nullptr);
    }
    TRY("set video input", hr);
    return S_OK;
}

void Writer::describe() {
    Com<IMFSinkWriterEx> ex;
    if (FAILED(sw_.as(ex))) {
        pipeline = "(transforms unknown)";
        return;
    }
    std::string s;
    const DWORD streams[2] = {vs_, as_};
    for (int k = 0; k < 2; ++k) {
        if (k == 0 && !video_) continue;
        s += k == 0 ? "video:" : video_ ? " | audio:" : "audio:";
        for (DWORD i = 0;; ++i) {
            GUID cat{};
            Com<IMFTransform> t;
            if (FAILED(ex->GetTransformForStream(streams[k], i, &cat, t.put()))) break;
            Com<IMFAttributes> ta;
            std::wstring name;
            bool hw = false;
            if (SUCCEEDED(t->GetAttributes(ta.put())) && ta) {
                name = attrString(ta.get(), MFT_FRIENDLY_NAME_Attribute);
                hw = !attrString(ta.get(), MFT_ENUM_HARDWARE_URL_Attribute).empty();
            }
            if (name.empty()) {
                if (cat == MFT_CATEGORY_VIDEO_ENCODER) name = L"video encoder";
                else if (cat == MFT_CATEGORY_VIDEO_PROCESSOR) name = L"video processor";
                else if (cat == MFT_CATEGORY_AUDIO_ENCODER) name = L"audio encoder";
                else name = L"transform";
            }
            s += std::string(i ? " > " : " ") + u8(name) + (hw ? " [hw]" : "");
            if (k == 0 && cat == MFT_CATEGORY_VIDEO_ENCODER) encoderIsHardware = hw;
        }
    }
    pipeline = s;
}

HRESULT Writer::writeVideo(ID3D11Texture2D* src, int64_t t100ns, int64_t dur100ns) {
    std::lock_guard<std::mutex> lk(mu_);
    if (!sw_ || finalized_ || !video_) return E_UNEXPECTED;
    const int64_t t0 = qpcNow();
    Com<IMFSample> s;
    HRESULT hr;
    if (d3d_) {
        hr = alloc_->AllocateSample(s.put());
        if (hr == MF_E_SAMPLEALLOCATOR_EMPTY) {
            videoDrops++;
            return S_FALSE;
        }
        if (SUCCEEDED(hr)) {
            Com<IMFMediaBuffer> b;
            Com<IMFDXGIBuffer> db;
            Com<ID3D11Texture2D> tex;
            UINT sub = 0;
            hr = s->GetBufferByIndex(0, b.put());
            if (SUCCEEDED(hr)) hr = b.as(db);
            if (SUCCEEDED(hr)) hr = db->GetResource(__uuidof(ID3D11Texture2D), tex.put_void());
            if (SUCCEEDED(hr)) {
                db->GetSubresourceIndex(&sub);
                ctx_->CopySubresourceRegion(tex.get(), sub, 0, 0, 0, src, 0, nullptr);
                Com<IMF2DBuffer> b2;
                if (SUCCEEDED(b.as(b2))) {
                    DWORD len = 0;
                    if (SUCCEEDED(b2->GetContiguousLength(&len))) b->SetCurrentLength(len);
                }
            }
        }
    } else {
        hr = S_OK;
        if (!staging_) {
            D3D11_TEXTURE2D_DESC d{};
            src->GetDesc(&d);
            d.Usage = D3D11_USAGE_STAGING;
            d.BindFlags = 0;
            d.MiscFlags = 0;
            d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            hr = dev_->CreateTexture2D(&d, nullptr, staging_.put());
        }
        Com<IMFMediaBuffer> b;
        if (SUCCEEDED(hr)) hr = MFCreateMemoryBuffer(w_ * h_ * 4, b.put());
        if (SUCCEEDED(hr)) {
            ctx_->CopyResource(staging_.get(), src);
            D3D11_MAPPED_SUBRESOURCE m{};
            hr = ctx_->Map(staging_.get(), 0, D3D11_MAP_READ, 0, &m);
            if (SUCCEEDED(hr)) {
                BYTE* dst = nullptr;
                hr = b->Lock(&dst, nullptr, nullptr);
                if (SUCCEEDED(hr)) {
                    for (UINT y = 0; y < h_; ++y)
                        memcpy(dst + static_cast<size_t>(y) * w_ * 4, static_cast<BYTE*>(m.pData) + static_cast<size_t>(y) * m.RowPitch, w_ * 4);
                    b->Unlock();
                    b->SetCurrentLength(w_ * h_ * 4);
                }
                ctx_->Unmap(staging_.get(), 0);
            }
        }
        if (SUCCEEDED(hr)) hr = MFCreateSample(s.put());
        if (SUCCEEDED(hr)) hr = s->AddBuffer(b.get());
    }
    if (SUCCEEDED(hr)) hr = s->SetSampleTime(t100ns);
    if (SUCCEEDED(hr)) hr = s->SetSampleDuration(dur100ns);
    if (SUCCEEDED(hr)) hr = sw_->WriteSample(vs_, s.get());
    if (FAILED(hr)) {
        writeErrors++;
        if (hr != lastErr_) Log::event("writer.video", false, -1, "video sample was not written", hr);
        lastErr_ = hr;
        return hr;
    }
    videoSamples++;
    const int64_t us = static_cast<int64_t>(qpcMs(qpcNow() - t0) * 1000.0);
    if (us > maxVideoWriteUs.load()) maxVideoWriteUs.store(us);
    return S_OK;
}

HRESULT Writer::writeAudio(const int16_t* pcm, uint32_t frames, int64_t t100ns) {
    std::lock_guard<std::mutex> lk(mu_);
    if (!sw_ || finalized_) return E_UNEXPECTED;
    const DWORD bytes = frames * 4;
    Com<IMFMediaBuffer> b;
    Com<IMFSample> s;
    HRESULT hr = MFCreateMemoryBuffer(bytes, b.put());
    if (SUCCEEDED(hr)) {
        BYTE* d = nullptr;
        hr = b->Lock(&d, nullptr, nullptr);
        if (SUCCEEDED(hr)) {
            memcpy(d, pcm, bytes);
            b->Unlock();
            b->SetCurrentLength(bytes);
        }
    }
    if (SUCCEEDED(hr)) hr = MFCreateSample(s.put());
    if (SUCCEEDED(hr)) hr = s->AddBuffer(b.get());
    if (SUCCEEDED(hr)) hr = s->SetSampleTime(t100ns);
    if (SUCCEEDED(hr)) hr = s->SetSampleDuration(static_cast<int64_t>(frames) * 10000000LL / 48000);
    if (SUCCEEDED(hr)) hr = sw_->WriteSample(as_, s.get());
    if (FAILED(hr)) {
        writeErrors++;
        if (hr != lastErr_) Log::event("writer.audio", false, -1, "audio sample was not written", hr);
        lastErr_ = hr;
        return hr;
    }
    audioSamples++;
    return S_OK;
}

HRESULT Writer::finalize(double* ms) {
    std::lock_guard<std::mutex> lk(mu_);
    if (!sw_ || finalized_) return S_FALSE;
    Stopwatch sw;
    HRESULT hr = sw_->Finalize();
    finalized_ = true;
    if (SUCCEEDED(hr) && fragmented_) patchTrackDurations();
    if (ms) *ms = sw.ms();
    Log::event("writer.finalize", SUCCEEDED(hr), sw.ms(),
               fmt("video=%llu drops=%llu audio=%llu errors=%llu", static_cast<unsigned long long>(videoSamples.load()),
                   static_cast<unsigned long long>(videoDrops.load()), static_cast<unsigned long long>(audioSamples.load()),
                   static_cast<unsigned long long>(writeErrors.load())),
               hr);
    return hr;
}

// The fragmented-MP4 sink fills in the movie duration (mvhd) at Finalize but leaves the track headers
// (tkhd / mdhd) at 0, and macOS AVFoundation (QuickTime, Kanary) then reports a 0-second file.
// Writing the movie duration into those fixed-size fields in place fixes it without moving any bytes.
void Writer::patchTrackDurations() {
    Stopwatch sw;
    durationPatched = false;
    // Another process (indexer, antivirus) may hold the new file for a moment: retry for about 1 s.
    HANDLE h = INVALID_HANDLE_VALUE;
    DWORD openErr = 0;
    for (int i = 0; i < 10 && h == INVALID_HANDLE_VALUE; ++i) {
        h = CreateFileW(path_.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            openErr = GetLastError();
            Sleep(100);
        }
    }
    if (h == INVALID_HANDLE_VALUE) {
        Log::event("writer.patch", false, sw.ms(), "cannot open the file (Mac players may show 0 s)", HRESULT_FROM_WIN32(openErr));
        return;
    }
    std::vector<uint8_t> b(65536);
    DWORD got = 0;
    if (!ReadFile(h, b.data(), static_cast<DWORD>(b.size()), &got, nullptr)) got = 0;
    b.resize(got);
    auto be32 = [&](size_t o) { return (uint32_t(b[o]) << 24) | (uint32_t(b[o + 1]) << 16) | (uint32_t(b[o + 2]) << 8) | b[o + 3]; };
    auto be64 = [&](size_t o) { return (uint64_t(be32(o)) << 32) | be32(o + 4); };
    auto put32 = [&](size_t o, uint32_t v) { b[o] = uint8_t(v >> 24); b[o + 1] = uint8_t(v >> 16); b[o + 2] = uint8_t(v >> 8); b[o + 3] = uint8_t(v); };
    auto put64 = [&](size_t o, uint64_t v) { put32(o, uint32_t(v >> 32)); put32(o + 4, uint32_t(v)); };
    struct Box { char type[5]; size_t pos, size, body; };
    auto children = [&](size_t pos, size_t end) {
        std::vector<Box> v;
        while (end >= 8 && pos <= end - 8) {
            uint64_t size = be32(pos);
            size_t hdr = 8;
            if (size == 1) {
                if (end - pos < 16) break;
                size = be64(pos + 8);
                hdr = 16;
            }
            if (size < hdr || size > end - pos) break;
            Box x{};
            memcpy(x.type, &b[pos + 4], 4);
            x.pos = pos;
            x.size = static_cast<size_t>(size);
            x.body = pos + hdr;
            v.push_back(x);
            pos += static_cast<size_t>(size);
        }
        return v;
    };
    auto find = [&](const std::vector<Box>& v, const char* t) -> const Box* {
        for (auto& x : v) if (memcmp(x.type, t, 4) == 0) return &x;
        return nullptr;
    };
    // The body must hold `need` bytes after the box header.
    auto fits = [](const Box* x, size_t need) { return x && x->pos + x->size >= x->body + need; };
    std::string result = "moov not found in the first 64 KB";
    bool ok = false;
    const std::vector<Box> top = children(0, b.size());
    const Box* moov = find(top, "moov");
    if (moov) {
        const std::vector<Box> kids = children(moov->body, moov->pos + moov->size);
        const Box* mvhd = find(kids, "mvhd");
        if (fits(mvhd, 4) && fits(mvhd, b[mvhd->body] == 1 ? 32 : 20)) {
            const size_t m = mvhd->body;
            const bool v1 = b[m] == 1;
            const uint32_t ts = v1 ? be32(m + 20) : be32(m + 12);
            const uint64_t du = v1 ? be64(m + 24) : be32(m + 16);
            int tracks = 0;
            for (auto& k : kids) {
                if (memcmp(k.type, "trak", 4) != 0) continue;
                const std::vector<Box> tk = children(k.body, k.pos + k.size);
                const Box* tkhd = find(tk, "tkhd");
                if (fits(tkhd, 4)) {
                    const size_t o = tkhd->body;
                    if (b[o] == 1 && fits(tkhd, 36)) put64(o + 28, du);
                    else if (b[o] != 1 && fits(tkhd, 24)) put32(o + 20, static_cast<uint32_t>(du));
                }
                if (const Box* mdia = find(tk, "mdia")) {
                    const std::vector<Box> md = children(mdia->body, mdia->pos + mdia->size);
                    const Box* mdhd = find(md, "mdhd");
                    if (fits(mdhd, 4) && fits(mdhd, b[mdhd->body] == 1 ? 32 : 20)) {
                        const size_t o = mdhd->body;
                        const bool w1 = b[o] == 1;
                        const uint32_t mts = w1 ? be32(o + 20) : be32(o + 12);
                        const uint64_t d = ts ? du * mts / ts : 0;
                        if (w1) put64(o + 24, d); else put32(o + 16, static_cast<uint32_t>(d));
                    }
                }
                tracks++;
            }
            if (ts && du && tracks) {
                LARGE_INTEGER at;
                at.QuadPart = static_cast<LONGLONG>(moov->pos);
                DWORD w = 0;
                ok = SetFilePointerEx(h, at, nullptr, FILE_BEGIN) &&
                     WriteFile(h, &b[moov->pos], static_cast<DWORD>(moov->size), &w, nullptr) && w == moov->size;
                result = fmt("%d tracks set to %.2f s", tracks, static_cast<double>(du) / ts);
            } else {
                result = fmt("nothing to patch (timescale=%u duration=%llu tracks=%d)", ts, static_cast<unsigned long long>(du), tracks);
            }
        }
    }
    CloseHandle(h);
    durationPatched = ok;
    Log::event("writer.patch", ok, sw.ms(), result);
}

void Writer::close() {
    std::lock_guard<std::mutex> lk(mu_);
    if (alloc_) alloc_->UninitializeSampleAllocator();
    alloc_.reset();
    sw_.reset();
    videoIn_.reset();
    staging_.reset();
    mgr_.reset();
}

}  // namespace rc
