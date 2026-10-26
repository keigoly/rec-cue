#include "recorder.h"

#include <mfapi.h>
#include <mferror.h>
#include <mfreadwrite.h>
#include <psapi.h>

#include <algorithm>
#include <cmath>
#include <map>

namespace rc {

namespace {

int64_t ft100(const FILETIME& f) { return (static_cast<int64_t>(f.dwHighDateTime) << 32) | f.dwLowDateTime; }

uint64_t fileBytes(const std::wstring& p) {
    WIN32_FILE_ATTRIBUTE_DATA a{};
    if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &a)) return 0;
    return (static_cast<uint64_t>(a.nFileSizeHigh) << 32) | a.nFileSizeLow;
}

std::wstring parentOf(const std::wstring& p) {
    size_t k = p.find_last_of(L"\\/");
    return k == std::wstring::npos ? L"." : p.substr(0, k);
}

uint64_t freeBytes(const std::wstring& dir) {
    ULARGE_INTEGER f{};
    if (!GetDiskFreeSpaceExW(dir.c_str(), &f, nullptr, nullptr)) return UINT64_MAX;
    return f.QuadPart;
}

std::string systemJson() {
    PERFORMANCE_INFORMATION pi{};
    pi.cb = sizeof(pi);
    if (!GetPerformanceInfo(&pi, sizeof(pi))) return "{}";
    const double mb = static_cast<double>(pi.PageSize) / (1024.0 * 1024.0);
    return fmt("{\"avail_mb\":%.0f,\"commit_mb\":%.0f,\"commit_limit_mb\":%.0f,\"nonpaged_mb\":%.0f,\"paged_mb\":%.0f}",
               static_cast<double>(pi.PhysicalAvailable) * mb, static_cast<double>(pi.CommitTotal) * mb,
               static_cast<double>(pi.CommitLimit) * mb, static_cast<double>(pi.KernelNonpaged) * mb,
               static_cast<double>(pi.KernelPaged) * mb);
}

// Where this process's memory is, for the 10 s stats line (2026-10-23: a recording from the window grew 2-4 MB/s
// while --record stayed flat). heap: every heap's committed / allocated bytes (new / malloc and Windows components);
// priv / mapped: every committed private / mapped page, the private ones by allocation (VirtualAlloc or heap segment)
// with the sizes holding the most; gpu: this process's video memory; and the GDI / USER / kernel handle counts.
std::string memoryJson(IDXGIAdapter3* gpu) {
    Stopwatch sw;
    constexpr double kMb = 1048576.0;
    HANDLE heaps[128];
    const DWORD nh = std::min<DWORD>(GetProcessHeaps(128, heaps), 128);
    double heapMb = 0, usedMb = 0;
    for (DWORD i = 0; i < nh; ++i) {
        HEAP_SUMMARY hs{};
        hs.cb = sizeof(hs);
        if (!HeapSummary(heaps[i], 0, &hs)) continue;
        heapMb += static_cast<double>(hs.cbCommitted) / kMb;
        usedMb += static_cast<double>(hs.cbAllocated) / kMb;
    }
    uint64_t priv = 0, mapped = 0, bucketN[4] = {}, bucketB[4] = {};   // allocations < 1 MB, 1-4, 4-16, >= 16 MB
    std::map<uint64_t, uint64_t> sizes;                                 // 64 KB units -> allocations (>= 64 KB)
    auto done = [&](uint64_t b) {
        if (!b) return;
        const int k = b < (1ull << 20) ? 0 : b < (4ull << 20) ? 1 : b < (16ull << 20) ? 2 : 3;
        bucketN[k]++;
        bucketB[k] += b;
        if (b >= (64ull << 10)) sizes[(b + (32ull << 10)) >> 16]++;
    };
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    const BYTE* p = static_cast<const BYTE*>(si.lpMinimumApplicationAddress);
    const BYTE* end = static_cast<const BYTE*>(si.lpMaximumApplicationAddress);
    const void* base = nullptr;
    uint64_t cur = 0;
    MEMORY_BASIC_INFORMATION mbi;
    while (p < end && VirtualQuery(p, &mbi, sizeof(mbi)) == sizeof(mbi)) {
        if (mbi.AllocationBase != base) {
            done(cur);
            cur = 0;
            base = mbi.AllocationBase;
        }
        if (mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE) {
            priv += mbi.RegionSize;
            cur += mbi.RegionSize;
        } else if (mbi.State == MEM_COMMIT && mbi.Type == MEM_MAPPED) {
            mapped += mbi.RegionSize;
        }
        p = static_cast<const BYTE*>(mbi.BaseAddress) + mbi.RegionSize;
    }
    done(cur);
    std::vector<std::pair<uint64_t, uint64_t>> top(sizes.begin(), sizes.end());
    std::sort(top.begin(), top.end(), [](const auto& a, const auto& b) { return a.first * a.second > b.first * b.second; });
    std::string t;
    for (size_t i = 0; i < top.size() && i < 3; ++i)
        t += fmt("%s{\"kb\":%llu,\"n\":%llu}", i ? "," : "", static_cast<unsigned long long>(top[i].first * 64),
                 static_cast<unsigned long long>(top[i].second));
    double local = -1, shared = -1;
    if (gpu) {
        DXGI_QUERY_VIDEO_MEMORY_INFO vi{};
        if (SUCCEEDED(gpu->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &vi)))
            local = static_cast<double>(vi.CurrentUsage) / kMb;
        if (SUCCEEDED(gpu->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &vi)))
            shared = static_cast<double>(vi.CurrentUsage) / kMb;
    }
    DWORD handles = 0;
    GetProcessHandleCount(GetCurrentProcess(), &handles);
    std::string b;
    for (int k = 0; k < 4; ++k)
        b += fmt("%s[%llu,%.0f]", k ? "," : "", static_cast<unsigned long long>(bucketN[k]),
                 static_cast<double>(bucketB[k]) / kMb);
    return fmt("{\"heap_mb\":%.1f,\"heap_used_mb\":%.1f,\"heaps\":%lu,\"priv_mb\":%.1f,\"mapped_mb\":%.1f,\"blocks\":[",
               heapMb, usedMb, static_cast<unsigned long>(nh), static_cast<double>(priv) / kMb,
               static_cast<double>(mapped) / kMb) +
           b + "],\"top\":[" + t +
           fmt("],\"gpu_local_mb\":%.1f,\"gpu_shared_mb\":%.1f,\"gdi\":%lu,\"user\":%lu,\"handles\":%lu,\"scan_ms\":%.1f}",
               local, shared, static_cast<unsigned long>(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS)),
               static_cast<unsigned long>(GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS)),
               static_cast<unsigned long>(handles), sw.ms());
}

