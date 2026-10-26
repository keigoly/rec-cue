// Rec Cue! — Media Foundation sink writer: H.264 (GPU encoder when available) + AAC into MP4 / fragmented MP4.
#pragma once

#include <d3d11.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include "common.h"

namespace rc {

// The file of a recording of sound alone (a recording of the screen is always an MP4 with H.264 and AAC):
// AAC in fragmented MP4, MP3 (Windows' own encoder, Windows 8+), or 16-bit PCM WAVE written directly.
enum class AudioFile { M4a, Mp3, Wav };
AudioFile audioFileOf(const std::wstring& s);   // "mp3" / "wav" / "m4a" (also ".mp3"...); M4a otherwise
const wchar_t* audioFileExt(AudioFile f);       // L".mp3"
const char* audioFileName(AudioFile f);         // "mp3"

struct WriterParams {
    std::wstring path;
    UINT width = 0, height = 0, fps = 15;
    UINT videoBitrate = 2000000;
    UINT audioBytesPerSec = 16000;   // 16000 = 128 kbps AAC / MP3
    bool fragmented = true;          // fMP4 survives a crash up to the last fragment
    bool hardware = true;            // allow hardware MFTs
    bool d3d = true;                 // hand GPU textures to the encoder (no CPU copy)
    bool video = true;               // false: audio only, into `audioFile`
    AudioFile audioFile = AudioFile::M4a;
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
    bool durationPatched = false;   // fMP4: track durations written back after Finalize (MP3 / WAV: nothing to patch)
    bool full() const { return full_.load(); }   // WAV: the most a WAVE file can hold is written (the recorder stops)

    std::atomic<uint64_t> videoSamples{0}, videoDrops{0}, audioSamples{0}, writeErrors{0};
    std::atomic<int64_t> maxVideoWriteUs{0};

private:
    HRESULT openOnce(const WriterParams& p, bool d3d);
    HRESULT openVideo(const WriterParams& p, const Stopwatch& sw);
    HRESULT openWav(const WriterParams& p);
    bool wavHeader();   // the RIFF header with the sizes written so far
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
    HANDLE wav_ = INVALID_HANDLE_VALUE;   // WAV: written here instead of by the sink writer
    uint64_t wavBytes_ = 0;               // PCM bytes in it
    int64_t wavSyncAt_ = 0;               // when its header is next brought up to date (QPC)
    std::atomic<bool> full_{false};
};

}  // namespace rc
