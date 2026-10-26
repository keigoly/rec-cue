// Rec Cue! — one recording session: screen + audio -> writer, with 10 s stats lines and a summary JSON.
#pragma once

#include <dxgi1_4.h>

#include <memory>
#include <thread>

#include "audio.h"
#include "common.h"
#include "config.h"
#include "video.h"
#include "writer.h"

namespace rc {

struct RecordOptions {
    bool testPattern = false;   // synthetic picture instead of the screen (self-test, no screen content)
    bool audioOnly = false;     // microphone + PC audio only (no screen), as audio_format says: .mp3 / .wav / .m4a
    double maxSeconds = 0;      // 0 = until stop()
    std::wstring outPath;       // empty = <out_dir>\<prefix> <start time>.mp4 (.mp3 / .wav / .m4a when audio only);
                                // for audio only its extension, when it is one of those, decides the format
};

class Recorder {
public:
    ~Recorder();
    bool start(const Config& c, const RecordOptions& o, std::wstring* err);
    void stop();                                // finalize the file; blocks (≈ latency + finalize)
    bool active() const { return active_.load(); }
    bool loopEnded() const { return loopDone_.load(); }   // max duration reached or auto-stop
    std::string autoStopReason() const;
    double elapsedSec() const { return static_cast<double>(elapsed100_.load()) / 1e7; }
    std::wstring path() const { return path_; }
    std::string summary() const { return summary_; }
    bool lastOk() const { return lastOk_; }
    bool durationPatched() const { return patched_; }   // false: Mac players may show 0 s
    bool videoCapturing() const { return videoOk_.load(); }
    bool audioOnly() const { return opt_.audioOnly; }
    int64_t qpcStart() const { return qpcStart_; }
    // A screenshot taken during this recording, `atS` seconds in (listed in the summary).
    void noteShot(const std::wstring& path, double atS);
    // Pause / resume. Nothing is recorded in between, and the file goes on without a gap: the paused time is left
    // out of the picture and the sound alike. false: not recording, already so, or the file is being finished.
    bool pause();
    bool resume();
    bool paused() const { return pausedNow_.load(); }
    double pausedSec() const;   // every pause so far, the current one included
    int pauses() const;

    AudioEngine audio;

private:
    void videoLoop();
    void audioLoop();
    void logStats(int64_t now, bool final);
    void writeSummary(HRESULT finHr, double finMs);
    // false while `qpc` lies in a pause; otherwise *shift = the paused QPC ticks before it (taken off the timeline)
    bool liveAt(int64_t qpc, int64_t* shift) const;

    Config cfg_;
    RecordOptions opt_;
    AudioFile audioFile_ = AudioFile::M4a;   // audio only
    std::unique_ptr<VideoSource> video_;
    std::unique_ptr<Writer> writer_;
    std::thread vt_;
    std::string sid_, startedIso_;
    std::wstring path_;
    int64_t qpcStart_ = 0, start100_ = 0, stopQpc_ = 0;
    std::atomic<bool> active_{false}, stop_{false}, loopDone_{false}, videoOk_{false};
    std::atomic<int64_t> elapsed100_{0};
    mutable std::mutex reasonMu_;
    std::string autoStop_;
    std::mutex shotsMu_;
    std::string shots_;   // JSON objects separated by commas
    mutable std::mutex pauseMu_;
    std::vector<std::pair<int64_t, int64_t>> pauses_;   // [from, to) in QPC; to = INT64_MAX while paused
    std::atomic<bool> pausedNow_{false};
    std::string summary_;
    bool lastOk_ = false;
    bool patched_ = true;
    // stats
    uint64_t ticks_ = 0, lateTicks_ = 0;
    double maxLateMs_ = 0;
    int64_t lastCpu100_ = 0, lastWall_ = 0;
    double cpuSum_ = 0, wsPeakMb_ = 0;
    int cpuN_ = 0;
    std::string lastSystem_;
    Com<IDXGIAdapter3> gpu_;   // for this process's video memory in the stats line
};

// Reads the finished file back with the Media Foundation source reader (no decoding) and reports what it holds.
// ok needs an audio stream, and a video stream too unless `audioOnly`.
std::string verifyFile(const std::wstring& path, bool audioOnly = false);
// Decodes the audio track and finds where 1 kHz bursts start (self-test for the loopback path and the timeline).
std::string analyzeTones(const std::wstring& path, const std::vector<double>& expected);

}  // namespace rc