// The window's counters (zero with --record).
std::string uiJson() {
    const UiCounters& u = uiCounters();
    return fmt(",\"ui\":{\"paints\":%llu,\"ticks\":%llu,\"resizes\":%llu,\"targets\":%llu,\"overlays\":%llu}",
               static_cast<unsigned long long>(u.paints.load()), static_cast<unsigned long long>(u.ticks.load()),
               static_cast<unsigned long long>(u.resizes.load()), static_cast<unsigned long long>(u.targets.load()),
               static_cast<unsigned long long>(u.overlays.load()));
}

}  // namespace

Recorder::~Recorder() {
    if (active_) stop();
}

std::string Recorder::autoStopReason() const {
    std::lock_guard<std::mutex> lk(reasonMu_);
    return autoStop_;
}

bool Recorder::start(const Config& c, const RecordOptions& o, std::wstring* err) {
    if (active_) return false;
    Stopwatch total;
    cfg_ = c;
    opt_ = o;
    sid_ = Log::newSessionId();
    Log::setSession(sid_);
    startedIso_ = nowIso();
    {
        std::lock_guard<std::mutex> lk(reasonMu_);
        autoStop_.clear();
    }
    {
        std::lock_guard<std::mutex> lk(shotsMu_);
        shots_.clear();
    }
    {
        std::lock_guard<std::mutex> lk(pauseMu_);
        pauses_.clear();
        pausedNow_ = false;
    }
    summary_.clear();
    lastOk_ = false;
    ticks_ = lateTicks_ = 0;
    maxLateMs_ = cpuSum_ = wsPeakMb_ = 0;
    cpuN_ = 0;
    Log::line("session.start", "\"version\":" + json(std::string(kVersion)) + ",\"config\":" + configJson(c) +
                                   fmt(",\"test_pattern\":%s,\"audio_only\":%s,\"max_s\":%.1f,\"system\":",
                                       o.testPattern ? "true" : "false", o.audioOnly ? "true" : "false", o.maxSeconds) +
                                   systemJson());

    audioFile_ = audioFileOf(c.audioFormat);
    if (!o.outPath.empty()) {
        path_ = o.outPath;
        const size_t dot = path_.find_last_of(L'.');
        const std::wstring ext = dot == std::wstring::npos ? L"" : lower(path_.substr(dot));
        if (ext == L".mp3" || ext == L".wav" || ext == L".m4a") audioFile_ = audioFileOf(ext);   // the name decides
    } else {
        path_ = c.resolvedOutDir() + L"\\" + c.namePrefix + L" " + stamp() + (o.audioOnly ? audioFileExt(audioFile_) : L".mp4");
    }
    const std::wstring dir = parentOf(path_);
    if (!ensureDir(dir)) {
        Log::event("session.dir", false, total.ms(), "cannot create " + u8(dir));
        *err = L"保存先のフォルダを作れませんでした: " + dir;
        return false;
    }
    const uint64_t fb = freeBytes(dir);
    if (fb < (1ull << 30)) {
        Log::event("session.disk", false, total.ms(), fmt("free %.0f MB < 1024 MB", static_cast<double>(fb) / 1048576.0));
        *err = L"保存先の空き容量が 1 GB を下回っています。";
        return false;
    }

    std::vector<std::string> notes;
    audio.open(c, &notes);
    for (auto& n : notes) Log::event("audio.note", false, -1, n);

    HRESULT hr = S_OK;
    if (!o.audioOnly) {
        video_ = std::make_unique<VideoSource>();
        std::string vinfo;
        hr = video_->init(c.monitor, o.testPattern, &vinfo);
        if (FAILED(hr)) {
            audio.stopNow();
            video_.reset();
            *err = L"画面の取り込みを準備できませんでした (" + wide(hrHex(hr)) + L")";
            return false;
        }
    }
    gpu_.reset();
    if (video_ && video_->device()) {
        Com<IDXGIDevice> dd;
        Com<IDXGIAdapter> ad;
        if (SUCCEEDED(Com<ID3D11Device>::from(video_->device()).as(dd)) && SUCCEEDED(dd->GetAdapter(ad.put()))) ad.as(gpu_);
    }
    if (!gpu_) {   // audio only: the first GPU (the window is drawn there)
        Com<IDXGIFactory1> f;
        Com<IDXGIAdapter1> a;
        if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), f.put_void())) && SUCCEEDED(f->EnumAdapters1(0, a.put())))
            a.as(gpu_);
    }
    writer_ = std::make_unique<Writer>();
    WriterParams wp;
    wp.path = path_;
    wp.video = !o.audioOnly;
    wp.width = video_ ? video_->width() : 0;
    wp.height = video_ ? video_->height() : 0;
    wp.fps = static_cast<UINT>(c.fps);
    wp.videoBitrate = static_cast<UINT>(c.videoKbps) * 1000;
    wp.audioBytesPerSec = static_cast<UINT>(c.audioKbps) * 125;
    wp.fragmented = c.container == L"fmp4";
    wp.hardware = c.hwEncode;
    wp.d3d = true;
    wp.audioFile = audioFile_;
    hr = writer_->open(wp, video_ ? video_->device() : nullptr, video_ ? video_->context() : nullptr);
    if (FAILED(hr)) {
        audio.stopNow();
        writer_.reset();
        video_.reset();
        *err = (o.audioOnly ? L"録音ファイルを作れませんでした (" : L"録画ファイルを作れませんでした (") + wide(hrHex(hr)) + L")";
        return false;
    }

    qpcStart_ = qpcNow();
    start100_ = qpcTo100ns(qpcStart_);
    stopQpc_ = 0;
    elapsed100_ = 0;
    stop_ = false;
    loopDone_ = false;
    active_ = true;
    Writer* w = writer_.get();
    audio.go(qpcStart_, [w](const int16_t* p, uint32_t n, int64_t t) { w->writeAudio(p, n, t); });
    vt_ = std::thread([this] { video_ ? videoLoop() : audioLoop(); });
    Log::event("session.recording", true, total.ms(), std::string(o.audioOnly ? "audio only " : "") + u8(path_));
    return true;
}

