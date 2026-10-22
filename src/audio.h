// Rec Cue! — audio: WASAPI microphone + loopback ("the other party"), placed on the QPC timeline and mixed.
#pragma once

#include <condition_variable>
#include <functional>
#include <memory>
#include <thread>

#include "common.h"
#include "config.h"

struct IMMDeviceEnumerator;

namespace rc {

struct AudioDevice {
    std::wstring id, name;
    bool render = false;
    bool defConsole = false;   // Windows default device
    bool defComm = false;      // default communications device
    struct App { std::wstring exe; bool active = false; };
    std::vector<App> apps;     // render only: programs with an audio session on this device
};
std::vector<AudioDevice> listAudioDevices(bool render, std::string* err = nullptr, bool withApps = false);
// spec: default | communications | none | auto | part of a name, several separated by |
// mic auto = headset > (if the headset is held by a mixer) a mixer output that carries sound > communications
//            default > default > other physical microphones; virtual devices are used only when not silent.
// auto = the default speaker plus every playback device some program has an audio session on.
std::vector<AudioDevice> resolveDevices(const std::wstring& spec, bool render, std::vector<std::string>* notes);
// Device names drop the "3- " style instance prefix Windows adds after a driver reinstall, for matching.
std::wstring normName(const std::wstring& s);
std::vector<AudioDevice> autoSystemDevices();
// Opens every active microphone and every playback device (loopback) at once for `seconds` and
// reports the level each one carries. Used to find where the user's voice and the other party are routed.
std::string probeLevels(double seconds);

class AudioCapture;

// Self-test helper: plays 1 kHz bursts into one playback device at given times after qpcStart.
// Used with a playback device nobody listens to (e.g. a virtual cable) so the test is silent in the room.
class TonePlayer {
public:
    ~TonePlayer();
    bool start(const std::wstring& deviceSpec, int64_t qpcStart, std::vector<double> times, double burstS, std::string* err);
    void stop();
    std::wstring device;

private:
    std::thread th_;
    std::atomic<bool> stop_{false};
};

class AudioEngine {
public:
    struct Meter {
        std::atomic<int> peakDb10{-900};   // dBFS x10 over the last 100 ms
        std::atomic<int> rmsDb10{-900};
        std::atomic<int> silentMs{0};      // how long the level has stayed below -55 dBFS
        std::atomic<bool> active{false};   // a device is open for this meter
    };
    using Sink = std::function<void(const int16_t* pcm, uint32_t frames, int64_t t100ns)>;

    AudioEngine();
    ~AudioEngine();
    // Opens the devices on the engine thread and waits for the result.
    void open(const Config& c, std::vector<std::string>* notes);
    // Starts streaming. Position 0 of the output = qpcStart. sink may be empty (level meters only).
    void go(int64_t qpcStart, Sink sink);
    // Mixes up to qpcStop (waits for late packets), then stops. Blocks.
    void stopAt(int64_t qpcStop);
    void stopNow();
    bool running() const { return running_.load(); }

    Meter mic, sys;
    std::wstring micName() const;
    std::wstring sysName() const;
    std::string windowJson();    // per-source levels since the previous call (for the 10 s stats line)
    std::string summaryJson();   // whole-session numbers per source
    uint64_t mixedFrames() const { return static_cast<uint64_t>(mixed_.load()); }
    uint64_t clipped() const { return clipped_.load(); }

private:
    void run();
    AudioCapture* addSource(IMMDeviceEnumerator* en, const AudioDevice& d, bool isMic, std::vector<std::string>* notes);
    // Runs beside the engine while streaming: finds new playback devices (auto) and a replacement microphone
    // after the current one is unplugged. The engine thread opens what it finds (no slow COM calls on it).
    void helperLoop();
    void takePending(IMMDeviceEnumerator* en);
    void reapDead();
    bool chooseAutoMic(IMMDeviceEnumerator* en, AudioDevice* out, std::vector<std::string>* notes);
    void mix(uint32_t n);
    void finishMeters(bool isMic, float peak, double ss, uint32_t n);

    Config cfg_;
    std::vector<std::unique_ptr<AudioCapture>> src_;
    std::thread th_;
    std::mutex mu_;
    std::condition_variable cv_;
    bool opened_ = false, goSignal_ = false;
    std::vector<std::string> openNotes_;
    Sink sink_;
    int64_t start100_ = 0;
    std::atomic<int64_t> stopFrames_{-1};
    std::atomic<bool> stopNow_{false}, running_{false};
    std::atomic<int64_t> mixed_{0};
    std::atomic<uint64_t> clipped_{0};
    int64_t latencyFrames_ = 7200;
    std::vector<float> acc_;
    std::vector<int16_t> out_;
    int64_t outStart_ = 0;
    // meter windows (100 ms)
    float mPk_[2] = {0, 0};
    double mSs_[2] = {0, 0};
    uint32_t mN_[2] = {0, 0};
    int64_t silentRun_[2] = {0, 0};
    std::thread helper_;
    std::mutex pendMu_;
    std::condition_variable helperCv_;
    std::vector<AudioDevice> pendingSys_, pendingMic_;
    std::atomic<bool> helperStop_{false}, micDead_{false};
    mutable std::mutex statsMu_;
    std::wstring micName_, sysName_;
    bool autoSys_ = false;
    int sysCount_ = 0;
};

}  // namespace rc
