// Rec Cue! — Media Foundation sink writer: H.264 (GPU encoder when available) + AAC into MP4 / fragmented MP4.
#pragma once

#include <d3d11.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include "common.h"

namespace rc {

struct WriterParams {
    std::wstring path;
    UINT width = 0, height = 0, fps = 15;
    UINT videoBitrate = 2000000;
    UINT audioBytesPerSec = 16000;   // 16000 = 128 kbps AAC
    bool fragmented = true;          // fMP4 survives a crash up to the last fragment
    bool hardware = true;            // allow hardware MFTs
    bool d3d = true;                 // hand GPU textures to the encoder (no CPU copy)
    bool video = true;               // false: audio only (an .m4a)
};

class Writer {
public:
    ~Writer();
    HRESULT open(const WriterParams& p, ID3D11Device* dev, ID3D11DeviceContext* ctx);
    // Copies src (BGRA) into a pooled sample and submits it. S_FALSE = dropped (encoder backlog).
    HRESULT writeVideo(ID3D11Texture2D* src, int64_t t100ns, int64_t dur100ns);
    HRESULT writeAudio(const int16_t* pcm, uint32_t frames, int64_t t100ns);
    HRESULT finalize(double* ms);
    void close();

    std::string pipeline;     // e.g. "video: Video Processor MFT > Intel Quick Sync H.264 Encoder MFT [hw]"
    bool usesD3D() const { return d3d_; }
    bool encoderIsHardware = false;
    bool durationPatched = false;   // fMP4 only: track durations written back after Finalize

    std::atomic<uint64_t> videoSamples{0}, videoDrops{0}, audioSamples{0}, writeErrors{0};
    std::atomic<int64_t> maxVideoWriteUs{0};

private:
    HRESULT openOnce(const WriterParams& p, bool d3d);
    HRESULT openVideo(const WriterParams& p, const Stopwatch& sw);
    void describe();
    void patchTrackDurations();

    Com<IMFSinkWriter> sw_;
    DWORD vs_ = 0, as_ = 0;
    Com<IMFDXGIDeviceManager> mgr_;
    Com<IMFVideoSampleAllocatorEx> alloc_;
    Com<ID3D11Device> dev_;
    Com<ID3D11DeviceContext> ctx_;
    Com<ID3D11Texture2D> staging_;
    Com<IMFMediaType> videoIn_;
    std::mutex mu_;
    bool d3d_ = false;
    bool video_ = true;
    bool finalized_ = false;
    bool fragmented_ = false;
    std::wstring path_;
    UINT w_ = 0, h_ = 0;
    HRESULT lastErr_ = S_OK;
};

}  // namespace rc