// Audio only: no frames to pace, so this thread keeps the clock, the 10 s stats and the stop conditions.
void Recorder::audioLoop() {
    const int64_t f = qpcFreq();
    int64_t nextStats = qpcStart_ + 10 * f;
    lastWall_ = qpcStart_;
    FILETIME c0, e0, k0, u0;
    GetProcessTimes(GetCurrentProcess(), &c0, &e0, &k0, &u0);
    lastCpu100_ = ft100(k0) + ft100(u0);
    while (!stop_) {
        Sleep(50);
        const int64_t now = qpcNow();
        int64_t shift = 0;
        if (liveAt(now, &shift)) {   // the clock stands still while paused
            const int64_t t = qpcTo100ns(now - shift) - start100_;
            elapsed100_ = t;
            if (opt_.maxSeconds > 0 && static_cast<double>(t) >= opt_.maxSeconds * 1e7) break;
        }
        ticks_++;
        if (writer_->full()) {   // WAV: 4 GB (5 h 47 min) is all a WAVE file can hold
            std::lock_guard<std::mutex> lk(reasonMu_);
            autoStop_ = "the WAV file is full (4 GB, about 5 h 47 min)";
            Log::event("session.wav_full", false, -1, autoStop_);
            break;
        }
        if (now >= nextStats) {
            logStats(now, false);
            nextStats += 10 * f;
            const uint64_t fb = freeBytes(parentOf(path_));
            if (fb < (300ull << 20)) {
                std::lock_guard<std::mutex> lk(reasonMu_);
                autoStop_ = fmt("disk almost full (%.0f MB left)", static_cast<double>(fb) / 1048576.0);
                Log::event("session.disk", false, -1, autoStop_);
                break;
            }
        }
    }
    stopQpc_ = qpcNow();
    loopDone_ = true;
}

void Recorder::videoLoop() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!timer) timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    const int64_t f = qpcFreq();
    const int64_t interval = f / cfg_.fps;
    const int64_t dur100 = 10000000LL / cfg_.fps;
    int64_t next = qpcStart_;
    int64_t lastT = -1;
    int64_t nextStats = qpcStart_ + 10 * f;
    int failStreak = 0;
    int dropStreak = 0;
    lastWall_ = qpcStart_;
    FILETIME c0, e0, k0, u0;
    GetProcessTimes(GetCurrentProcess(), &c0, &e0, &k0, &u0);
    lastCpu100_ = ft100(k0) + ft100(u0);
    while (!stop_) {
        const int64_t now = qpcNow();
        if (now < next) {
            const int64_t wait100 = qpcTo100ns(next - now);
            if (timer) {
                LARGE_INTEGER due;
                due.QuadPart = -std::max<int64_t>(wait100, 1000);
                SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE);
                WaitForSingleObject(timer, 200);
            } else {
                Sleep(static_cast<DWORD>(std::max<int64_t>(1, wait100 / 10000)));
            }
            continue;
        }
        if (now - next > 2 * interval) {
            lateTicks_++;
            maxLateMs_ = std::max(maxLateMs_, qpcMs(now - next));
            next = now;
        }
        next += interval;
        int64_t shift = 0;
        if (liveAt(now, &shift)) {   // while paused nothing is taken or written (the desktop image waits in DXGI)
            video_->update();
            ID3D11Texture2D* tex = video_->compose(cfg_.cursor);
            int64_t t = qpcTo100ns(now - shift) - start100_;
            if (t <= lastT) t = lastT + 1;
            lastT = t;
            HRESULT hr = writer_->writeVideo(tex, t, dur100);
            if (hr == S_FALSE) {           // pool empty: the encoder is not returning frames
                if (++dropStreak > cfg_.fps * 5) {
                    std::lock_guard<std::mutex> lk(reasonMu_);
                    autoStop_ = "encoder stalled (no frame accepted for 5 s)";
                    Log::event("session.encoder", false, -1, autoStop_);
                    break;
                }
            } else {
                dropStreak = 0;
            }
            if (FAILED(hr)) {
                if (++failStreak > cfg_.fps * 5) {
                    std::lock_guard<std::mutex> lk(reasonMu_);
                    autoStop_ = "video writes keep failing (" + hrHex(hr) + ")";
                    break;
                }
            } else {
                failStreak = 0;
            }
            ticks_++;
            videoOk_ = video_->capturing();
            elapsed100_ = t;
            if (opt_.maxSeconds > 0 && static_cast<double>(t) >= opt_.maxSeconds * 1e7) break;
        }
        if (now >= nextStats) {
            logStats(now, false);
            nextStats += 10 * f;
            const uint64_t fb = freeBytes(parentOf(path_));
            if (fb < (300ull << 20)) {
                std::lock_guard<std::mutex> lk(reasonMu_);
                autoStop_ = fmt("disk almost full (%.0f MB left)", static_cast<double>(fb) / 1048576.0);
                Log::event("session.disk", false, -1, autoStop_);
                break;
            }
        }
    }
    stopQpc_ = qpcNow();
    if (timer) CloseHandle(timer);
    CoUninitialize();
    loopDone_ = true;
}

void Recorder::logStats(int64_t now, bool final) {
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof(pmc);
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc));
    FILETIME c, e, k, u;
    GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u);
    const int64_t cpu = ft100(k) + ft100(u);
    const int64_t wall100 = qpcTo100ns(now - lastWall_);
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    const double cpuPct = wall100 > 0 ? 100.0 * static_cast<double>(cpu - lastCpu100_) /
                                            (static_cast<double>(wall100) * si.dwNumberOfProcessors)
                                      : 0.0;
    lastCpu100_ = cpu;
    lastWall_ = now;
    cpuSum_ += cpuPct;
    cpuN_++;
    const double ws = static_cast<double>(pmc.WorkingSetSize) / 1048576.0;
    wsPeakMb_ = std::max(wsPeakMb_, ws);
    lastSystem_ = systemJson();
    std::string v = "null";   // audio only
    if (video_) {
        const auto& vs = video_->stats;
        v = fmt("{\"ticks\":%llu,\"new\":%llu,\"unchanged\":%llu,\"pointer_only\":%llu,"
                "\"written\":%llu,\"dropped\":%llu,\"late_ticks\":%llu,\"max_late_ms\":%.0f,\"capturing\":%s,"
                "\"lost\":%llu,\"max_write_ms\":%.1f,\"cursor\":{\"drawn\":%llu,\"hidden\":%llu,\"away\":%llu,"
                "\"shapes\":%llu}}",
                static_cast<unsigned long long>(ticks_), static_cast<unsigned long long>(vs.newFrames),
                static_cast<unsigned long long>(vs.unchanged), static_cast<unsigned long long>(vs.pointerOnly),
                static_cast<unsigned long long>(writer_->videoSamples.load()),
                static_cast<unsigned long long>(writer_->videoDrops.load()), static_cast<unsigned long long>(lateTicks_),
                maxLateMs_, video_->capturing() ? "true" : "false", static_cast<unsigned long long>(vs.lostEvents),
                static_cast<double>(writer_->maxVideoWriteUs.load()) / 1000.0,
                static_cast<unsigned long long>(vs.cursorDrawn), static_cast<unsigned long long>(vs.cursorHidden),
                static_cast<unsigned long long>(vs.cursorAway), static_cast<unsigned long long>(vs.cursorShapes));
    }
    Log::line(final ? "stats.final" : "stats",
              fmt("\"t\":%.1f,\"paused\":%s,\"paused_s\":%.1f,\"video\":", static_cast<double>(elapsed100_.load()) / 1e7,
                  pausedNow_ ? "true" : "false", pausedSec()) +
                  v + ",\"audio\":" +
                  audio.windowJson() +
                  fmt(",\"proc\":{\"ws_mb\":%.0f,\"private_mb\":%.0f,\"cpu_pct\":%.1f},\"system\":", ws,
                      static_cast<double>(pmc.PrivateUsage) / 1048576.0, cpuPct) +
                  lastSystem_ + fmt(",\"file_mb\":%.1f", static_cast<double>(fileBytes(path_)) / 1048576.0) +
                  ",\"mem\":" + memoryJson(gpu_.get()) + uiJson());
}

void Recorder::stop() {
    if (!active_) return;
    Stopwatch sw;
    stop_ = true;
    if (vt_.joinable()) vt_.join();
    if (!stopQpc_) stopQpc_ = qpcNow();
    {   // stopped while paused: that pause ends here (the sound after it is left out anyway)
        std::lock_guard<std::mutex> lk(pauseMu_);
        if (!pauses_.empty() && pauses_.back().second == INT64_MAX)
            pauses_.back().second = std::max(stopQpc_, pauses_.back().first);
    }
    audio.stopAt(stopQpc_);
    logStats(qpcNow(), true);
    double finMs = 0;
    HRESULT hr = writer_->finalize(&finMs);
    writeSummary(hr, finMs);
    writer_->close();
    writer_.reset();
    video_.reset();
    active_ = false;
    pausedNow_ = false;
    Log::event("session.end", lastOk_, sw.ms(), u8(path_));
}

void Recorder::noteShot(const std::wstring& path, double atS) {
    std::lock_guard<std::mutex> lk(shotsMu_);
    shots_ += (shots_.empty() ? "" : ",") + std::string("{\"path\":") + json(path) + fmt(",\"at_s\":%.1f}", atS);
}

bool Recorder::liveAt(int64_t qpc, int64_t* shift) const {
    std::lock_guard<std::mutex> lk(pauseMu_);
    int64_t s = 0;
    for (auto& p : pauses_) {
        if (qpc < p.first) break;
        if (qpc < p.second) {
            *shift = s;
            return false;
        }
        s += p.second - p.first;
    }
    *shift = s;
    return true;
}

// The video thread looks at the pauses on every frame and the audio mixer runs latency_ms behind, so both cut the
// same span of the timeline: picture and sound stay together after a resume.
bool Recorder::pause() {
    if (!active_ || stop_) return false;
    const int64_t now = qpcNow();
    size_t n = 0;
    {
        std::lock_guard<std::mutex> lk(pauseMu_);
        if (pausedNow_) return false;
        pauses_.push_back({now, INT64_MAX});
        pausedNow_ = true;
        n = pauses_.size();
    }
    audio.pauseAt(now);
    Log::event("session.pause", true, -1, fmt("pause=%zu at=%.1fs", n, elapsedSec()));
    return true;
}

bool Recorder::resume() {
    if (!active_ || stop_) return false;
    const int64_t now = qpcNow();
    double ms = 0;
    size_t n = 0;
    {
        std::lock_guard<std::mutex> lk(pauseMu_);
        if (!pausedNow_ || pauses_.empty()) return false;
        pauses_.back().second = std::max(now, pauses_.back().first);
        ms = qpcMs(pauses_.back().second - pauses_.back().first);
        pausedNow_ = false;
        n = pauses_.size();
    }
    audio.resumeAt(now);
    Log::event("session.resume", true, ms, fmt("pause=%zu at=%.1fs paused_total=%.1fs", n, elapsedSec(), pausedSec()));
    return true;
}

double Recorder::pausedSec() const {
    const int64_t now = qpcNow();
    std::lock_guard<std::mutex> lk(pauseMu_);
    int64_t s = 0;
    for (auto& p : pauses_) s += std::min(p.second, std::max(now, p.first)) - p.first;
    return qpcMs(s) / 1000.0;
}

int Recorder::pauses() const {
    std::lock_guard<std::mutex> lk(pauseMu_);
    return static_cast<int>(pauses_.size());
}

void Recorder::writeSummary(HRESULT finHr, double finMs) {
    const double dur = static_cast<double>(elapsed100_.load()) / 1e7;
    std::string shots;
    {
        std::lock_guard<std::mutex> lk(shotsMu_);
        shots = shots_;
    }
    const uint64_t bytes = fileBytes(path_);
    lastOk_ = SUCCEEDED(finHr) && (!video_ || writer_->videoSamples.load() > 0) && writer_->audioSamples.load() > 0;
    patched_ = !cfg_.container.empty() && cfg_.container == L"fmp4" ? writer_->durationPatched : true;
    std::string reason = autoStopReason();
    std::string v = "null";   // audio only
    if (video_) {
        const auto& vs = video_->stats;
        v = fmt("{\"width\":%u,\"height\":%u,\"fps\":%d,\"ticks\":%llu,\"new_frames\":%llu,\"written\":%llu,"
                "\"dropped\":%llu,\"late_ticks\":%llu,\"max_late_ms\":%.0f,\"lost_events\":%llu,\"lost_ms\":%.0f,"
                "\"format_mismatch\":%llu,\"max_write_ms\":%.1f,\"hw_encoder\":%s,\"gpu_textures\":%s,\"pipeline\":",
                video_->width(), video_->height(), cfg_.fps, static_cast<unsigned long long>(ticks_),
                static_cast<unsigned long long>(vs.newFrames), static_cast<unsigned long long>(writer_->videoSamples.load()),
                static_cast<unsigned long long>(writer_->videoDrops.load()), static_cast<unsigned long long>(lateTicks_),
                maxLateMs_, static_cast<unsigned long long>(vs.lostEvents), vs.lostMs,
                static_cast<unsigned long long>(vs.formatMismatch),
                static_cast<double>(writer_->maxVideoWriteUs.load()) / 1000.0,
                writer_->encoderIsHardware ? "true" : "false", writer_->usesD3D() ? "true" : "false") +
            json(writer_->pipeline) + "}";
    }
    summary_ =
        "{\"sid\":" + json(sid_) + ",\"version\":" + json(std::string(kVersion)) + ",\"path\":" + json(path_) +
        ",\"started\":" + json(startedIso_) + ",\"ended\":" + json(nowIso()) +
        fmt(",\"ok\":%s,\"audio_only\":%s,\"duration_s\":%.2f,\"paused_s\":%.1f,\"pauses\":%d,\"file_bytes\":%llu,"
            "\"avg_kbps\":%.0f,\"finalize_ms\":%.1f,\"finalize_hr\":%s",
            lastOk_ ? "true" : "false", video_ ? "false" : "true", dur, pausedSec(), pauses(),
            static_cast<unsigned long long>(bytes), dur > 0 ? static_cast<double>(bytes) * 8.0 / 1000.0 / dur : 0.0, finMs,
            json(hrHex(finHr)).c_str()) +
        ",\"audio_file\":" + (video_ ? std::string("null") : json(std::string(audioFileName(audioFile_)))) +
        ",\"auto_stop\":" + (reason.empty() ? std::string("null") : json(reason)) +
        ",\"duration_patched\":" + (patched_ ? "true" : "false") + ",\"video\":" + v +
        fmt(",\"audio\":{\"mixed_s\":%.2f,\"cut_s\":%.2f,\"clipped_samples\":%llu,\"written_samples\":%llu,\"sources\":",
            static_cast<double>(audio.mixedFrames()) / 48000.0, static_cast<double>(audio.cutFrames()) / 48000.0,
            static_cast<unsigned long long>(audio.clipped()), static_cast<unsigned long long>(writer_->audioSamples.load())) +
        audio.summaryJson() + "}" +
        fmt(",\"perf\":{\"ws_peak_mb\":%.0f,\"cpu_avg_pct\":%.1f,\"write_errors\":%llu},\"system_at_end\":", wsPeakMb_,
            cpuN_ ? cpuSum_ / cpuN_ : 0.0, static_cast<unsigned long long>(writer_->writeErrors.load())) +
        (lastSystem_.empty() ? std::string("{}") : lastSystem_) + ",\"screenshots\":[" + shots + "]}";
    const std::wstring dir = appDir() + L"\\sessions";
    ensureDir(dir);
    HANDLE h = CreateFileW((dir + L"\\" + wide(sid_) + L".json").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD w = 0;
        WriteFile(h, summary_.data(), static_cast<DWORD>(summary_.size()), &w, nullptr);
        CloseHandle(h);
    }
}

std::string verifyFile(const std::wstring& path, bool audioOnly) {
    Stopwatch sw;
    Com<IMFSourceReader> r;
    HRESULT hr = MFCreateSourceReaderFromURL(path.c_str(), nullptr, r.put());
    if (FAILED(hr)) return "{\"ok\":false,\"error\":" + json("open " + hrHex(hr)) + "}";
    PROPVARIANT pv;
    PropVariantInit(&pv);
    double duration = -1;
    if (SUCCEEDED(r->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &pv)) && pv.vt == VT_UI8)
        duration = static_cast<double>(pv.uhVal.QuadPart) / 1e7;
    PropVariantClear(&pv);
    struct S { std::string kind; uint64_t n = 0; int64_t first = -1, last = -1; uint64_t bytes = 0; std::vector<int64_t> ts; };
    std::vector<S> st;
    for (DWORD i = 0;; ++i) {
        Com<IMFMediaType> t;
        if (FAILED(r->GetNativeMediaType(i, 0, t.put()))) break;
        GUID major{};
        t->GetGUID(MF_MT_MAJOR_TYPE, &major);
        S s;
        s.kind = major == MFMediaType_Video ? "video" : major == MFMediaType_Audio ? "audio" : "other";
        st.push_back(s);
    }
    uint64_t errors = 0;
    uint32_t ended = 0;  // ANY_STREAM reports end of stream per stream; stop once every stream is done
    const uint32_t all = st.empty() ? 0 : ((1u << st.size()) - 1);
    int eosCalls = 0;
    r->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, TRUE);
    for (;;) {
        DWORD idx = 0, flags = 0;
        LONGLONG ts = 0;
        Com<IMFSample> smp;
        hr = r->ReadSample(MF_SOURCE_READER_ANY_STREAM, 0, &idx, &flags, &ts, smp.put());
        if (FAILED(hr)) { errors++; break; }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
            ended |= (1u << idx);
            if (ended == all || ++eosCalls > 16) break;
            continue;
        }
        if (flags & MF_SOURCE_READERF_ERROR) { errors++; break; }
        if (smp && idx < st.size()) {
            S& s = st[idx];
            s.n++;
            if (s.first < 0) s.first = ts;
            s.last = ts;
            s.ts.push_back(ts);
            DWORD len = 0;
            smp->GetTotalLength(&len);
            s.bytes += len;
        }
    }
    bool hasVideo = false, hasAudio = false;
    for (auto& s : st) {
        hasVideo |= s.kind == "video" && s.n > 0;
        hasAudio |= s.kind == "audio" && s.n > 0;
    }
    std::string o = fmt("{\"ok\":%s,\"ms\":%.0f,\"duration_s\":%.2f,\"read_errors\":%llu,\"streams\":[",
                        errors == 0 && hasAudio && (hasVideo || audioOnly) ? "true" : "false", sw.ms(), duration,
                        static_cast<unsigned long long>(errors));
    for (size_t i = 0; i < st.size(); ++i) {
        // the longest step between two samples: a hole in the timeline (a pause that was not cut out) shows here.
        // (A comparator of our own: std::less would link libc++'s prebuilt sorts for every type, +85 KB.)
        std::sort(st[i].ts.begin(), st[i].ts.end(), [](int64_t x, int64_t y) { return x < y; });
        int64_t gap = 0;
        for (size_t k = 1; k < st[i].ts.size(); ++k) gap = std::max(gap, st[i].ts[k] - st[i].ts[k - 1]);
        o += fmt("%s{\"kind\":\"%s\",\"samples\":%llu,\"first_s\":%.3f,\"last_s\":%.3f,\"max_gap_s\":%.3f,\"kbytes\":%.0f}",
                 i ? "," : "", st[i].kind.c_str(), static_cast<unsigned long long>(st[i].n), st[i].first / 1e7,
                 st[i].last / 1e7, gap / 1e7, static_cast<double>(st[i].bytes) / 1024.0);
    }
    return o + "]}";
}

std::string analyzeTones(const std::wstring& path, const std::vector<double>& expected) {
    Com<IMFSourceReader> r;
    HRESULT hr = MFCreateSourceReaderFromURL(path.c_str(), nullptr, r.put());
    if (FAILED(hr)) return "{\"ok\":false,\"error\":" + json("open " + hrHex(hr)) + "}";
    r->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
    r->SetStreamSelection(MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);
    Com<IMFMediaType> pcm;
    MFCreateMediaType(pcm.put());
    pcm->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    pcm->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    pcm->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    pcm->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 48000);
    pcm->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
    hr = r->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, pcm.get());
    if (FAILED(hr)) return "{\"ok\":false,\"error\":" + json("pcm " + hrHex(hr)) + "}";
    std::vector<int16_t> mono;
    for (;;) {
        DWORD idx = 0, flags = 0;
        LONGLONG ts = 0;
        Com<IMFSample> s;
        if (FAILED(r->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, &idx, &flags, &ts, s.put()))) break;
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
        if (!s) continue;
        Com<IMFMediaBuffer> b;
        if (FAILED(s->ConvertToContiguousBuffer(b.put()))) continue;
        BYTE* d = nullptr;
        DWORD len = 0;
        if (SUCCEEDED(b->Lock(&d, nullptr, &len))) {
            const int16_t* p = reinterpret_cast<const int16_t*>(d);
            for (DWORD i = 0; i + 1 < len / 2; i += 2) mono.push_back(static_cast<int16_t>((p[i] + p[i + 1]) / 2));
            b->Unlock();
        }
    }
    // Onset = first sample above -20 dBFS that follows at least 100 ms below it.
    std::vector<double> onsets;
    int quiet = 0;
    int peak = 0;
    for (size_t i = 0; i < mono.size(); ++i) {
        const int v = std::abs(static_cast<int>(mono[i]));
        peak = std::max(peak, v);
        if (v > 3277) {
            if (quiet >= 4800) onsets.push_back(static_cast<double>(i) / 48000.0);
            quiet = 0;
        } else {
            quiet++;
        }
    }
    std::string o = fmt("{\"ok\":%s,\"samples\":%llu,\"peak_db\":%.1f,\"onsets_s\":[", onsets.size() == expected.size() ? "true" : "false",
                        static_cast<unsigned long long>(mono.size()), peak > 0 ? 20.0 * std::log10(peak / 32768.0) : -90.0);
    for (size_t i = 0; i < onsets.size(); ++i) o += fmt("%s%.3f", i ? "," : "", onsets[i]);
    o += "],\"offset_ms\":[";
    for (size_t i = 0; i < onsets.size() && i < expected.size(); ++i) o += fmt("%s%.1f", i ? "," : "", (onsets[i] - expected[i]) * 1000.0);
    return o + "]}";
}

}  // namespace rc
