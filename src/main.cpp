// Rec Cue! — entry point: small Win32 window, or command-line modes for tests.
//   rec-cue.exe                         window
//   rec-cue.exe --ui-snapshot <png> [--dpi N] [--at 2026-10-22T14:00]   the window's look in several states, as PNGs
//                                       (no window opened; --at: しおり's lines for that day and time)
//   rec-cue.exe --lines-check [--json-out <file>]   しおり's lines: counts, load / pick time, any that would not fit
//   rec-cue.exe --quit [--json-out <file>]   closes a running window unless it is recording (deploy)
//   rec-cue.exe --list-devices --json-out <file>
//   rec-cue.exe --record --seconds 15 [--test-pattern | --audio-only] [--out <mp4|mp3|wav|m4a>] [--json-out <file>] [--verify]
//               [--fps N] [--container fmp4|mp4] [--mic <spec>] [--system <spec>] [--audio-format mp3|wav|m4a]
//               [--pause-at S --pause-for S]   pauses S seconds after the start (wall clock) and resumes after the other S
//   rec-cue.exe --verify-file <mp4> --json-out <file>
//   rec-cue.exe --shot <png> [--json-out <file>]        screenshot of the recorded monitor (timing check)
//   rec-cue.exe --shot-selftest [--json-out <file>]     shows a small test square for a moment and checks that a
//                                                       window left out of the shot is really missing from it
//   rec-cue.exe --export-logs <zip> [--json-out <file>] ログを取得 without the window: the ZIP and its counts
//   rec-cue.exe --update-check [--update-feed <url>] [--json-out <file>]   what the window asks GitHub when it opens
//   rec-cue.exe --update-install [--json-out <file>]    今すぐ更新 without the window (this exe is replaced, not reopened)
//   --pretend-version <v>   compares the latest release with <v> instead of this exe's version (tests; also the window)
#include "common.h"  // windows.h first

#include <dwmapi.h>
#include <mfapi.h>
#include <mftransform.h>
#include <commctrl.h>
#include <psapi.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <thread>

#include "audio.h"
#include "config.h"
#include "indicator.h"
#include "logpack.h"
#include "recorder.h"
#include "shiori.h"
#include "shot.h"
#include "ui_draw.h"
#include "update.h"
#include "video.h"

using namespace rc;

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

namespace {

// ---------------------------------------------------------------- CLI

struct Args {
    bool listDevices = false, record = false, verify = false, version = false, testPattern = false, probe = false, quit = false,
         shotTest = false, audioOnly = false, linesCheck = false, updateCheck = false, updateInstall = false, any = false;
    double seconds = 0, pauseAt = 0, pauseFor = 0;
    int fps = 0, dpi = 0;
    std::wstring out, jsonOut, verifyFile, container, mic, system, testTone, snapshot, shot, at, audioFormat, exportLogs;
    std::wstring updateFeed, pretend;   // --update-feed: another API address (tests)
    std::string error;
};

Args parseArgs() {
    Args a;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; i < argc; ++i) {
        std::wstring s = argv[i];
        auto val = [&](std::wstring& dst) {
            if (i + 1 < argc) dst = argv[++i];
            else a.error = "missing value for " + u8(s);
        };
        if (s == L"--pretend-version") {   // still opens the window
            val(a.pretend);
            continue;
        }
        a.any = true;
        if (s == L"--list-devices") a.listDevices = true;
        else if (s == L"--record") a.record = true;
        else if (s == L"--verify") a.verify = true;
        else if (s == L"--version") a.version = true;
        else if (s == L"--test-pattern") a.testPattern = true;
        else if (s == L"--audio-only") a.audioOnly = true;
        else if (s == L"--probe-levels") a.probe = true;
        else if (s == L"--quit") a.quit = true;
        else if (s == L"--shot-selftest") a.shotTest = true;
        else if (s == L"--lines-check") a.linesCheck = true;
        else if (s == L"--update-check") a.updateCheck = true;
        else if (s == L"--update-install") a.updateInstall = true;
        else if (s == L"--update-feed") val(a.updateFeed);
        else if (s == L"--shot") val(a.shot);
        else if (s == L"--export-logs") val(a.exportLogs);
        else if (s == L"--seconds") { std::wstring v; val(v); a.seconds = _wtof(v.c_str()); }
        else if (s == L"--pause-at") { std::wstring v; val(v); a.pauseAt = _wtof(v.c_str()); }
        else if (s == L"--pause-for") { std::wstring v; val(v); a.pauseFor = _wtof(v.c_str()); }
        else if (s == L"--fps") { std::wstring v; val(v); a.fps = _wtoi(v.c_str()); }
        else if (s == L"--out") val(a.out);
        else if (s == L"--json-out") val(a.jsonOut);
        else if (s == L"--verify-file") val(a.verifyFile);
        else if (s == L"--container") val(a.container);
        else if (s == L"--mic") val(a.mic);
        else if (s == L"--system") val(a.system);
        else if (s == L"--audio-format") val(a.audioFormat);
        else if (s == L"--test-tone") val(a.testTone);
        else if (s == L"--ui-snapshot") val(a.snapshot);
        else if (s == L"--dpi") { std::wstring v; val(v); a.dpi = _wtoi(v.c_str()); }
        else if (s == L"--at") val(a.at);
        else a.error = "unknown argument " + u8(s);
    }
    LocalFree(argv);
    return a;
}

void writeText(const std::wstring& path, const std::string& s) {
    if (path.empty()) return;
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD w = 0;
    WriteFile(h, s.data(), static_cast<DWORD>(s.size()), &w, nullptr);
    CloseHandle(h);
}

std::string encodersJson() {
    MFT_REGISTER_TYPE_INFO outType{MFMediaType_Video, MFVideoFormat_H264};
    IMFActivate** acts = nullptr;
    UINT32 n = 0;
    std::string s = "[";
    HRESULT hr = MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER,
                           MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_ASYNCMFT | MFT_ENUM_FLAG_SORTANDFILTER,
                           nullptr, &outType, &acts, &n);
    if (SUCCEEDED(hr)) {
        for (UINT32 i = 0; i < n; ++i) {
            WCHAR* name = nullptr;
            UINT32 len = 0;
            std::wstring nm;
            if (SUCCEEDED(acts[i]->GetAllocatedString(MFT_FRIENDLY_NAME_Attribute, &name, &len))) nm = name;
            CoTaskMemFree(name);
            UINT32 flags = 0;
            acts[i]->GetUINT32(MF_TRANSFORM_FLAGS_Attribute, &flags);
            s += fmt("%s{\"name\":%s,\"hardware\":%s}", i ? "," : "", json(nm).c_str(),
                     (flags & MFT_ENUM_FLAG_HARDWARE) ? "true" : "false");
            acts[i]->Release();
        }
        CoTaskMemFree(acts);
    }
    return s + "]";
}

std::string devicesJson() {
    std::string s = "{\"version\":" + json(std::string(kVersion)) + ",\"monitors\":[";
    bool first = true;
    for (auto& m : listMonitors(true)) {
        s += fmt("%s{\"index\":%d,\"device\":%s,\"adapter\":%s,\"x\":%ld,\"y\":%ld,\"w\":%ld,\"h\":%ld,\"primary\":%s,"
                 "\"name\":%s,\"internal\":%s}",
                 first ? "" : ",", m.index, json(m.device).c_str(), json(m.adapter).c_str(), m.rect.left, m.rect.top,
                 m.rect.right - m.rect.left, m.rect.bottom - m.rect.top, m.primary ? "true" : "false",
                 json(m.name).c_str(), m.internal ? "true" : "false");
        first = false;
    }
    for (int r = 0; r < 2; ++r) {
        s += r == 0 ? "],\"mics\":[" : "],\"speakers\":[";
        first = true;
        for (auto& d : listAudioDevices(r == 1, nullptr, true)) {
            s += std::string(first ? "" : ",") + "{\"name\":" + json(d.name) + ",\"default\":" + (d.defConsole ? "true" : "false") +
                 ",\"communications\":" + (d.defComm ? "true" : "false") + ",\"apps\":[";
            for (size_t i = 0; i < d.apps.size(); ++i)
                s += fmt("%s{\"exe\":%s,\"active\":%s}", i ? "," : "", json(d.apps[i].exe).c_str(), d.apps[i].active ? "true" : "false");
            s += "]}";
            first = false;
        }
    }
    return s + "],\"h264_encoders\":" + encodersJson() + "}";
}

std::string shotInfoJson(const ShotImage& img, const ShotInfo& i) {
    return fmt("\"w\":%u,\"h\":%u,\"excluded\":%s,\"hidden\":%s,\"flush_ms\":%.1f,\"blt_ms\":%.1f", img.w, img.h,
               i.excluded ? "true" : "false", i.hidden ? "true" : "false", i.flushMs, i.bltMs);
}

// --shot <png>: the recorded monitor as the screenshot button takes it (nothing to leave out here), for timings.
int runShot(const std::wstring& path, const std::wstring& jsonOut) {
    const Config c = loadConfig();
    ShotImage img;
    ShotInfo info;
    std::string err;
    Stopwatch sw;
    const bool ok = captureMonitor(c.monitor, nullptr, &img, &info, &err);
    const double capMs = sw.ms();
    Stopwatch es;
    const HRESULT hr = ok ? savePng(img, path) : E_FAIL;
    const double saveMs = es.ms();
    WIN32_FILE_ATTRIBUTE_DATA fa{};
    const uint64_t bytes = GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa)
                               ? (static_cast<uint64_t>(fa.nFileSizeHigh) << 32) | fa.nFileSizeLow
                               : 0;
    const std::string s = fmt("{\"ok\":%s,\"capture_ms\":%.1f,\"save_ms\":%.1f,\"bytes\":%llu,\"hr\":%s,",
                              ok && SUCCEEDED(hr) ? "true" : "false", capMs, saveMs, static_cast<unsigned long long>(bytes),
                              json(hrHex(hr)).c_str()) +
                          shotInfoJson(img, info) + ",\"error\":" + json(err) + "}";
    Log::line("shot.cli", "\"result\":" + s);
    writeText(jsonOut, s);
    return ok && SUCCEEDED(hr) ? 0 : 4;
}

// --shot-selftest: shows a 96 x 96 magenta square on the recorded monitor for a moment and takes two pictures, one
// leaving that window out (as the app does with its own window) and one not. Only pixels are counted; nothing is saved.
int runShotSelftest(const std::wstring& jsonOut) {
    const Config c = loadConfig();
    const auto mons = listMonitors();
    if (mons.empty()) {
        writeText(jsonOut, "{\"ok\":false,\"error\":\"no monitor\"}");
        return 4;
    }
    const int idx = c.monitor >= 0 && c.monitor < static_cast<int>(mons.size()) ? c.monitor : 0;
    const RECT mr = mons[static_cast<size_t>(idx)].rect;
    constexpr int kSide = 96, kOff = 48;
    HBRUSH magenta = CreateSolidBrush(RGB(255, 0, 255));
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hbrBackground = magenta;
    wc.lpszClassName = L"RecCueShotTest";
    RegisterClassExW(&wc);
    HWND w = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName, L"", WS_POPUP,
                             mr.left + kOff, mr.top + kOff, kSide, kSide, nullptr, nullptr, wc.hInstance, nullptr);
    if (w) {
        ShowWindow(w, SW_SHOWNOACTIVATE);
        UpdateWindow(w);
        const int64_t until = qpcNow() + qpcFreq() / 5;   // let it paint and be composed (200 ms)
        MSG m;
        while (qpcNow() < until) {
            while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&m);
            Sleep(10);
        }
    }
    auto count = [&](const ShotImage& im) {
        int n = 0;
        for (int y = kOff; y < kOff + kSide && y < static_cast<int>(im.h); ++y)
            for (int x = kOff; x < kOff + kSide && x < static_cast<int>(im.w); ++x)
                if ((im.px[static_cast<size_t>(y) * im.w + static_cast<size_t>(x)] & 0xFFFFFFu) == 0xFF00FFu) ++n;
        return n;
    };
    ShotImage shown, left;
    ShotInfo si, li;
    std::string se, le;
    const bool okShown = w && captureMonitor(idx, nullptr, &shown, &si, &se);
    const bool okLeft = w && captureMonitor(idx, w, &left, &li, &le);
    if (w) DestroyWindow(w);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    DeleteObject(magenta);
    const int inShown = okShown ? count(shown) : -1, inLeft = okLeft ? count(left) : -1;
    constexpr int kArea = kSide * kSide;
    const bool ok = okShown && okLeft && inShown >= kArea * 9 / 10 && inLeft == 0 && li.excluded;
    const std::string s = fmt("{\"ok\":%s,\"monitor\":%d,\"square_px\":%d,\"in_plain_shot\":%d,\"in_shot_without_window\":%d,",
                              ok ? "true" : "false", idx, kArea, inShown, inLeft) +
                          "\"plain\":{" + shotInfoJson(shown, si) + "},\"without_window\":{" + shotInfoJson(left, li) +
                          "},\"error\":" + json(se + le) + "}";
    Log::line("shot.selftest", "\"result\":" + s);
    writeText(jsonOut, s);
    return ok ? 0 : 4;
}

// --lines-check: しおり's lines as the window would show them. How many per scene, the time to read them and to pick
// one, the memory they take, and every line (filled in with the longest values) that would not fit in her bubble.
// しおり's lines about アップデート (update.h): fixed ones, measured by --lines-check like those in shiori-lines.txt.
// {v}: a version.
const wchar_t* const kUpdOffer = L"新しい版 {v} が出ております。更新なさいますか。";
const wchar_t* const kUpdLater = L"承知いたしました。次に開いたときに改めてお知らせします。";
const wchar_t* const kUpdSkip = L"{v} は見送ります。次の版が出ましたらお知らせします。";
const wchar_t* const kUpdGetting = L"新しい版を受け取っております。少々お待ちください。";
const wchar_t* const kUpdRestart = L"入れ替えが済みました。開き直します。";
const wchar_t* const kUpdDone = L"{v} に更新いたしました。設定はそのままでございます。";
const wchar_t* const kUpdFailed = L"更新できませんでした。「ページを開く」からお入れください。";
const wchar_t* const kUpdNoFolder = L"この場所には書き込めません。「ページを開く」からどうぞ。";
const wchar_t* const kUpdNoReopen = L"入れ替えました。お手数ですが、開き直してください。";
const wchar_t* const kUpdLines[] = {kUpdOffer, kUpdLater,  kUpdSkip,     kUpdGetting, kUpdRestart,
                                    kUpdDone,  kUpdFailed, kUpdNoFolder, kUpdNoReopen};

std::wstring withVersion(const wchar_t* line, const std::wstring& v) {
    std::wstring s = line;
    const size_t k = s.find(L"{v}");
    if (k != std::wstring::npos) s.replace(k, 3, v);
    return s;
}

int runLinesCheck(const std::wstring& jsonOut) {
    UiPainter p;
    if (!p.init()) {
        writeText(jsonOut, "{\"ok\":false,\"error\":\"Direct2D / DirectWrite unavailable\"}");
        return 1;
    }
    auto privateKb = [] {
        PROCESS_MEMORY_COUNTERS_EX pm{};
        pm.cb = sizeof(pm);
        GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pm), sizeof(pm));
        return static_cast<double>(pm.PrivateUsage) / 1024.0;
    };
    const double kb0 = privateKb();
    Lines lines;
    std::vector<std::string> problems;
    Stopwatch sw;
    const bool loaded = lines.load(GetModuleHandleW(nullptr), &problems);
    const double loadMs = sw.ms(), addedKb = privateKb() - kb0;
    int per[static_cast<int>(Scene::Count)] = {};
    const float room = p.commentRoom();
    std::vector<std::pair<float, size_t>> widths;
    widths.reserve(lines.size());
    std::string over;
    auto entry = [&](size_t i, float w) {
        return fmt("{\"line\":%d,\"scene\":%s,\"dip\":%.1f,\"text\":", lines.sourceAt(i),
                   json(std::string(sceneName(lines.sceneAt(i)))).c_str(), w) +
               json(lines.longestAt(i)) + "}";
    };
    for (size_t i = 0; i < lines.size(); ++i) {
        ++per[static_cast<int>(lines.sceneAt(i))];
        const float w = p.commentWidth(lines.longestAt(i));
        widths.push_back({w, i});
        if (w > room) over += (over.empty() ? "" : ",") + entry(i, w);
    }
    std::string fixed;   // アップデート's lines, with the longest version they could name
    float fixedMax = 0;
    for (const wchar_t* l : kUpdLines) {
        const std::wstring t = withVersion(l, L"10.10.10");
        const float w = p.commentWidth(t);
        fixedMax = std::max(fixedMax, w);
        const std::string e = fmt("{\"dip\":%.1f,\"text\":", w) + json(t) + "}";
        fixed += (fixed.empty() ? "" : ",") + e;
        if (w > room) over += (over.empty() ? "" : ",") + e;
    }
    std::sort(widths.begin(), widths.end(), [](const auto& x, const auto& y) { return x.first > y.first; });
    std::string widest;
    for (size_t k = 0; k < widths.size() && k < 12; ++k) widest += (k ? "," : "") + entry(widths[k].second, widths[k].first);
    SayContext c;
    c.recKey = L"Ctrl+Alt+R";
    c.audioKey = L"Ctrl+Alt+A";
    c.shotKey = L"Ctrl+Alt+S";
    constexpr int kPicks = 20000;
    int empty = 0;
    Stopwatch ps;
    for (int i = 0; i < kPicks; ++i) {
        c.n = i % 200;
        c.audio = (i / 7) % 2 == 1;
        if (lines.pick(static_cast<Scene>(i % static_cast<int>(Scene::Count)), c).empty()) ++empty;
    }
    const double pickUs = ps.ms() * 1000.0 / kPicks;
    std::string scenes;
    for (int i = 0; i < static_cast<int>(Scene::Count); ++i)
        scenes += fmt("%s\"%s\":%d", i ? "," : "", sceneName(static_cast<Scene>(i)), per[i]);
    std::string probs;
    for (auto& q : problems) probs += (probs.empty() ? "" : ",") + json(q);
    const bool ok = loaded && problems.empty() && over.empty();
    const std::string s = fmt("{\"ok\":%s,\"lines\":%zu,\"load_ms\":%.2f,\"private_kb_added\":%.0f,\"pick_us\":%.2f,"
                              "\"empty_picks\":%d,\"room_dip\":%.1f,",
                              ok ? "true" : "false", lines.size(), loadMs, addedKb, pickUs, empty, room) +
                          "\"scenes\":{" + scenes + "},\"problems\":[" + probs + "],\"over\":[" + over + "],\"widest\":[" +
                          widest + "]" + fmt(",\"fixed_max_dip\":%.1f,\"fixed\":[", fixedMax) + fixed + "]}";
    Log::line("lines.check", fmt("\"ok\":%s,\"lines\":%zu,\"load_ms\":%.2f", ok ? "true" : "false", lines.size(), loadMs));
    writeText(jsonOut, s);
    return ok ? 0 : 4;
}

DWORD windowsBuild();

// about.json in the ZIP of ログを取得: what the logs came from.
std::string aboutJson() {
    return "{\"app\":\"Rec Cue!\",\"version\":" + json(std::string(kVersion)) +
           fmt(",\"windows_build\":%lu,\"days\":%d,\"exported_at\":", windowsBuild(), kLogDays) + json(nowIso()) + "}";
}

std::string packJson(bool ok, const std::wstring& zip, const LogPack& r) {
    return fmt("{\"ok\":%s,\"files\":%d,\"skipped\":%d,\"bytes\":%llu,\"ms\":%.1f,\"path\":", ok ? "true" : "false", r.files,
               r.skipped, static_cast<unsigned long long>(r.bytes), r.ms) +
           json(zip) + ",\"error\":" + json(r.error) + "}";
}

// --export-logs <zip>: the ZIP that ログを取得 makes, written here (for the tests).
int runExportLogs(const std::wstring& zip, const std::wstring& jsonOut) {
    LogPack r;
    const bool ok = packLogs(zip, kLogDays, aboutJson(), &r);
    const std::string s = packJson(ok, zip, r);
    Log::line("logs.cli", "\"result\":" + s);
    writeText(jsonOut, s);
    return ok ? 0 : 4;
}

// --update-check / --update-install: what the window does when it opens and on 今すぐ更新, without the window.
// Install replaces this exe (kept as <name>.prev.exe) and does not open the new one.
int runUpdate(const Args& a) {
    const Config cfg = loadConfig();
    const std::wstring current = a.pretend.empty() ? wide(kVersion) : a.pretend;
    const UpdateCheck c = checkLatest(a.updateFeed.empty() ? kUpdateApi : a.updateFeed);
    const bool newer = c.ok && compareVersions(c.latest.version, current) > 0;
    const bool skipped = newer && c.latest.version == cfg.updateSkip;
    Log::event("update.check", c.ok, c.ms,
               fmt("status=%d latest=%s current=%s newer=%d skipped=%d (cli)", c.status, u8(c.latest.version).c_str(),
                   u8(current).c_str(), newer, skipped) + (c.error.empty() ? "" : " " + c.error));
    std::string s = updateCheckJson(c);
    s = s.substr(0, s.size() - 1) + ",\"current\":" + json(current) + ",\"newer\":" + (newer ? "true" : "false") +
        ",\"skipped\":" + (skipped ? "true" : "false") + ",\"update_check\":" + (cfg.updateCheck ? "true" : "false") + "}";
    if (!a.updateInstall) {
        writeText(a.jsonOut, s);
        return c.ok ? 0 : 4;
    }
    UpdateResult u;
    if (!newer) {
        u.step = "current";
        u.error = c.ok ? "the latest release is not newer" : "no release";
    } else {
        UpdateProgress p;
        Stopwatch sw;
        u = installRelease(c.latest, selfPath(), &p);
        Log::event("update.install", u.ok, sw.ms(), "(cli) " + updateResultJson(u));
    }
    writeText(a.jsonOut, "{\"ok\":" + std::string(u.ok ? "true" : "false") + ",\"check\":" + s +
                             ",\"install\":" + updateResultJson(u) + "}");
    return u.ok ? 0 : 5;
}

int runCli(const Args& a) {
    if (!a.error.empty()) {
        writeText(a.jsonOut, "{\"ok\":false,\"error\":" + json(a.error) + "}");
        return 2;
    }
    if (a.updateCheck || a.updateInstall) return runUpdate(a);
    if (!a.exportLogs.empty()) return runExportLogs(a.exportLogs, a.jsonOut);
    if (a.shotTest) return runShotSelftest(a.jsonOut);
    if (a.linesCheck) return runLinesCheck(a.jsonOut);
    if (!a.shot.empty()) return runShot(a.shot, a.jsonOut);
    if (a.version) {
        writeText(a.jsonOut, "{\"version\":" + json(std::string(kVersion)) + "}");
        return 0;
    }
    if (a.listDevices) {
        std::string s = devicesJson();
        Log::line("devices", "\"list\":" + s);
        writeText(a.jsonOut, s);
        return 0;
    }
    if (a.probe) {
        std::string s = probeLevels(a.seconds > 0 ? std::min(a.seconds, 60.0) : 5.0);
        Log::line("probe", "\"result\":" + s);
        writeText(a.jsonOut, s);
        return 0;
    }
    if (!a.verifyFile.empty() && !a.record) {
        const std::wstring f = lower(a.verifyFile);
        const std::wstring ext = f.size() > 4 ? f.substr(f.size() - 4) : L"";
        std::string v = verifyFile(a.verifyFile, ext == L".m4a" || ext == L".mp3" || ext == L".wav");
        writeText(a.jsonOut, v);
        return v.find("\"ok\":true") != std::string::npos ? 0 : 5;
    }
    if (a.record) {
        std::vector<std::string> warn;
        Config c = loadConfig(&warn);
        for (auto& w : warn) Log::event("config", false, -1, w);
        if (a.fps > 0) c.fps = std::min(60, a.fps);
        if (!a.container.empty()) c.container = lower(a.container);
        if (!a.mic.empty()) c.mic = a.mic;
        if (!a.system.empty()) c.system = a.system;
        if (!a.audioFormat.empty()) c.audioFormat = lower(a.audioFormat);
        RecordOptions o;
        o.testPattern = a.testPattern;
        o.audioOnly = a.audioOnly;
        o.maxSeconds = a.seconds > 0 ? a.seconds : 10;
        o.outPath = a.out;
        Recorder r;
        std::wstring err;
        if (!r.start(c, o, &err)) {
            writeText(a.jsonOut, "{\"ok\":false,\"error\":" + json(err) + ",\"log\":" + json(Log::lastError()) + "}");
            return 3;
        }
        TonePlayer tone;
        const std::vector<double> toneAt = {2.0, 4.0, 6.0};
        if (!a.testTone.empty()) {
            std::string e;
            if (!tone.start(a.testTone, r.qpcStart(), toneAt, 0.3, &e)) Log::event("tone", false, -1, e);
        }
        double pausedAt = -1, resumedAt = -1;   // wall clock seconds since the start (--pause-at)
        auto wall = [&r] { return qpcMs(qpcNow() - r.qpcStart()) / 1000.0; };
        while (!r.loopEnded()) {
            Sleep(10);
            if (a.pauseAt <= 0) continue;
            if (pausedAt < 0 && wall() >= a.pauseAt && r.pause()) pausedAt = wall();
            if (pausedAt >= 0 && resumedAt < 0 && wall() >= pausedAt + a.pauseFor && r.resume()) resumedAt = wall();
        }
        tone.stop();
        r.stop();
        std::string s = r.summary();
        // where the bursts should be in the file: a burst inside the pause is left out, later ones move back
        std::vector<double> toneIn;
        for (double x : toneAt) {
            if (pausedAt < 0 || x < pausedAt) toneIn.push_back(x);
            else if (resumedAt >= 0 && x >= resumedAt) toneIn.push_back(x - (resumedAt - pausedAt));
        }
        if (pausedAt >= 0 && !s.empty() && s.back() == '}')
            s = s.substr(0, s.size() - 1) + fmt(",\"cli_pause\":{\"at_s\":%.3f,\"resumed_s\":%.3f}}", pausedAt, resumedAt);
        if (!a.testTone.empty() && !s.empty() && s.back() == '}')
            s = s.substr(0, s.size() - 1) + ",\"tone\":" + analyzeTones(r.path(), toneIn) + ",\"tone_device\":" + json(tone.device) + "}";
        int code = r.lastOk() ? 0 : 4;
        if (a.verify && r.lastOk()) {
            std::string v = verifyFile(r.path(), r.audioOnly());
            if (v.find("\"ok\":true") == std::string::npos) code = 5;
            if (!s.empty() && s.back() == '}') s = s.substr(0, s.size() - 1) + ",\"verify\":" + v + "}";
        }
        writeText(a.jsonOut, s);
        return code;
    }
    return 2;
}

// ---------------------------------------------------------------- window
// The whole client area is drawn by UiPainter (ui_draw.cpp); there are no child controls.
// Mouse and keyboard hit the same rectangles the painter uses. Device choices open a popup menu.

constexpr UINT WM_APP_STOPPED = WM_APP + 1;
constexpr UINT WM_APP_SHOT = WM_APP + 2;   // the PNG of a screenshot has been written
constexpr UINT WM_APP_LOGS = WM_APP + 3;   // the ZIP of ログを取得 has been written
constexpr UINT WM_APP_UPDATE = WM_APP + 4;      // GitHub has answered (or not) about the latest release
constexpr UINT WM_APP_INSTALLED = WM_APP + 5;   // 今すぐ更新 has swapped the exe (or stopped)
constexpr UINT_PTR kTimer = 1, kTimerNotice = 2, kTimerFlash = 3, kTimerSplit = 4, kTimerSay = 5, kTimerResume = 6,
                   kTimerUpdate = 7;
constexpr int kHotkey = 1, kHotkeyShot = 2, kHotkeyAudio = 3;
constexpr UINT kMenuBase = 1000;
constexpr UINT kSysTopmost = 0x0010, kSysShot = 0x0020;   // system menu items (Windows uses the low 4 bits)
const wchar_t* kClass = L"RecCueWindow";
const wchar_t* kTitle = L"Rec Cue!";

struct Choice { std::wstring label, value; };

struct App {
    HINSTANCE inst = nullptr;
    HWND hwnd = nullptr;
    UINT dpi = 96;
    UiPainter painter;
    Recorder rec;
    AudioEngine probe;          // level check before recording
    Config cfg;
    bool recording = false, starting = false, stopping = false, checking = false, ticking = false;
    bool recAudio = false;      // the recording being started, made or saved is audio only
    int64_t checkStart = 0;
    std::thread stopper;
    std::vector<Choice> micItems, sysItems;
    int micSel = 0, sysSel = 0;
    std::vector<MonitorInfo> monitors;   // for the screen row (refreshed when the menu opens and the displays change)
    int recMonitor = 0;                  // the monitor of the recording being started, made or saved
    bool hotkeyOn = false, shotHotkeyOn = false, audioHotkeyOn = false, topmost = false;
    Indicators ind;             // taskbar dot and screen-corner dot while recording
    ThumbBar thumb;             // record / audio / pause / stop under the taskbar thumbnail
    bool thumbIcons = false;    // ... and whether its icons have been drawn
    UINT taskbarMsg = 0;        // "TaskbarButtonCreated"
    HICON iconBig = nullptr, iconSmall = nullptr;   // the window's icons at its monitor's DPI
    HWND tip = nullptr;         // tooltips of the icon-only tools and the guide
    Lines lines;                // しおり's lines (shiori.h)
    std::wstring line;          // what she says now (chosen when the moment changes) ...
    UiModel::Tone lineTone = UiModel::Tone::Normal;
    std::wstring passing;       // ... or for a few seconds: a screenshot, the pin, a notice while idle
    UiModel::Tone passingTone = UiModel::Tone::Normal;
    int64_t passingUntil = 0;
    Scene warnScene = Scene::Count;   // the warning she is giving about the recording (Count: none)
    int sayMinute = 0;          // the minute of the recording she last looked at
    bool checkMic = false, checkSys = false, checkQuiet = false;   // what she has said about this level check
    int64_t inactiveSince = 0;  // when the window lost the focus (welcome back after a while)
    std::wstring tipText;
    float split = 0, splitFrom = 0, splitTo = 0;   // the deck: 0 halves, 1 recording side open, -1 audio side open
    int64_t splitT0 = 0;
    // screenshot: the picture is taken on this thread, the PNG is written by `shooter`
    std::thread shooter;
    bool shooting = false, shotClip = false;
    int shots = 0, shotSeq = 0;   // in the current recording / since the window opened
    std::wstring shotPath;
    HRESULT shotHr = S_OK;
    double shotSaveMs = 0;
    HANDLE shotDone = nullptr;    // set by the worker when the PNG is written (exit waits on it with a limit)
    bool shotInRec = false;       // taken during a running recording (counted once the PNG exists)
    std::string shotSid;          // ... that recording's session
    double shotAt = 0;            // seconds into that recording
    int64_t noticeUntil = 0;      // a message (screenshot saved) holds the status line for a while when recording
    bool noticeActive = false;
    int64_t flashUntil = 0;       // the screenshot button lights up briefly after a shot
    bool alert = false;           // the recording itself needs attention (amber deck), unlike a passing notice
    std::wstring timer = L"00:00:00", status, pathLabel = L"保存先", path;
    UiModel::Tone tone = UiModel::Tone::Normal;
    std::wstring beforeNotice;    // the status line a notice replaced (put back when it times out while idle)
    UiModel::Tone beforeTone = UiModel::Tone::Normal;
    std::wstring noticeText;
    Part hot = Part::None, pressed = Part::None, focus = Part::Record;
    bool showFocus = false, mouseIn = false;
    View view = View::Main;       // the settings view (the gear) or the update view, in place of the deck and the rows
    // ログを取得: the ZIP is written by `packer`
    std::thread packer;
    bool packing = false;
    std::wstring packPath;
    LogPack packed;
    HANDLE packDone = nullptr;    // set by the worker when the ZIP is written (exit waits on it with a limit)
    double hold[2] = {-90, -90};   // meter peak hold (dBFS) and when it was last raised
    int64_t holdAt[2] = {0, 0};
    // アップデート (update.h): GitHub is asked on `checker` when the window opens; 今すぐ更新 runs on `updater`
    std::wstring version = wide(kVersion);   // what the latest release is compared with (--pretend-version in tests)
    HANDLE mutex = nullptr;     // one window: given up just before 今すぐ更新 opens the new exe
    std::thread checker;
    HANDLE checkDone = nullptr;
    UpdateCheck upd;            // GitHub's answer (read once `checker` has been joined)
    bool updOffered = false;    // a newer release, not skipped: the gear's dot, the version row and the update view
    std::thread updater;
    HANDLE updDone = nullptr;
    UpdateProgress updProg;
    UpdateResult updResult;
    std::wstring updExe;        // the exe being replaced (opened again once it is)
    std::wstring updatedFrom;   // the window was opened by 今すぐ更新 of this version (updated-from.txt)
    bool updBusy = false, updFailed = false;
    std::wstring updLine, updBefore;   // what she says in the update view, and what she said before it
    UiModel::Tone updBeforeTone = UiModel::Tone::Normal;
};
App g;

int S(int v) { return MulDiv(v, static_cast<int>(g.dpi), 96); }
float dip(int px) { return static_cast<float>(px) * 96.f / static_cast<float>(g.dpi); }
void invalidate() { InvalidateRect(g.hwnd, nullptr, FALSE); }

UiModel::Phase phase() {
    using P = UiModel::Phase;
    return g.stopping ? P::Saving : g.starting ? P::Starting : g.recording ? P::Recording : g.checking ? P::Checking : P::Idle;
}

void setStatus(const std::wstring& s, UiModel::Tone t = UiModel::Tone::Normal) {
    g.status = s;
    g.tone = t;
    invalidate();
}

// ---- しおり (shiori.h): a line that fits the moment, chosen when the moment changes (never on a timer while idle),
// and for a few seconds a passing one (a screenshot, the pin, a notice while idle).
SayContext sayContext() {
    SayContext c;
    c.audio = g.recAudio;
    if (g.hotkeyOn) c.recKey = g.cfg.hotkey;
    if (g.audioHotkeyOn) c.audioKey = g.cfg.audioHotkey;
    if (g.shotHotkeyOn) c.shotKey = g.cfg.shotHotkey;
    return c;
}

UiModel::Tone toneOf(Scene s) {
    using T = UiModel::Tone;
    switch (s) {
        case Scene::WarnScreen:
        case Scene::WarnSys:
        case Scene::WarnMic:
        case Scene::WarnNoSys:
        case Scene::CheckQuiet: return T::Warn;
        case Scene::FailStart:
        case Scene::FailSave:
        case Scene::FailShot: return T::Bad;
        case Scene::CheckMic:
        case Scene::CheckSys:
        case Scene::Shot:
        case Scene::ShotIdle:
        case Scene::Recover:
        case Scene::Saved: return T::Good;
        default: return T::Normal;
    }
}

std::wstring pickLine(Scene s, const SayContext& c) {
    std::wstring l = g.lines.pick(s, c);
    Log::event("ui.say", !l.empty(), -1, fmt("scene=%s line=%d", sceneName(s), l.empty() ? 0 : g.lines.lastSource()));
    return l;
}

// What she says from now on, until the next moment.
void say(Scene s, const SayContext& c) {
    g.line = pickLine(s, c);
    g.lineTone = toneOf(s);
    invalidate();
}

void say(Scene s) { say(s, sayContext()); }

// A line for a few seconds, then the current one again.
void passText(const std::wstring& text, UINT ms, UiModel::Tone tone) {
    if (text.empty()) return;
    g.passing = text;
    g.passingTone = tone;
    g.passingUntil = qpcNow() + qpcFreq() * ms / 1000;
    SetTimer(g.hwnd, kTimerSay, ms, nullptr);
    invalidate();
}

void sayPassing(Scene s, UINT ms, const SayContext& c) { passText(pickLine(s, c), ms, toneOf(s)); }

// A short message (screenshot saved) that stays 4 s (8 s for a failure): the recording status lines (refreshed every
// 100 ms) leave it alone unless the recording needs attention, and while idle the previous line comes back afterwards.
void notice(const std::wstring& s, UiModel::Tone t) {
    if (!g.noticeActive) {
        g.beforeNotice = g.status;
        g.beforeTone = g.tone;
    }
    const UINT ms = t == UiModel::Tone::Bad ? 8000 : 4000;
    setStatus(s, t);
    g.noticeText = s;
    g.noticeActive = true;
    g.noticeUntil = qpcNow() + qpcFreq() * ms / 1000;
    SetTimer(g.hwnd, kTimerNotice, ms, nullptr);
    if (!g.recording && !g.starting && !g.stopping) passText(s, ms, t);
}

void dropNotice() {
    KillTimer(g.hwnd, kTimerNotice);
    g.noticeUntil = 0;
    g.noticeActive = false;
}

// Puts the previous line back, unless something else has been said since (a recording ended, a check started).
void noticeOver() {
    dropNotice();
    if (!g.recording && !g.starting && !g.stopping && g.status == g.noticeText) setStatus(g.beforeNotice, g.beforeTone);
}

// The audio side of the deck opens toward the left when audio recording starts and closes after it is saved:
// 260 ms, eased, redrawn every 16 ms only while it moves.
void animateSplit(float to) {
    if (g.split == to && g.splitTo == to) return;
    g.splitFrom = g.split;
    g.splitTo = to;
    g.splitT0 = qpcNow();
    SetTimer(g.hwnd, kTimerSplit, 16, nullptr);
}

void splitStep() {
    const double p = std::clamp(qpcMs(qpcNow() - g.splitT0) / 260.0, 0.0, 1.0);
    const double e = p * p * (3 - 2 * p);
    g.split = static_cast<float>(g.splitFrom + (g.splitTo - g.splitFrom) * e);
    if (p >= 1) {
        g.split = g.splitTo;
        KillTimer(g.hwnd, kTimerSplit);
    }
    invalidate();
}

std::wstring idleStatus() {
    UINT mods = 0, vk = 0;
    const std::wstring s = L"待機中 — 左の ● で録画、右のマイクで録音";
    if (g.hotkeyOn || !parseHotkey(g.cfg.hotkey, &mods, &vk)) return s;
    return s + L" (録画のショートカットはほかのアプリが使用中)";
}

// The window's own icons at this DPI (the .ico holds exact 16-256 px sizes), set with WM_SETICON so the title bar,
// Alt+Tab and the taskbar button use them instead of the class icon scaled from the system DPI.
void setWindowIcons() {
    const int big = GetSystemMetricsForDpi(SM_CXICON, g.dpi), small = GetSystemMetricsForDpi(SM_CXSMICON, g.dpi);
    HICON b = static_cast<HICON>(LoadImageW(g.inst, MAKEINTRESOURCEW(1), IMAGE_ICON, big, big, LR_DEFAULTCOLOR));
    HICON s = static_cast<HICON>(LoadImageW(g.inst, MAKEINTRESOURCEW(1), IMAGE_ICON, small, small, LR_DEFAULTCOLOR));
    if (b) {
        SendMessageW(g.hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(b));
        if (g.iconBig) DestroyIcon(g.iconBig);
        g.iconBig = b;
    }
    if (s) {
        SendMessageW(g.hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(s));
        if (g.iconSmall) DestroyIcon(g.iconSmall);
        g.iconSmall = s;
    }
    Log::event("ui.icon", b && s, -1, fmt("dpi=%u big=%d small=%d", g.dpi, big, small));
}

void resizeToClient() {
    RECT r{0, 0, S(static_cast<int>(kClientW)), S(static_cast<int>(kClientH))};
    AdjustWindowRectExForDpi(&r, GetWindowLongW(g.hwnd, GWL_STYLE), FALSE, GetWindowLongW(g.hwnd, GWL_EXSTYLE), g.dpi);
    SetWindowPos(g.hwnd, nullptr, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

DWORD windowsBuild() {
    using GetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    auto fn = reinterpret_cast<GetVersionFn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
    OSVERSIONINFOW v{};
    v.dwOSVersionInfoSize = sizeof(v);
    return fn && fn(&v) == 0 ? v.dwBuildNumber : 0;
}

// Title bar in the window's own colours (Windows 11; ignored elsewhere) and dark popup menus.
void styleFrame(HWND h) {
    const BOOL dark = TRUE;
    DwmSetWindowAttribute(h, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    const COLORREF cap = kFrameColor, txt = kFrameText, border = kFrameBorder;
    DwmSetWindowAttribute(h, DWMWA_CAPTION_COLOR, &cap, sizeof(cap));
    DwmSetWindowAttribute(h, DWMWA_TEXT_COLOR, &txt, sizeof(txt));
    DwmSetWindowAttribute(h, DWMWA_BORDER_COLOR, &border, sizeof(border));
    // Unnamed uxtheme exports (Windows 10 1903+): SetPreferredAppMode(ForceDark), AllowDarkModeForWindow, FlushMenuThemes.
    // An ordinal that exists on an older build may be a different function, so check the build first.
    if (windowsBuild() < 18362) return;
    HMODULE ux = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!ux) return;
    using SetMode = int(WINAPI*)(int);
    using AllowWindow = BOOL(WINAPI*)(HWND, BOOL);
    using Flush = void(WINAPI*)();
    auto setMode = reinterpret_cast<SetMode>(GetProcAddress(ux, MAKEINTRESOURCEA(135)));
    auto allow = reinterpret_cast<AllowWindow>(GetProcAddress(ux, MAKEINTRESOURCEA(133)));
    auto flush = reinterpret_cast<Flush>(GetProcAddress(ux, MAKEINTRESOURCEA(136)));
    if (setMode) setMode(2);
    if (allow) allow(h, TRUE);
    if (flush) flush();
}

// "マイク配列 (3- デジタル…)" → "マイク配列 (デジタル…)": drops the number Windows adds to repeated names.
std::wstring displayName(std::wstring s) {
    for (size_t p = s.find(L'('); p != std::wstring::npos; p = s.find(L'(', p + 1)) {
        size_t q = p + 1;
        while (q < s.size() && iswdigit(s[q])) ++q;
        if (q > p + 1 && q + 1 < s.size() && s[q] == L'-' && s[q + 1] == L' ') s.erase(p + 1, q + 2 - (p + 1));
    }
    return s;
}

std::wstring modeName(const std::wstring& value) {
    const std::wstring v = lower(value);
    if (v == L"auto") return L"自動";
    if (v == L"default") return L"既定";
    if (v == L"communications") return L"通話用の既定";
    return L"";
}

void fillItems(bool render) {
    std::vector<Choice>& items = render ? g.sysItems : g.micItems;
    const std::wstring& cur = render ? g.cfg.system : g.cfg.mic;
    items.clear();
    auto devs = listAudioDevices(render, nullptr, render);
    std::wstring defC, defM;
    for (auto& d : devs) {
        if (d.defConsole) defC = d.name;
        if (d.defComm) defM = d.name;
    }
    auto shortName = [](const std::wstring& n) { return n.empty() ? std::wstring(L"(なし)") : displayName(n); };
    if (render) items.push_back({L"自動 (音を出しているアプリの機器をすべて)", L"auto"});
    else items.push_back({L"自動 (ヘッドセットを優先)", L"auto"});
    items.push_back({(render ? L"既定のスピーカー — " : L"既定のマイク — ") + shortName(defC), L"default"});
    items.push_back({(render ? L"通話用の既定 — " : L"通話用の既定のマイク — ") + shortName(defM), L"communications"});
    items.push_back({L"なし", L"none"});
    for (auto& d : devs) {
        std::wstring apps;
        for (auto& a : d.apps) {
            const std::wstring e = lower(a.exe);
            if (e == L"(system sounds)" || e.find(L"voicemeeter") != std::wstring::npos) continue;
            apps += (apps.empty() ? L"" : L", ") + a.exe;
        }
        items.push_back({displayName(d.name) + (apps.empty() ? L"" : L"  ← " + apps), d.name});
    }
    int sel = -1;
    const std::wstring lc = normName(cur);
    for (size_t i = 0; i < items.size(); ++i)
        if (normName(items[i].value) == lc) { sel = static_cast<int>(i); break; }
    if (sel < 0) {
        items.push_back({L"(設定ファイルの指定) " + cur, cur});
        sel = static_cast<int>(items.size()) - 1;
    }
    (render ? g.sysSel : g.micSel) = sel;
}

void refreshPath() {
    g.pathLabel = L"保存先";
    g.path = g.cfg.resolvedOutDir();
}

void refreshMonitors() {
    Stopwatch sw;
    g.monitors = listMonitors(true);
    Log::event("ui.monitors", !g.monitors.empty(), sw.ms(), fmt("count=%zu selected=%d", g.monitors.size(), g.cfg.monitor));
}

// "1: メイン 1920×1080" in the field, numbered as in the small map; where it sits is told from the main screen.
// The menu adds the monitor's own name: "2: 右 2560×1440 — DELL U2720Q".
std::wstring screenLabel(size_t i, bool full) {
    const MonitorInfo& mi = g.monitors[i];
    std::wstring where = L"メイン";
    if (!mi.primary && !g.monitors.empty()) {
        const RECT& p = g.monitors[0].rect;   // the main screen comes first
        const long dx = (mi.rect.left + mi.rect.right - p.left - p.right) / 2, dy = (mi.rect.top + mi.rect.bottom - p.top - p.bottom) / 2;
        where = (dx < 0 ? -dx : dx) >= (dy < 0 ? -dy : dy) ? (dx >= 0 ? L"右" : L"左") : (dy >= 0 ? L"下" : L"上");
    }
    std::wstring s = std::to_wstring(i + 1) + L": " + where + L" " + std::to_wstring(mi.rect.right - mi.rect.left) + L"×" +
                     std::to_wstring(mi.rect.bottom - mi.rect.top);
    const std::wstring name = !mi.name.empty() ? mi.name : mi.internal ? L"内蔵ディスプレイ" : L"";
    if (full && !name.empty()) s += L" — " + name;
    return s;
}

// The monitor recorded (while a recording runs, the one it started with, even if the settings file changed since),
// or the configured one; the main one when that is not there (the recording falls back the same way).
int screenSetting() { return g.recording || g.starting || g.stopping ? g.recMonitor : g.cfg.monitor; }
int screenIndex() {
    const int m = screenSetting();
    return m >= 0 && m < static_cast<int>(g.monitors.size()) ? m : 0;
}

// The 100 ms tick runs only while something moves (recording, level check, saving); idle costs nothing.
void syncTimer() {
    const bool want = g.recording || g.checking || g.stopping;
    if (want && !g.ticking) g.ticking = SetTimer(g.hwnd, kTimer, 100, nullptr) != 0;
    if (!want && g.ticking) {
        KillTimer(g.hwnd, kTimer);
        g.ticking = false;
    }
}

float pulse() {   // one slow breath every 2 s for the recording lamp
    const double t = qpcMs(qpcNow()) / 1000.0;
    return static_cast<float>(0.5 + 0.5 * std::cos(t * 3.14159265358979));
}

const wchar_t* audioFormatLabel(AudioFile f) { return f == AudioFile::Mp3 ? L"MP3" : f == AudioFile::Wav ? L"WAV" : L"M4A"; }

UiModel model() {
    UiModel m;
    m.phase = phase();
    m.timer = g.timer;
    m.status = g.status;
    m.tone = g.tone;
    m.pathLabel = g.pathLabel;
    m.path = g.path;
    const AudioEngine* e = g.recording ? &g.rec.audio : g.checking ? &g.probe : nullptr;
    for (int k = 0; k < 2; ++k) {
        ChannelView& c = k == 0 ? m.mic : m.sys;
        const auto& items = k == 0 ? g.micItems : g.sysItems;
        const int sel = k == 0 ? g.micSel : g.sysSel;
        if (sel >= 0 && sel < static_cast<int>(items.size())) {
            c.choice = items[static_cast<size_t>(sel)].label;
            c.mode = modeName(items[static_cast<size_t>(sel)].value);
        }
        if (!e) continue;
        const AudioEngine::Meter& mt = k == 0 ? e->mic : e->sys;
        c.active = mt.active.load();
        c.db = mt.peakDb10.load() / 10.0;
        c.hold = g.hold[k];
        c.device = displayName(k == 0 ? e->micName() : e->sysName());
        c.warn = k == 1 && g.recording && c.active && mt.silentMs.load() > 60000;   // the other party may be missing
    }
    m.audioOnly = g.recAudio && (g.recording || g.starting || g.stopping);
    m.screen.active = (g.recording || g.starting || g.stopping) && !g.recAudio;
    m.screen.selected = screenIndex();
    for (auto& mi : g.monitors) m.screen.monitors.push_back(mi.rect);
    if (g.monitors.empty()) m.screen.choice = L"メインの画面";
    else if (screenSetting() != m.screen.selected)
        m.screen.choice = L"画面 " + std::to_wstring(screenSetting() + 1) + L" が見つかりません → " + screenLabel(0, false);
    else m.screen.choice = screenLabel(static_cast<size_t>(m.screen.selected), false);
    m.split = g.split;
    m.hot = g.hot;
    m.pressed = g.pressed;
    m.focus = g.focus;
    m.showFocus = g.showFocus;
    m.pulse = g.recording ? pulse() : 1.f;
    m.topmost = g.topmost;
    m.shots = g.shots;
    m.shotFlash = qpcNow() < g.flashUntil;
    m.alert = g.alert && g.recording;
    m.paused = g.recording && g.rec.paused();
    m.audioFormat = audioFormatLabel(audioFileOf(g.cfg.audioFormat));
    m.recHotkey = g.hotkeyOn ? g.cfg.hotkey : L"";
    m.shotHotkey = g.shotHotkeyOn ? g.cfg.shotHotkey : L"";
    m.audioHotkey = g.audioHotkeyOn ? g.cfg.audioHotkey : L"";
    // しおり: a passing line first, otherwise the moment's
    const bool passing = qpcNow() < g.passingUntil;
    m.comment = passing ? g.passing : g.line;
    m.commentTone = passing ? g.passingTone : g.lineTone;
    m.view = g.view;
    if (g.updOffered) m.newVersion = g.upd.latest.version;
    if (g.view == View::Update) {
        using S = UiModel::Update::State;
        const Release& r = g.upd.latest;
        m.update.version = r.version;
        m.update.current = g.version;
        m.update.published = r.published;
        m.update.size = r.size;
        m.update.got = g.updProg.got;
        m.update.notes = r.notes;
        m.update.state = g.updFailed ? S::Failed : !g.updBusy ? S::Offer
                         : g.updProg.stage == UpdateProgress::Download ? S::Download
                         : g.updProg.stage == UpdateProgress::Verify   ? S::Verify
                                                                       : S::Swap;
        const std::string& st = g.updResult.step;
        m.update.error = st == "folder"                       ? L"この場所には書き込めません"
                         : st == "download"                   ? L"受け取れませんでした"
                         : st == "size" || st == "sha256"     ? L"受け取ったファイルに誤りがありました"
                         : st == "smoke"                      ? L"新しい版を起動できませんでした"
                                                              : L"入れ替えられませんでした";
    }
    if (g.view == View::Settings) {   // the folders and the shortcuts in use (while recording: as that recording started)
        m.outDir = g.cfg.resolvedOutDir();
        m.shotDir = g.cfg.shotDir.empty() ? L"" : g.cfg.resolvedShotDir();
        const std::pair<const std::wstring*, bool> keys[3] = {
            {&g.cfg.hotkey, g.hotkeyOn}, {&g.cfg.audioHotkey, g.audioHotkeyOn}, {&g.cfg.shotHotkey, g.shotHotkeyOn}};
        for (int i = 0; i < 3; ++i) {
            UINT mods = 0, vk = 0;
            const bool set = parseHotkey(*keys[i].first, &mods, &vk);
            m.keys[i].text = set ? *keys[i].first : L"なし";
            m.keys[i].taken = set && !keys[i].second;
        }
        m.logsBusy = g.packing;
    }
    return m;
}

void setTopmost(bool on, bool save) {
    const bool ok = SetWindowPos(g.hwnd, on ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) != 0;
    if (ok) g.topmost = on;
    if (ok && save) {
        saveConfigValue(L"topmost", on ? L"1" : L"0");
        sayPassing(on ? Scene::PinOn : Scene::PinOff, 3000, sayContext());
    }
    CheckMenuItem(GetSystemMenu(g.hwnd, FALSE), kSysTopmost, MF_BYCOMMAND | (g.topmost ? MF_CHECKED : MF_UNCHECKED));
    Log::event("ui.topmost", ok, -1, std::string(on ? "on" : "off") + (save ? "" : " (from config)"));
    invalidate();
}

// Screenshot of the recorded monitor without this window (see shot.h). The capture takes a few tens of ms here;
// the PNG (about 0.1-0.3 s for 1080p) is written on a worker and reported with WM_APP_SHOT.
void takeShot(const char* via) {
    if (g.shooting) return;   // the previous PNG is still being written
    if (!g.recording && !g.starting && !g.stopping) g.cfg = loadConfig();   // picks up a changed monitor / folder
    const int seq = ++g.shotSeq;
    const int monitor = screenSetting();   // the recorded one while recording
    ShotImage img;
    ShotInfo info;
    std::string err;
    Stopwatch sw;
    const bool ok = captureMonitor(monitor, g.hwnd, &img, &info, &err);
    Log::event("shot.capture", ok, sw.ms(),
               fmt("shot=%d via=%s monitor=%d %ux%u excluded=%s hidden=%s flush_ms=%.1f blt_ms=%.1f recording=%s%s%s", seq, via,
                   monitor, img.w, img.h, info.excluded ? "true" : "false", info.hidden ? "true" : "false",
                   info.flushMs, info.bltMs, g.recording ? "true" : "false", err.empty() ? "" : " error=", err.c_str()));
    if (!ok) {
        notice(L"スクショを撮れませんでした (" + wide(err) + L")", UiModel::Tone::Bad);   // in her bubble while idle
        if (g.recording) sayPassing(Scene::FailShot, 8000, sayContext());
        return;
    }
    Stopwatch cs;
    g.shotClip = g.cfg.shotClipboard && copyToClipboard(g.hwnd, img);
    if (g.cfg.shotClipboard) Log::event("shot.clipboard", g.shotClip, cs.ms(), fmt("shot=%d", seq));
    const std::wstring dir = g.cfg.resolvedShotDir();
    ensureDir(dir);
    g.shotPath = uniquePath(dir, g.cfg.namePrefix + L" " + stamp(), L".png");
    g.shotInRec = g.recording && !g.stopping;   // counted in that recording once the PNG exists (onShotSaved)
    g.shotSid = g.shotInRec ? Log::session() : std::string();
    g.shotAt = g.shotInRec ? g.rec.elapsedSec() : 0;
    g.shooting = true;
    g.flashUntil = qpcNow() + qpcFreq() * 6 / 10;   // the button lights up for 0.6 s
    SetTimer(g.hwnd, kTimerFlash, 650, nullptr);
    invalidate();
    if (g.shooter.joinable()) g.shooter.join();
    if (!g.shotDone) g.shotDone = CreateEventW(nullptr, TRUE, TRUE, nullptr);
    if (g.shotDone) ResetEvent(g.shotDone);
    g.shooter = std::thread([img = std::move(img)] {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        Stopwatch es;
        g.shotHr = savePng(img, g.shotPath);
        g.shotSaveMs = es.ms();
        CoUninitialize();
        if (g.shotDone) SetEvent(g.shotDone);
        PostMessageW(g.hwnd, WM_APP_SHOT, 0, 0);
    });
}

// The PNG normally takes well under a second; a save folder that stopped answering (an unreachable share) must not
// keep a closed Rec Cue! alive without a window, holding the single-instance mutex. false: still writing after `ms`.
bool waitShooter(DWORD ms) {
    if (!g.shooter.joinable()) return true;
    if (g.shotDone && WaitForSingleObject(g.shotDone, ms) != WAIT_OBJECT_0) {
        Log::event("shot.wait", false, ms, "the screenshot is still being written; not waiting any longer");
        return false;
    }
    g.shooter.join();
    return true;
}

void onShotSaved() {
    if (g.shooter.joinable()) g.shooter.join();   // also makes the worker's results visible here
    g.shooting = false;
    const size_t k = g.shotPath.find_last_of(L'\\');
    const std::wstring name = k == std::wstring::npos ? g.shotPath : g.shotPath.substr(k + 1);
    WIN32_FILE_ATTRIBUTE_DATA fa{};
    const uint64_t bytes = GetFileAttributesExW(g.shotPath.c_str(), GetFileExInfoStandard, &fa)
                               ? (static_cast<uint64_t>(fa.nFileSizeHigh) << 32) | fa.nFileSizeLow
                               : 0;
    const bool saved = SUCCEEDED(g.shotHr);
    // listed in the recording only if it exists and that same recording is still running (not stopping / a new one)
    const bool listed = saved && g.shotInRec && g.recording && !g.stopping && Log::session() == g.shotSid;
    if (listed) {
        g.rec.noteShot(g.shotPath, g.shotAt);
        ++g.shots;
    }
    Log::event("shot.save", saved, g.shotSaveMs,
               fmt("shot=%d bytes=%llu clipboard=%s in_recording=%s ", g.shotSeq, static_cast<unsigned long long>(bytes),
                   g.shotClip ? "true" : "false", listed ? "true" : "false") + u8(g.shotPath),
               g.shotHr);
    // the deck's status line while recording; しおり says it either way
    if (saved && g.recording)
        notice((listed ? L"スクショ " + std::to_wstring(g.shots) + L" 枚目を保存" : std::wstring(L"スクショを保存")) +
                   (g.shotClip ? L"し、コピーしました" : L"しました: " + name),
               UiModel::Tone::Good);
    else if (!saved && g.recording)
        notice(L"スクショを保存できませんでした (設定 → ログを取得)", UiModel::Tone::Bad);
    SayContext c = sayContext();
    c.n = g.shots;
    c.clip = g.shotClip;
    if (saved) sayPassing(listed ? Scene::Shot : Scene::ShotIdle, 4000, c);
    else sayPassing(Scene::FailShot, 8000, c);
}

void stopCheck() {
    if (!g.checking) return;
    g.probe.stopNow();
    g.checking = false;
    syncTimer();
    invalidate();
}

void startCheck() {
    stopCheck();
    g.cfg = loadConfig();
    std::vector<std::string> notes;
    g.probe.open(g.cfg, &notes);
    g.probe.go(qpcNow(), nullptr);
    g.checking = true;
    g.checkStart = qpcNow();
    g.checkMic = g.checkSys = g.checkQuiet = false;
    say(Scene::Check);
    g.hold[0] = g.hold[1] = -90;
    setStatus(notes.empty() ? L"音量を確認中です (60 秒で自動で止まります)" : L"確認中: " + wide(notes.front()));
    syncTimer();
}

// ---- the buttons under the taskbar thumbnail (indicator.h): what they show follows the recording

// The taskbar's own theme (Settings > Personalization > Colors > "Windows mode"); the button glyphs follow it.
bool lightTaskbar() {
    DWORD v = 0, n = sizeof(v);
    return RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                        L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &v, &n) == ERROR_SUCCESS &&
           v != 0;
}

void makeThumbIcons() {
    const int px = GetSystemMetricsForDpi(SM_CXSMICON, GetDpiForSystem());   // the size the taskbar draws them at
    const bool light = lightTaskbar();
    const ThumbGlyph glyphs[ThumbBar::kIcons] = {ThumbGlyph::Record, ThumbGlyph::Audio, ThumbGlyph::Pause,
                                                 ThumbGlyph::Resume, ThumbGlyph::Stop};
    HICON icons[ThumbBar::kIcons];
    int made = 0;
    Stopwatch sw;
    for (int i = 0; i < ThumbBar::kIcons; ++i) made += (icons[i] = g.painter.thumbIcon(glyphs[i], px, light)) != nullptr;
    g.thumb.setIcons(icons);
    g.thumbIcons = true;
    Log::event("ui.thumbicons", made == ThumbBar::kIcons, sw.ms(), fmt("%dpx light=%s made=%d", px, light ? "true" : "false", made));
}

ThumbView thumbView() {
    ThumbView v;
    v.recording = g.recording;   // also while it is being saved
    v.busy = g.starting || g.stopping;
    v.paused = g.recording && g.rec.paused();
    v.audioOnly = g.recAudio;
    if (g.hotkeyOn) v.recKey = g.cfg.hotkey;
    if (g.audioHotkeyOn) v.audioKey = g.cfg.audioHotkey;
    v.audioFile = audioFormatLabel(audioFileOf(g.cfg.audioFormat));
    return v;
}

void syncThumb() { g.thumb.show(thumbView()); }

void showView(View v);

// audio: microphone + PC audio only (MP3 / WAV / M4A), shown in blue; otherwise the screen as well (.mp4), in red.
void startRecording(bool audio) {
    if (g.recording || g.stopping || g.starting) return;
    if (g.updBusy) {   // the exe is being replaced (a few seconds): the window is about to open again
        notice(L"更新の途中です。数秒お待ちください", UiModel::Tone::Warn);
        return;
    }
    if (g.view != View::Main) showView(View::Main);   // the deck shows the recording (its time and ■)
    stopCheck();
    std::vector<std::string> warn;
    g.cfg = loadConfig(&warn);
    for (auto& w : warn) Log::event("config", false, -1, w);
    g.recMonitor = g.cfg.monitor;
    g.starting = true;
    g.recAudio = audio;
    syncThumb();
    dropNotice();
    g.passingUntil = 0;
    g.warnScene = Scene::Count;
    g.sayMinute = 0;
    say(Scene::Starting);
    setStatus(L"準備中…");
    // the title changes already now, so --quit leaves a starting recording alone
    SetWindowTextW(g.hwnd, audio ? L"● 録音中 - Rec Cue!" : L"● 録画中 - Rec Cue!");
    RedrawWindow(g.hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
    std::wstring err;
    SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
    RecordOptions o;
    o.audioOnly = audio;
    const bool ok = g.rec.start(g.cfg, o, &err);
    g.starting = false;
    if (!ok) {
        g.recAudio = false;
        syncThumb();
        SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);
        SetWindowTextW(g.hwnd, kTitle);
        setStatus(L"開始できませんでした: " + err, UiModel::Tone::Bad);
        say(Scene::FailStart);
        MessageBoxW(g.hwnd, err.c_str(), kTitle, MB_ICONERROR | MB_OK);
        return;
    }
    g.recording = true;
    g.shots = 0;
    g.alert = false;
    g.timer = L"00:00:00";
    g.hold[0] = g.hold[1] = -90;
    setStatus(audio ? L"録音中 — ■ を押すと停止して保存します" : L"録画中 — ■ を押すと停止して保存します");
    g.pathLabel = audio ? L"録音中" : L"録画中";
    g.path = g.rec.path();
    say(audio ? Scene::Audio : Scene::Rec);
    // audio only has no recorded monitor: its corner dot goes on the main one
    g.ind.start(g.hwnd, audio ? RecKind::Audio : RecKind::Screen, audio ? 0 : g.cfg.monitor);
    syncThumb();
    animateSplit(audio ? -1.f : 1.f);   // now that it records (the start above holds this thread for a moment)
    syncTimer();
}

void stopRecording() {
    if (!g.recording || g.stopping) return;
    g.stopping = true;
    g.ind.stop();   // nothing is captured any more; only the file is being finished
    syncThumb();
    setStatus(L"ファイルを仕上げています…");
    say(Scene::Saving);
    syncTimer();
    g.stopper = std::thread([] {
        g.rec.stop();
        PostMessageW(g.hwnd, WM_APP_STOPPED, 0, 0);
    });
}

void onStopped() {
    if (g.stopper.joinable()) g.stopper.join();
    SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);
    g.recording = false;
    g.stopping = false;
    dropNotice();   // the saved message below must stay
    g.alert = false;
    syncTimer();
    SetWindowTextW(g.hwnd, kTitle);
    const std::wstring p = g.rec.path();   // shown in the path bar with its name highlighted
    WIN32_FILE_ATTRIBUTE_DATA fa{};
    double mb = 0;
    if (GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &fa))
        mb = static_cast<double>((static_cast<uint64_t>(fa.nFileSizeHigh) << 32) | fa.nFileSizeLow) / 1048576.0;
    wchar_t b[64];
    swprintf(b, 64, L" (%.1f MB)", mb);
    std::string reason = g.rec.autoStopReason();
    if (g.rec.lastOk())
        setStatus((reason.empty() ? L"保存しました" : L"自動で停止して保存しました") + std::wstring(b) +
                      (g.rec.durationPatched() ? L"" : L" Mac では 0 秒に見える場合あり"),
                  reason.empty() ? UiModel::Tone::Good : UiModel::Tone::Warn);
    else
        setStatus(L"保存に失敗しました (設定 → ログを取得)", UiModel::Tone::Bad);
    SayContext c = sayContext();   // しおり reports it until the next moment
    c.shots = g.shots;
    c.sec = g.rec.elapsedSec();
    c.mb = mb;
    c.autoStop = !reason.empty();
    say(g.rec.lastOk() ? Scene::Saved : Scene::FailSave, c);
    animateSplit(0);   // back to halves
    g.recAudio = false;
    syncThumb();
    g.pathLabel = L"保存先";
    g.path = p;
}

void tick() {
    uiCounters().ticks++;
    if (g.recording && !g.stopping) {
        const int t = static_cast<int>(g.rec.elapsedSec());
        wchar_t b[32];
        swprintf(b, 32, L"%02d:%02d:%02d", t / 3600, t / 60 % 60, t % 60);
        g.timer = b;
        using T = UiModel::Tone;
        const bool audio = g.rec.audioOnly();
        const bool held = g.rec.paused();   // nothing is recorded, so there is nothing to warn about
        const std::wstring what = audio ? L"録音中" : L"録画中";
        std::wstring st = what + L" — ■ を押すと停止して保存します";
        T tone = T::Normal;
        Scene warn = Scene::Count;
        if (held) {
            st = L"一時停止中 — ▶ で再開、■ で停止して保存します";
        } else if (!audio && !g.rec.videoCapturing()) {
            st = L"画面を取り込めていません (ロック中など)";
            tone = T::Warn;
            warn = Scene::WarnScreen;
        } else if (g.rec.audio.sys.active.load() && g.rec.audio.sys.silentMs.load() > 60000) {
            st = L"PC の音声が 1 分以上無音です。出力先を確認";
            tone = T::Warn;
            warn = Scene::WarnSys;
        } else if (!g.rec.audio.mic.active.load()) {
            st = what + L" — マイクなし";
            tone = lower(g.cfg.mic) == L"none" ? T::Normal : T::Warn;
            if (tone == T::Warn) warn = Scene::WarnMic;
        } else if (!g.rec.audio.sys.active.load()) {
            st = what + L" — PC の音声なし";
            tone = lower(g.cfg.system) == L"none" ? T::Normal : T::Warn;
            if (tone == T::Warn) warn = Scene::WarnNoSys;
        }
        const bool alert = tone == T::Warn;   // the recording needs attention: amber deck
        const bool newAlert = alert && !g.alert;
        g.alert = alert;
        if (!g.noticeActive || newAlert) {    // a warning that just appeared goes before a passing notice
            if (g.noticeActive) dropNotice();
            g.status = st;
            g.tone = tone;
        }
        // しおり: which warning as soon as it has lasted 2 s, a word when it is over, every 10 minutes and milestones
        if (!held && warn != g.warnScene && (warn == Scene::Count || t >= 2)) {
            say(warn != Scene::Count ? warn : Scene::Recover);
            g.warnScene = warn;
        }
        if (t / 60 != g.sayMinute) {
            g.sayMinute = t / 60;
            const Scene on = audio ? Scene::AudioOn : Scene::RecOn;
            if (warn == Scene::Count && (g.sayMinute % 10 == 0 || g.lines.milestone(on, g.sayMinute))) {
                SayContext c = sayContext();
                c.n = g.sayMinute;
                c.sec = g.sayMinute * 60.0;
                say(on, c);
            }
        }
        g.ind.tick(pulse());
        if (g.rec.loopEnded()) stopRecording();
    }
    if (g.checking) {   // しおり answers the meters: the microphone heard, PC audio heard, no microphone sound for 8 s
        const double ms = qpcMs(qpcNow() - g.checkStart);
        const bool micOn = g.probe.mic.active.load();
        if (ms > 600 && !g.checkMic && micOn && g.probe.mic.peakDb10.load() > -450) {
            g.checkMic = true;
            say(Scene::CheckMic);
        } else if (ms > 600 && !g.checkSys && g.probe.sys.active.load() && g.probe.sys.peakDb10.load() > -500) {
            g.checkSys = true;
            say(Scene::CheckSys);
        } else if (ms > 8000 && !g.checkMic && !g.checkQuiet && micOn) {
            g.checkQuiet = true;
            say(Scene::CheckQuiet);
        }
    }
    if (g.checking && qpcMs(qpcNow() - g.checkStart) > 60000) {
        stopCheck();
        setStatus(L"音量の確認を止めました");
        say(Scene::CheckEnd);
    }
    const AudioEngine* e = g.recording ? &g.rec.audio : g.checking ? &g.probe : nullptr;
    const int64_t now = qpcNow();
    for (int k = 0; k < 2; ++k) {
        const double db = e ? (k == 0 ? e->mic : e->sys).peakDb10.load() / 10.0 : -90;
        if (db >= g.hold[k]) {
            g.hold[k] = db;
            g.holdAt[k] = now;
        } else if (qpcMs(now - g.holdAt[k]) > 1500) {
            g.hold[k] = std::max(db, g.hold[k] - 1.5);   // falls 15 dB/s after 1.5 s
        }
    }
    invalidate();
}

std::wstring menuText(const std::wstring& s) {   // '&' would underline the next letter in a menu
    std::wstring o;
    for (wchar_t ch : s) {
        if (ch == L'&') o += L'&';
        o += ch;
    }
    return o;
}

bool enabled(Part p) {   // as usable() in ui_draw.cpp
    const bool choice = p == Part::UpdNow || p == Part::UpdLater || p == Part::UpdSkip ||
                        (p == Part::Back && g.view == View::Update);
    return inView(p, g.view) && partEnabled(phase(), g.recAudio, p) && !(p == Part::Logs && g.packing) &&
           !(choice && g.updBusy) && !(p == Part::About && !g.updOffered);
}

// Opens `menu` under the field of `part` and returns the command picked (0: none).
UINT menuUnder(HMENU menu, Part part) {
    const D2D1_RECT_F f = partRect(part);
    RECT ex{S(static_cast<int>(f.left)), S(static_cast<int>(f.top)), S(static_cast<int>(f.right)), S(static_cast<int>(f.bottom))};
    MapWindowPoints(g.hwnd, nullptr, reinterpret_cast<POINT*>(&ex), 2);
    TPMPARAMS tp{sizeof(tp), ex};
    g.pressed = Part::None;
    invalidate();
    return static_cast<UINT>(TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_VERTICAL,
                                              ex.left, ex.bottom + S(2), g.hwnd, &tp));
}

void chooseDevice(bool render) {
    const Part part = render ? Part::Sys : Part::Mic;
    if (!enabled(part)) return;
    g.cfg = loadConfig();
    fillItems(render);   // pick up devices plugged in since the window opened
    const auto& items = render ? g.sysItems : g.micItems;
    const int sel = render ? g.sysSel : g.micSel;
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    for (size_t i = 0; i < items.size(); ++i) {
        if (i == 4) AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);   // presets above, devices below
        AppendMenuW(menu, MF_STRING | (static_cast<int>(i) == sel ? MF_CHECKED : MF_UNCHECKED), kMenuBase + i,
                    menuText(items[i].label).c_str());
    }
    const UINT cmd = menuUnder(menu, part);
    DestroyMenu(menu);
    if (!IsWindow(g.hwnd)) return;
    if (!enabled(part)) {   // recording started (hotkey) while the menu was open: keep the setting
        invalidate();
        return;
    }
    if (cmd >= kMenuBase && cmd < kMenuBase + items.size()) {
        const size_t i = cmd - kMenuBase;
        saveConfigValue(render ? L"system" : L"mic", items[i].value);
        g.cfg = loadConfig();
        (render ? g.sysSel : g.micSel) = static_cast<int>(i);
        if (g.checking) startCheck();
    }
    invalidate();
}

// The monitor to record (the screenshots and the corner dot follow it). Saved as monitor=N, which the next recording
// reads; a number no longer there is listed too, greyed, as the recording then uses the main screen.
void chooseScreen() {
    if (!enabled(Part::Screen)) return;
    g.cfg = loadConfig();
    refreshMonitors();   // a display plugged in since
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    const int sel = screenIndex();
    const bool missing = g.cfg.monitor != sel;
    for (size_t i = 0; i < g.monitors.size(); ++i)
        AppendMenuW(menu, MF_STRING | (!missing && static_cast<int>(i) == sel ? MF_CHECKED : MF_UNCHECKED), kMenuBase + i,
                    menuText(screenLabel(i, true)).c_str());
    if (missing) {
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING | MF_CHECKED | MF_GRAYED, 0,
                    (L"(設定ファイルの指定) 画面 " + std::to_wstring(g.cfg.monitor + 1) + L" — 見つからないので 1 で録ります").c_str());
    }
    const UINT cmd = menuUnder(menu, Part::Screen);
    DestroyMenu(menu);
    if (!IsWindow(g.hwnd)) return;
    if (!enabled(Part::Screen)) {   // recording started (hotkey, taskbar) while the menu was open: keep the setting
        invalidate();
        return;
    }
    if (cmd >= kMenuBase && cmd < kMenuBase + g.monitors.size()) {
        const size_t i = cmd - kMenuBase;
        const bool ok = saveConfigValue(L"monitor", std::to_wstring(i));
        g.cfg = loadConfig();
        const RECT& r = g.monitors[i].rect;
        Log::event("ui.monitor", ok, -1,
                   fmt("monitor=%zu %s %ldx%ld at (%ld,%ld)", i, u8(g.monitors[i].device).c_str(), r.right - r.left,
                       r.bottom - r.top, r.left, r.top));
    }
    invalidate();
}

// What a recording of sound alone makes. Saved as audio_format=..., used from the next recording on.
void chooseAudioFormat() {
    if (!enabled(Part::AudioFormat)) return;
    g.cfg = loadConfig();
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    const std::pair<const wchar_t*, const wchar_t*> items[] = {{L"mp3", L"MP3"}, {L"wav", L"WAV"}, {L"m4a", L"M4A"}};
    const AudioFile cur = audioFileOf(g.cfg.audioFormat);
    for (size_t i = 0; i < 3; ++i)
        AppendMenuW(menu, MF_STRING | (audioFileOf(items[i].first) == cur ? MF_CHECKED : MF_UNCHECKED), kMenuBase + i,
                    items[i].second);
    const UINT cmd = menuUnder(menu, Part::AudioFormat);
    DestroyMenu(menu);
    if (!IsWindow(g.hwnd)) return;
    if (!enabled(Part::AudioFormat)) {   // recording started while the menu was open: keep the setting
        invalidate();
        return;
    }
    if (cmd >= kMenuBase && cmd < kMenuBase + 3) {
        const wchar_t* v = items[cmd - kMenuBase].first;
        const bool ok = saveConfigValue(L"audio_format", v);
        g.cfg = loadConfig();
        Log::event("ui.audio_format", ok, -1, u8(v));
        syncThumb();   // its tip names the format
    }
    invalidate();
}

// The audio button starts audio only, or stops an audio-only recording; ● / ■ stops whichever recording runs.
void toggleAudio() {
    if (g.recording && g.recAudio) stopRecording();
    else if (!g.recording) startRecording(true);
}

// ● starts recording the screen, or stops it; while audio only records it waits (the right side stops that).
void toggleRecord() {
    if (g.recording && !g.recAudio) stopRecording();
    else if (!g.recording) startRecording(false);
}

// ❚❚ beside ■ (either side): nothing is recorded until ▶, and the file then goes on where it stopped. The title
// keeps 録画中 / 録音中 so that --quit (deploy) still leaves a paused recording alone.
void togglePause() {
    if (!g.recording || g.stopping) return;
    const bool resume = g.rec.paused();
    if (!(resume ? g.rec.resume() : g.rec.pause())) return;
    g.ind.setPaused(!resume);
    g.alert = false;
    g.warnScene = Scene::Count;   // a warning that still holds after the pause is said again
    if (g.noticeActive) dropNotice();
    const bool audio = g.recAudio;
    SetWindowTextW(g.hwnd, resume ? (audio ? L"● 録音中 - Rec Cue!" : L"● 録画中 - Rec Cue!")
                                  : (audio ? L"● 録音中 (一時停止) - Rec Cue!" : L"● 録画中 (一時停止) - Rec Cue!"));
    syncThumb();
    say(resume ? Scene::Resume : Scene::Pause);
    tick();   // the status line and the time now
}

// A button under the taskbar thumbnail: the same as the window's own buttons.
void onThumb(UINT id) {
    Log::event("ui.thumb", true, -1,
               fmt("button=%s recording=%s paused=%s", id == kThumbRecord ? "record" : id == kThumbAudio ? "audio"
                                                       : id == kThumbPause ? "pause"
                                                                           : "stop",
                   g.recording ? "true" : "false", g.recording && g.rec.paused() ? "true" : "false"));
    const bool idle = !g.recording && !g.starting && !g.stopping;
    if (id == kThumbRecord && idle) startRecording(false);
    if (id == kThumbAudio && idle) startRecording(true);
    // ▶ closes the preview, which fades out for a moment: resume once it is gone, so the taskbar is not recorded
    // (starting has its own second or so before the first frame)
    if (id == kThumbPause && g.recording && g.rec.paused()) SetTimer(g.hwnd, kTimerResume, 300, nullptr);
    else if (id == kThumbPause) togglePause();
    if (id == kThumbStop) stopRecording();
}

// Only web addresses from the settings file are opened (never a program or a file).
void openUrl(const std::wstring& url) {
    const std::wstring u = lower(url);
    if (u.rfind(L"https://", 0) != 0 && u.rfind(L"http://", 0) != 0) {
        notice(L"アドレスは https:// で始めてください (設定ファイル)", UiModel::Tone::Warn);
        return;
    }
    ShellExecuteW(g.hwnd, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

std::wstring keyText(const std::wstring& key, bool registered) {
    UINT mods = 0, vk = 0;
    if (!parseHotkey(key, &mods, &vk)) return L"なし";
    return registered ? key : key + L" (ほかのアプリが使用中)";
}

void moveTooltips();

// The gear: the settings view in place of the deck and the rows (the header, しおり and the band stay), or back; or
// the update view. What she said in the update view goes with it.
void showView(View v) {
    if (g.view == v) return;
    if (v == View::Settings && !g.recording && !g.starting && !g.stopping) g.cfg = loadConfig();   // as the file is now
    if (g.view == View::Update && g.line == g.updLine) {
        g.line = g.updBefore;
        g.lineTone = g.updBeforeTone;
    }
    g.view = v;
    g.hot = g.pressed = Part::None;
    g.focus = v == View::Main ? Part::Gear : v == View::Update && !g.updBusy ? Part::UpdNow : Part::Back;
    moveTooltips();   // each view has its own tools
    Log::event("ui.view", true, -1, v == View::Settings ? "settings" : v == View::Update ? "update" : "main");
    invalidate();
}

// ---- アップデート (update.h)

// What she says while the update view is open (put back as it was when the view closes).
void sayUpdate(const std::wstring& s, UiModel::Tone t) {
    if (g.line != g.updLine || g.updLine.empty()) {
        g.updBefore = g.line;
        g.updBeforeTone = g.lineTone;
    }
    g.line = g.updLine = s;
    g.lineTone = t;
    invalidate();
}

void openUpdate() {
    if (!g.updOffered) return;
    showView(View::Update);
    if (!g.updBusy && !g.updFailed) sayUpdate(withVersion(kUpdOffer, g.upd.latest.version), UiModel::Tone::Normal);
}

// When the window opens: GitHub's latest release, on a worker (the answer comes as WM_APP_UPDATE). update_check=0 in
// the settings file keeps Rec Cue! from asking.
void startUpdateCheck() {
    if (!g.cfg.updateCheck) {
        Log::event("update.check", true, -1, "off (update_check=0)");
        return;
    }
    if (!g.checkDone) g.checkDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g.checker = std::thread([] {
        g.upd = checkLatest();
        if (g.checkDone) SetEvent(g.checkDone);
        PostMessageW(g.hwnd, WM_APP_UPDATE, 0, 0);
    });
}

// A newer release that was not skipped: shown at once when nothing is going on (as OBS does), otherwise a dot on the
// gear and the settings' version row wait for it. No answer is not mentioned (only logged).
void onUpdateChecked() {
    if (g.checker.joinable()) g.checker.join();   // also makes the worker's answer visible here
    const UpdateCheck& c = g.upd;
    const bool newer = c.ok && compareVersions(c.latest.version, g.version) > 0;
    const bool skipped = newer && c.latest.version == g.cfg.updateSkip;
    Log::event("update.check", c.ok, c.ms,
               fmt("status=%d latest=%s current=%s newer=%d skipped=%d", c.status, u8(c.latest.version).c_str(),
                   u8(g.version).c_str(), newer, skipped) + (c.error.empty() ? "" : " " + c.error));
    if (!newer || skipped) return;
    g.updOffered = true;
    const bool now = phase() == UiModel::Phase::Idle && g.view == View::Main;
    Log::event("update.offer", true, -1, u8(c.latest.version) + (now ? " (view)" : " (dot on the gear: busy)"));
    if (now) openUpdate();
    invalidate();
}

// 今すぐ更新: the release is downloaded, checked and swapped in on `updater` (WM_APP_INSTALLED). Recording waits for
// those few seconds, and the title says so, so that deploy.sh's --quit leaves it alone too. After a failure the
// button opens the release's page instead.
void startInstall() {
    if (g.updBusy || !g.updOffered || !enabled(Part::UpdNow)) return;
    const Release& r = g.upd.latest;
    if (g.updFailed) {
        Log::event("update.choice", true, -1, "page " + u8(r.version));
        openUrl(r.page);
        return;
    }
    stopCheck();
    g.updBusy = true;
    g.updProg.stage = UpdateProgress::Download;
    g.updProg.got = 0;
    g.updExe = selfPath();
    if (!g.updDone) g.updDone = CreateEventW(nullptr, TRUE, TRUE, nullptr);
    if (g.updDone) ResetEvent(g.updDone);
    SetWindowTextW(g.hwnd, L"更新中 - Rec Cue!");
    SetTimer(g.hwnd, kTimerUpdate, 100, nullptr);   // the progress line
    g.focus = Part::Back;
    Log::event("update.choice", true, -1, "now " + u8(r.version) + " into " + u8(g.updExe));
    sayUpdate(kUpdGetting, UiModel::Tone::Normal);
    g.updater = std::thread([r] {
        g.updResult = installRelease(r, g.updExe, &g.updProg);
        if (g.updDone) SetEvent(g.updDone);
        PostMessageW(g.hwnd, WM_APP_INSTALLED, 0, 0);
    });
}

void registerHotkey();
void unregisterHotkeys();

// %USERPROFILE%\.rec-cue\updated-from.txt: the version 今すぐ更新 replaced, for the new exe to mention once.
std::wstring updatedNotePath() { return appDir() + L"\\updated-from.txt"; }

void writeUpdatedNote(const std::wstring& from) {
    HANDLE f = CreateFileW(updatedNotePath().c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    const std::string s = u8(from);
    DWORD w = 0;
    WriteFile(f, s.data(), static_cast<DWORD>(s.size()), &w, nullptr);
    CloseHandle(f);
}

// The note, if 今すぐ更新 left one in the last 2 minutes (an older one is only removed). Read once.
std::wstring takeUpdatedNote() {
    const std::wstring path = updatedNotePath();
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return {};
    char b[64]{};
    DWORD n = 0;
    FILETIME written{}, now{};
    const bool read = ReadFile(f, b, sizeof(b) - 1, &n, nullptr) && GetFileTime(f, nullptr, nullptr, &written);
    CloseHandle(f);
    DeleteFileW(path.c_str());
    GetSystemTimeAsFileTime(&now);
    const auto t = [](const FILETIME& x) { return (static_cast<uint64_t>(x.dwHighDateTime) << 32) | x.dwLowDateTime; };
    if (!read || t(now) < t(written) || t(now) - t(written) > 120ull * 10000000ull) return {};
    return wide(std::string(b, n));
}

// Swapped in: the new exe opens in place of this window and says so. Otherwise the exe is as it was, and the reason
// stays in the view.
void onUpdateInstalled() {
    if (g.updater.joinable()) g.updater.join();
    KillTimer(g.hwnd, kTimerUpdate);
    g.updBusy = false;
    SetWindowTextW(g.hwnd, kTitle);
    const UpdateResult& u = g.updResult;
    Log::event("update.install", u.ok, u.downloadMs + u.verifyMs + u.smokeMs + u.swapMs, updateResultJson(u));
    if (!u.ok) {   // back to the update view (it may have been left meanwhile) for the reason and ページを開く
        g.updFailed = true;
        showView(View::Update);
        g.focus = Part::UpdNow;
        sayUpdate(u.step == "folder" ? kUpdNoFolder : kUpdFailed, UiModel::Tone::Bad);
        return;
    }
    // The new exe opens as from the Start menu (no arguments, which any version takes): this one first gives up its
    // shortcuts and the one-window mutex, and leaves a note that the new one reads to say it was updated.
    sayUpdate(kUpdRestart, UiModel::Tone::Good);
    RedrawWindow(g.hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
    unregisterHotkeys();
    if (g.mutex) CloseHandle(g.mutex);
    g.mutex = nullptr;
    writeUpdatedNote(g.version);
    const bool reopened = relaunch(g.updExe);
    Log::event("update.relaunch", reopened, -1, u8(g.updExe), reopened ? S_OK : HRESULT_FROM_WIN32(GetLastError()));
    if (reopened) {
        DestroyWindow(g.hwnd);
        return;
    }
    // the new exe is in place (the next start opens it); this window goes on as it was
    DeleteFileW(updatedNotePath().c_str());
    g.mutex = CreateMutexW(nullptr, TRUE, L"Local\\RecCue.Window");
    registerHotkey();
    g.updOffered = false;
    showView(View::Main);
    passText(kUpdNoReopen, 10000, UiModel::Tone::Warn);
}

// あとで: asked again when the window opens next time; the dot on the gear stays meanwhile.
void updateLater() {
    Log::event("update.choice", true, -1, "later " + u8(g.upd.latest.version));
    showView(View::Main);
    passText(kUpdLater, 5000, UiModel::Tone::Normal);
}

// この版をスキップ: this version is not offered again (update_skip); a later one is.
void updateSkip() {
    const std::wstring v = g.upd.latest.version;
    const bool ok = saveConfigValue(L"update_skip", v);
    g.cfg.updateSkip = v;
    g.updOffered = false;
    Log::event("update.choice", ok, -1, "skip " + u8(v));
    showView(View::Main);
    passText(withVersion(kUpdSkip, v), 5000, UiModel::Tone::Normal);
}

// Like the screenshot's PNG: a worker that does not end in time must not keep a closed Rec Cue! alive.
bool waitWorker(std::thread& t, HANDLE done, DWORD ms, const char* what) {
    if (!t.joinable()) return true;
    if (done && WaitForSingleObject(done, ms) != WAIT_OBJECT_0) {
        Log::event("update.wait", false, ms, std::string(what) + " is still running; not waiting any longer");
        return false;
    }
    t.join();
    return true;
}

// A save folder in Explorer (made first when it is not there yet).
void openDir(bool shot) {
    if (!g.recording && !g.starting && !g.stopping) g.cfg = loadConfig();
    const std::wstring d = shot ? g.cfg.resolvedShotDir() : g.cfg.resolvedOutDir();
    ensureDir(d);
    const auto r = reinterpret_cast<INT_PTR>(ShellExecuteW(g.hwnd, L"open", d.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    Log::event("ui.open_dir", r > 32, -1, std::string(shot ? "shot " : "out ") + u8(d));
}

// Explorer's own folder picker, opened at `start`. Empty when it was cancelled (hr: ERROR_CANCELLED) or failed.
std::wstring pickFolder(const wchar_t* title, const std::wstring& start, HRESULT* hr) {
    Com<IFileOpenDialog> d;
    *hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IFileOpenDialog), d.put_void());
    if (FAILED(*hr)) return {};
    DWORD o = 0;
    d->GetOptions(&o);
    d->SetOptions(o | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR);
    d->SetTitle(title);
    Com<IShellItem> at;
    if (SUCCEEDED(SHCreateItemFromParsingName(start.c_str(), nullptr, __uuidof(IShellItem), at.put_void())))
        d->SetFolder(at.get());
    *hr = d->Show(g.hwnd);
    if (FAILED(*hr)) return {};
    Com<IShellItem> r;
    PWSTR p = nullptr;
    *hr = d->GetResult(r.put());
    if (SUCCEEDED(*hr)) *hr = r->GetDisplayName(SIGDN_FILESYSPATH, &p);
    const std::wstring s = SUCCEEDED(*hr) && p ? p : L"";
    CoTaskMemFree(p);
    return s;
}

// 0 when the folder takes a new file (an empty one, gone as it closes); otherwise why not. A read-only or unreachable
// place is not kept, so the next recording does not fail to start there.
DWORD writeError(const std::wstring& dir) {
    const std::wstring f = dir + (!dir.empty() && dir.back() == L'\\' ? L"" : L"\\") +
                           wide(fmt(".rec-cue-write-test-%lu-%llx.tmp", GetCurrentProcessId(),
                                    static_cast<unsigned long long>(qpcNow())));
    HANDLE h = CreateFileW(f.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                           FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (h == INVALID_HANDLE_VALUE) return GetLastError();
    CloseHandle(h);
    return 0;
}

bool samePath(std::wstring a, std::wstring b) {
    for (std::wstring* s : {&a, &b})
        while (s->size() > 3 && (s->back() == L'\\' || s->back() == L'/')) s->pop_back();
    return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_EQUAL;
}

// 変更: Explorer's folder picker at the folder in use. The folder must take a file before it is kept. The default
// place is kept as an empty value (out_dir: Videos\画面録画, shot_dir: with the recordings), so it goes on following
// that place. Used from the next recording / screenshot on.
void chooseDir(bool shot) {
    const Part part = shot ? Part::ShotChange : Part::OutChange;
    if (!enabled(part)) return;
    g.cfg = loadConfig();
    const std::wstring cur = shot ? g.cfg.resolvedShotDir() : g.cfg.resolvedOutDir();
    ensureDir(cur);   // so that the picker opens in it
    g.pressed = Part::None;
    invalidate();
    const char* kind = shot ? "shot" : "out";
    HRESULT hr = S_OK;
    Stopwatch sw;
    const std::wstring dir = pickFolder(shot ? L"スクショの保存先を選ぶ" : L"録画と録音の保存先を選ぶ", cur, &hr);
    const double ms = sw.ms();
    if (!IsWindow(g.hwnd)) return;
    if (dir.empty()) {
        const bool cancelled = hr == HRESULT_FROM_WIN32(ERROR_CANCELLED);
        Log::event("ui.folder", cancelled, ms, fmt("kind=%s %s", kind, cancelled ? "cancelled" : "no folder"),
                   cancelled ? S_OK : hr);
        invalidate();
        return;
    }
    if (!enabled(part)) {   // a recording started (shortcut, taskbar) while the picker was open: keep the folder
        Log::event("ui.folder", false, ms, fmt("kind=%s a recording started meanwhile; not changed", kind));
        invalidate();
        return;
    }
    if (const DWORD e = writeError(dir)) {
        Log::event("ui.folder", false, ms, fmt("kind=%s cannot write in ", kind) + u8(dir), HRESULT_FROM_WIN32(e));
        passText(L"このフォルダーには保存できません。ほかの場所をお選びください。", 6000, UiModel::Tone::Bad);
        return;
    }
    const std::wstring base = shot ? g.cfg.resolvedOutDir() : Config().resolvedOutDir();
    const std::wstring value = samePath(dir, base) ? L"" : dir;
    const bool ok = saveConfigValue(shot ? L"shot_dir" : L"out_dir", value);
    g.cfg = loadConfig();
    if (!g.recording && !g.starting && !g.stopping) refreshPath();   // the main view's 保存先 too
    Log::event("ui.folder", ok, ms, fmt("kind=%s default=%s ", kind, value.empty() ? "true" : "false") + u8(dir));
    if (!ok) passText(L"保存先を書き込めませんでした。", 6000, UiModel::Tone::Bad);
    else passText(shot ? L"スクショの保存先を変えました。" : L"保存先を変えました。次の録画から使います。", 4000,
                  UiModel::Tone::Good);
    invalidate();
}

// ログを取得: the logs and records of the last week in one ZIP in Downloads, to attach as it is. Written on a worker
// (Downloads may be a redirected share that answers slowly); WM_APP_LOGS then shows it selected in Explorer.
void exportLogs() {
    if (g.packing) return;
    if (g.packer.joinable()) g.packer.join();
    g.packing = true;
    const std::wstring dir = downloadsDir();
    ensureDir(dir);
    g.packPath = uniquePath(dir, L"Rec Cue ログ " + stamp(), L".zip");
    if (!g.packDone) g.packDone = CreateEventW(nullptr, TRUE, TRUE, nullptr);
    if (g.packDone) ResetEvent(g.packDone);
    Log::event("logs.export", true, -1, "start " + u8(g.packPath));
    g.packer = std::thread([about = aboutJson()] {
        packLogs(g.packPath, kLogDays, about, &g.packed);
        if (g.packDone) SetEvent(g.packDone);
        PostMessageW(g.hwnd, WM_APP_LOGS, 0, 0);
    });
    invalidate();
}

// The ZIP is there: Explorer opens Downloads with it selected (to drag into a mail, or copy).
void onLogsPacked() {
    if (g.packer.joinable()) g.packer.join();   // also makes the worker's results visible here
    g.packing = false;
    const bool ok = g.packed.error.empty();
    Log::event("logs.export", ok, g.packed.ms, packJson(ok, g.packPath, g.packed));
    if (ok) {
        HRESULT hr = E_FAIL;
        if (PIDLIST_ABSOLUTE pidl = ILCreateFromPathW(g.packPath.c_str())) {
            hr = SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
            ILFree(pidl);
        }
        Log::event("logs.show", SUCCEEDED(hr), -1, "", hr);
        passText(L"ログを ZIP にまとめ、ダウンロードに置きました。", 5000, UiModel::Tone::Good);
    } else {
        passText(L"ログをまとめられませんでした。", 8000, UiModel::Tone::Bad);
    }
    invalidate();
}

// Like the screenshot's PNG: a ZIP that cannot be finished must not keep a closed Rec Cue! alive. false: still
// writing after `ms`.
bool waitPacker(DWORD ms) {
    if (!g.packer.joinable()) return true;
    if (g.packDone && WaitForSingleObject(g.packDone, ms) != WAIT_OBJECT_0) {
        Log::event("logs.wait", false, ms, "the log ZIP is still being written; not waiting any longer");
        return false;
    }
    g.packer.join();
    return true;
}

// A click on her or her bubble: something else that fits the moment (while idle a tip or a word of hers).
void talk() {
    if (g.starting || g.stopping) return;
    g.passingUntil = 0;
    SayContext c = sayContext();
    if (g.recording && g.rec.paused()) {
        say(Scene::Pause, c);
    } else if (g.recording && g.warnScene != Scene::Count) {
        say(g.warnScene, c);
    } else if (g.recording) {
        c.sec = g.rec.elapsedSec();
        c.n = static_cast<int>(c.sec / 60);
        say(g.recAudio ? Scene::AudioOn : Scene::RecOn, c);
    } else if (g.checking) {
        say(Scene::Check, c);
    } else {
        say(g.lines.roll(10) < 4 ? Scene::Tip : Scene::Talk, c);
    }
}

void activate(Part p) {
    if (!enabled(p)) return;
    switch (p) {
        case Part::Record: toggleRecord(); break;
        case Part::Audio: toggleAudio(); break;
        case Part::Gear: showView(g.view == View::Settings ? View::Main : View::Settings); break;
        case Part::Back:
            if (g.view == View::Update) updateLater();
            else showView(View::Main);
            break;
        case Part::About: openUpdate(); break;
        case Part::UpdNow: startInstall(); break;
        case Part::UpdLater: updateLater(); break;
        case Part::UpdSkip: updateSkip(); break;
        case Part::UpdNotes:
            Log::event("update.choice", true, -1, "notes " + u8(g.upd.latest.version));
            openUrl(g.upd.latest.page);
            break;
        case Part::OutDir: openDir(false); break;
        case Part::ShotDir: openDir(true); break;
        case Part::OutChange: chooseDir(false); break;
        case Part::ShotChange: chooseDir(true); break;
        case Part::Logs: exportLogs(); break;
        case Part::Github: openUrl(g.cfg.githubUrl); break;
        case Part::Site: openUrl(g.cfg.siteUrl); break;
        case Part::Mic: chooseDevice(false); break;
        case Part::Sys: chooseDevice(true); break;
        case Part::Screen: chooseScreen(); break;
        case Part::AudioFormat: chooseAudioFormat(); break;
        case Part::Check:
            if (g.checking) {
                stopCheck();
                setStatus(L"音量の確認を止めました");
                say(Scene::CheckEnd);
            } else {
                startCheck();
            }
            break;
        case Part::Open: {
            std::wstring d = g.cfg.resolvedOutDir();
            ensureDir(d);
            ShellExecuteW(g.hwnd, L"open", d.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            break;
        }
        case Part::Pin: setTopmost(!g.topmost, true); break;
        case Part::Shot: takeShot("button"); break;
        case Part::Guide: talk(); break;
        case Part::Pause:
        case Part::AudioPause: togglePause(); break;
        case Part::None: break;
    }
}

void moveFocus(int dir) {
    static const Part mainOrder[] = {Part::Record, Part::Pause, Part::AudioPause, Part::Audio, Part::AudioFormat,
                                     Part::Pin,    Part::Shot,  Part::Gear,       Part::Screen, Part::Mic,
                                     Part::Sys,    Part::Check, Part::Open};
    static const Part setOrder[] = {Part::Back,    Part::Pin,        Part::Shot, Part::Gear,   Part::OutDir, Part::OutChange,
                                    Part::ShotDir, Part::ShotChange, Part::Logs, Part::Github, Part::Site,     Part::About};
    static const Part updOrder[] = {Part::Back,     Part::Pin,    Part::Shot,     Part::Gear,
                                    Part::UpdNotes, Part::UpdNow, Part::UpdLater, Part::UpdSkip};
    const Part* order = g.view == View::Settings ? setOrder : g.view == View::Update ? updOrder : mainOrder;
    const int n = g.view == View::Settings ? 12 : g.view == View::Update ? 8 : 13;
    int i = 0;
    while (i < n && order[i] != g.focus) ++i;
    for (int step = 0; step < n; ++step) {
        i = ((i + dir) % n + n) % n;
        if (enabled(order[i])) {
            g.focus = order[i];
            break;
        }
    }
    g.showFocus = true;
    invalidate();
}

Part enabledHit(LPARAM lp) {
    const Part p = hitTest(dip(static_cast<short>(LOWORD(lp))), dip(static_cast<short>(HIWORD(lp))), g.view);
    return enabled(p) ? p : Part::None;
}

void unregisterHotkeys() {
    if (g.hotkeyOn) UnregisterHotKey(g.hwnd, kHotkey);
    if (g.shotHotkeyOn) UnregisterHotKey(g.hwnd, kHotkeyShot);
    if (g.audioHotkeyOn) UnregisterHotKey(g.hwnd, kHotkeyAudio);
    g.hotkeyOn = g.shotHotkeyOn = g.audioHotkeyOn = false;
}

void registerHotkey() {
    UINT mods = 0, vk = 0;
    unregisterHotkeys();
    if (parseHotkey(g.cfg.audioHotkey, &mods, &vk)) {
        g.audioHotkeyOn = RegisterHotKey(g.hwnd, kHotkeyAudio, mods | MOD_NOREPEAT, vk) != 0;
        Log::event("ui.hotkey", g.audioHotkeyOn, -1,
                   "audio only " + u8(g.cfg.audioHotkey) + (g.audioHotkeyOn ? "" : " (used by another app)"));
    }
    if (parseHotkey(g.cfg.hotkey, &mods, &vk)) {
        g.hotkeyOn = RegisterHotKey(g.hwnd, kHotkey, mods | MOD_NOREPEAT, vk) != 0;
        Log::event("ui.hotkey", g.hotkeyOn, -1, "record " + u8(g.cfg.hotkey) + (g.hotkeyOn ? "" : " (used by another app)"));
    }
    if (parseHotkey(g.cfg.shotHotkey, &mods, &vk)) {
        g.shotHotkeyOn = RegisterHotKey(g.hwnd, kHotkeyShot, mods | MOD_NOREPEAT, vk) != 0;
        Log::event("ui.hotkey", g.shotHotkeyOn, -1,
                   "screenshot " + u8(g.cfg.shotHotkey) + (g.shotHotkeyOn ? "" : " (used by another app)"));
    }
}

// Alt+Space / right click on the title bar: the same two toggles as the buttons, for keyboard users.
void addSystemMenu() {
    HMENU m = GetSystemMenu(g.hwnd, FALSE);
    if (!m) return;
    InsertMenuW(m, SC_CLOSE, MF_BYCOMMAND | MF_STRING, kSysTopmost, L"最前面に表示(&T)");
    const std::wstring shot = L"スクショを撮る(&S)" + (g.shotHotkeyOn ? L"\t" + g.cfg.shotHotkey : std::wstring());
    InsertMenuW(m, SC_CLOSE, MF_BYCOMMAND | MF_STRING, kSysShot, shot.c_str());
    InsertMenuW(m, SC_CLOSE, MF_BYCOMMAND | MF_SEPARATOR, 0, nullptr);
}

// ---- tooltips: the tools that are only an icon, and the guide's name
constexpr UINT_PTR kTipGuide = 100;
const Part kTipParts[] = {Part::Record,    Part::Audio,   Part::Pin,        Part::Shot,       Part::Gear,
                          Part::Pause,     Part::AudioPause, Part::AudioFormat, Part::Back,    Part::OutDir,
                          Part::OutChange, Part::ShotDir, Part::ShotChange, Part::Logs,       Part::Github,
                          Part::Site,      Part::About,   Part::UpdNotes,   Part::UpdNow,     Part::UpdLater,
                          Part::UpdSkip};

// A tool of the other view has no room (moveTooltips when the view changes), so their rectangles never compete.
RECT tipRect(UINT_PTR id) {
    if (id != kTipGuide && !inView(static_cast<Part>(id), g.view)) return {0, 0, 0, 0};
    const D2D1_RECT_F f = id == kTipGuide ? guideRect() : partRect(static_cast<Part>(id));
    return {S(static_cast<int>(f.left)), S(static_cast<int>(f.top)), S(static_cast<int>(f.right)), S(static_cast<int>(f.bottom))};
}

std::wstring tipText(UINT_PTR id) {
    if (id == kTipGuide) return L"湊 しおり (案内役) — クリックすると、ほかのひとことを話します";
    const std::wstring rec = keyText(g.cfg.hotkey, g.hotkeyOn), aud = keyText(g.cfg.audioHotkey, g.audioHotkeyOn),
                       shot = keyText(g.cfg.shotHotkey, g.shotHotkeyOn);
    switch (static_cast<Part>(id)) {
        case Part::Record:
            return g.recording && !g.recAudio ? L"録画を止めて保存 (" + rec + L")" : L"録画: 画面とマイクと PC の音声 (" + rec + L")";
        case Part::Audio:
            return g.recording && g.recAudio ? L"録音を止めて保存 (" + aud + L")" : L"録音だけ: マイクと PC の音声 (" + aud + L")";
        case Part::Pin: return g.topmost ? L"最前面の固定をやめる" : L"最前面に固定する";
        case Part::Shot: return L"スクショ (" + shot + L") — Rec Cue! の窓は写りません";
        case Part::Gear:
            if (g.view == View::Settings) return L"設定を閉じる";
            return g.updOffered ? L"設定 — 新しい版 " + g.upd.latest.version + L" があります"
                                : L"設定 — 保存先・ショートカットキー・ログ・リンク";
        case Part::Back: return g.view == View::Update ? L"あとで (Esc)" : L"戻る (Esc)";
        case Part::About: return g.updOffered ? L"新しい版 " + g.upd.latest.version + L" の内容と更新" : L"";
        case Part::UpdNotes: return g.upd.latest.page;
        case Part::UpdNow:
            if (g.updFailed) return L"GitHub の Releases のページを開きます (rec-cue.exe を置き換えてください)";
            if (!enabled(Part::UpdNow)) return g.updBusy ? L"" : L"録画中・録音中は更新できません";
            return L"rec-cue.exe を受け取り、大きさと SHA-256 を確かめてから入れ替えて開き直します (今の版は "
                   L"rec-cue.prev.exe に残ります)";
        case Part::UpdLater: return L"次に開いたときに、またお知らせします";
        case Part::UpdSkip: return L"この版はお知らせしません (次の版が出たらお知らせします)";
        case Part::OutDir:
        case Part::ShotDir: return L"エクスプローラーで開く";
        case Part::OutChange:
        case Part::ShotChange:
            if (!enabled(static_cast<Part>(id))) return L"録画中・録音中は変えられません";
            return static_cast<Part>(id) == Part::OutChange
                       ? L"録画と録音の保存先を、エクスプローラーの画面で選ぶ"
                       : L"スクショの保存先を選ぶ — 録画と同じフォルダーを選ぶと「録画と同じ場所」に戻ります";
        case Part::Logs:
            return g.packing ? L"ログをまとめています" : L"ログを取得 — 直近 7 日のログと録画ごとの記録を ZIP にまとめ、ダウンロードに置きます";
        case Part::Github: return g.cfg.githubUrl;
        case Part::Site: return g.cfg.siteUrl;
        case Part::AudioFormat:   // only while nothing records (it is not shown otherwise)
            if (!enabled(Part::AudioFormat)) return L"";
            return std::wstring(L"録音の形式: ") + audioFormatLabel(audioFileOf(g.cfg.audioFormat)) +
                   L" — クリックで MP3・WAV・M4A から選べます (録画はいつも MP4)";
        case Part::Pause:
        case Part::AudioPause: {   // no tip where the button is not shown (an empty text shows nothing)
            if (!enabled(static_cast<Part>(id))) return L"";
            const wchar_t* what = g.recAudio ? L"録音" : L"録画";
            return g.rec.paused() ? std::wstring(what) + L"を再開 (止めていた間は入りません)"
                                  : std::wstring(what) + L"を一時停止 (■ で停止して保存)";
        }
        default: return L"";
    }
}

void addTooltips() {
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_WIN95_CLASSES};
    InitCommonControlsEx(&icc);
    g.tip = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, g.hwnd, nullptr, g.inst, nullptr);
    if (!g.tip) return;
    SetWindowDisplayAffinity(g.tip, WDA_EXCLUDEFROMCAPTURE);   // never in a screenshot or a recording
    if (HMODULE ux = GetModuleHandleW(L"uxtheme.dll"))
        if (auto setTheme = reinterpret_cast<HRESULT(WINAPI*)(HWND, LPCWSTR, LPCWSTR)>(GetProcAddress(ux, "SetWindowTheme")))
            setTheme(g.tip, L"DarkMode_Explorer", nullptr);
    SendMessageW(g.tip, TTM_SETMAXTIPWIDTH, 0, S(320));
    auto add = [](UINT_PTR id) {
        TOOLINFOW ti{};
        ti.cbSize = sizeof(ti);
        ti.uFlags = TTF_SUBCLASS;
        ti.hwnd = g.hwnd;
        ti.uId = id;
        ti.rect = tipRect(id);
        ti.lpszText = LPSTR_TEXTCALLBACKW;   // tipText() when it shows (WM_NOTIFY)
        SendMessageW(g.tip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti));
    };
    for (Part p : kTipParts) add(static_cast<UINT_PTR>(p));
    add(kTipGuide);
}

void moveTooltips() {   // after a DPI change
    if (!g.tip) return;
    auto move = [](UINT_PTR id) {
        TOOLINFOW ti{};
        ti.cbSize = sizeof(ti);
        ti.hwnd = g.hwnd;
        ti.uId = id;
        ti.rect = tipRect(id);
        SendMessageW(g.tip, TTM_NEWTOOLRECTW, 0, reinterpret_cast<LPARAM>(&ti));
    };
    for (Part p : kTipParts) move(static_cast<UINT_PTR>(p));
    move(kTipGuide);
    SendMessageW(g.tip, TTM_SETMAXTIPWIDTH, 0, S(320));
}

// --ui-snapshot <file.png>: renders the window in several states to <file>-idle.png, -check.png, ...
// No window and no audio device is opened (the levels are made up), so it shows nothing of the screen.
// dpi: 0 = the system's (192 = the README's 2x pictures). at: "2026-10-22T14:00" picks the lines of that time.
int runSnapshot(const std::wstring& path, UINT dpiArg, const std::wstring& at) {
    g.cfg = loadConfig();
    g.cfg.mic = g.cfg.system = L"auto";   // the menus' labels for other settings would name this PC's devices
    g.cfg.audioFormat = L"mp3";
    std::vector<std::string> problems;
    g.lines.load(GetModuleHandleW(nullptr), &problems);
    for (auto& q : problems) Log::event("lines", false, -1, q);
    g.lines.seed(20261023);   // the same lines every time (for this time of day)
    SYSTEMTIME st{};
    if (!at.empty()) {
        if (swscanf(at.c_str(), L"%hu-%hu-%huT%hu:%hu", &st.wYear, &st.wMonth, &st.wDay, &st.wHour, &st.wMinute) != 5) {
            Log::event("ui.snapshot", false, -1, "--at wants 2026-10-22T14:00");
            return 2;
        }
        FILETIME ft;   // fills in the day of the week
        if (SystemTimeToFileTime(&st, &ft)) FileTimeToSystemTime(&ft, &st);
        g.lines.setClock(st);
    }
    fillItems(false);
    fillItems(true);
    refreshPath();
    // sample monitors too (the main screen and a larger one to its right), so the map shows nothing of this PC
    MonitorInfo mainScreen, rightScreen;
    mainScreen.rect = {0, 0, 1920, 1080};
    mainScreen.primary = mainScreen.internal = true;
    rightScreen.index = 1;
    rightScreen.rect = {1920, -180, 4480, 1260};
    g.monitors = {mainScreen, rightScreen};
    g.cfg.monitor = 0;
    std::wstring base = path;
    if (base.size() > 4 && lower(base.substr(base.size() - 4)) == L".png") base.resize(base.size() - 4);
    UiPainter p;
    if (!p.init()) {
        Log::event("ui.snapshot", false, -1, "Direct2D / DirectWrite unavailable");
        return 1;
    }
    const UINT dpi = dpiArg >= 96 && dpiArg <= 480 ? dpiArg : GetDpiForSystem();
    // sample names, so the pictures carry nothing of this PC (device names, the user's folder)
    const std::wstring micDev = L"ヘッドセット マイク", sysDev = L"スピーカー", dir = L"C:\\Users\\you\\Videos\\画面録画";
    g.path = dir;
    int fails = 0;
    auto shot = [&](const wchar_t* name, const UiModel& m) {
        const HRESULT hr = p.snapshot(base + L"-" + name + L".png", dpi, m);
        Log::event("ui.snapshot", SUCCEEDED(hr), -1, u8(name) + fmt(" dpi=%u", dpi), hr);
        if (FAILED(hr)) ++fails;
    };
    auto live = [&](UiModel m, double micDb, double sysDb) {
        m.mic.device = micDev;
        m.sys.device = sysDev;
        m.mic.active = m.sys.active = true;
        m.mic.db = micDb;
        m.mic.hold = micDb + 6;
        m.sys.db = sysDb;
        m.sys.hold = sysDb + 7;
        return m;
    };
    using P = UiModel::Phase;
    g.hotkeyOn = g.shotHotkeyOn = g.audioHotkeyOn = true;   // as if the shortcuts were registered
    g.status = idleStatus();
    auto line = [&](UiModel& m, Scene s, SayContext c) {
        m.comment = g.lines.pick(s, c);
        m.commentTone = toneOf(s);
    };
    const SayContext c0 = sayContext();
    g.line = g.lines.pick(Scene::Start, c0);
    const UiModel idle = model();
    shot(L"idle", idle);
    UiModel hover = idle;
    hover.hot = Part::Record;
    hover.focus = Part::Sys;
    hover.showFocus = true;
    shot(L"hover", hover);
    UiModel check = live(idle, -16, -27);
    check.phase = P::Checking;
    check.status = L"音量を確認中です (60 秒で自動で止まります)";
    line(check, Scene::CheckMic, c0);
    shot(L"check", check);
    const std::wstring name = g.cfg.namePrefix + L" 2026-10-22 193001.mp4";
    UiModel rec = live(idle, -12, -23);
    rec.phase = P::Recording;
    rec.timer = L"00:12:34";
    rec.status = L"録画中 — ■ を押すと停止して保存します";
    rec.pathLabel = L"録画中";
    rec.path = dir + L"\\" + name;
    rec.split = 1;
    rec.screen.active = true;
    line(rec, Scene::Rec, c0);
    shot(L"rec", rec);
    UiModel shotm = rec;   // pinned, just after the third screenshot
    shotm.topmost = true;
    shotm.shots = 3;
    shotm.shotFlash = true;
    shotm.status = L"スクショ 3 枚目を保存し、コピーしました";
    shotm.tone = UiModel::Tone::Good;
    SayContext c3 = c0;
    c3.n = 3;
    line(shotm, Scene::Shot, c3);
    shot(L"shot", shotm);
    UiModel warn = rec;
    warn.sys.db = warn.sys.hold = -90;
    warn.sys.warn = true;
    warn.alert = true;
    warn.timer = L"00:14:02";
    warn.status = L"PC の音声が 1 分以上無音です。出力先を確認";
    warn.tone = UiModel::Tone::Warn;
    warn.pulse = 0.2f;
    line(warn, Scene::WarnSys, c0);
    shot(L"warn", warn);
    UiModel audio = live(idle, -18, -30);
    audio.phase = P::Recording;
    audio.audioOnly = true;
    audio.split = -1;
    SayContext ca = c0;
    ca.audio = true;
    line(audio, Scene::Audio, ca);
    audio.timer = L"00:03:21";
    audio.status = L"録音中 — ■ を押すと停止して保存します";
    audio.pathLabel = L"録音中";
    audio.path = dir + L"\\" + g.cfg.namePrefix + L" 2026-10-22 200105.mp3";
    shot(L"audio", audio);
    UiModel opening = audio;   // the audio side half way open (the animation after audio recording starts)
    opening.split = -0.5f;
    opening.timer = L"00:00:00";
    shot(L"opening", opening);
    UiModel saved = idle;
    saved.timer = L"00:12:34";
    saved.status = L"保存しました (196.3 MB)";
    SayContext cs = c0;
    cs.sec = 754;
    cs.mb = 196.3;
    line(saved, Scene::Saved, cs);
    saved.tone = UiModel::Tone::Good;
    saved.path = rec.path;
    shot(L"saved", saved);
    UiModel talk = idle;   // the pointer on her: she gives a tip when clicked
    talk.hot = Part::Guide;
    line(talk, Scene::Tip, c0);
    shot(L"talk", talk);
    UiModel paused = rec;   // paused: ▶ beside ■, the PAUSE chip and band in amber, the time stands still
    paused.paused = true;
    paused.pulse = 0.6f;
    paused.status = L"一時停止中 — ▶ で再開、■ で停止して保存します";
    line(paused, Scene::Pause, c0);
    shot(L"paused", paused);
    UiModel apaused = audio;
    apaused.paused = true;
    apaused.pulse = 0.6f;
    apaused.status = paused.status;
    line(apaused, Scene::Pause, ca);
    shot(L"audio-paused", apaused);
    UiModel set = idle;   // the settings view (the gear): the save folders, the shortcuts, the log and the links
    set.view = View::Settings;
    set.outDir = dir;
    set.keys[0] = {L"Ctrl+Alt+R", false};
    set.keys[1] = {L"Ctrl+Alt+A", false};
    set.keys[2] = {L"Ctrl+Alt+S", false};
    shot(L"settings", set);
    UiModel setRec = set;   // ... while recording: 変更 waits, the chip and the time beside 設定; the pointer on a folder
    setRec.phase = P::Recording;
    setRec.timer = rec.timer;
    setRec.shotDir = L"C:\\Users\\you\\Pictures\\スクショ";
    setRec.hot = Part::ShotDir;
    setRec.keys[2].taken = true;   // as if another app had the screenshot key
    setRec.comment = rec.comment;
    setRec.commentTone = rec.commentTone;
    shot(L"settings-rec", setRec);
    // the update view as it opens when a newer release is found, while 今すぐ更新 downloads, after a failure, and the
    // settings with that release on the version row and a dot on the gear (a sample release, not GitHub's)
    UiModel upd = idle;
    upd.view = View::Update;
    upd.newVersion = upd.update.version = L"0.6.0";
    upd.update.current = wide(kVersion);
    upd.update.published = L"2026-11-14";
    upd.update.size = 1317376;
    upd.update.notes = {L"アップデートの通知\t窓を開いたときに新しい版を確かめ、今すぐ更新・あとで・この版をスキップから選べます",
                        L"今すぐ更新\trec-cue.exe を受け取り、大きさと SHA-256 を確かめてから入れ替えて開き直します",
                        L"この版をスキップ\tその版はお知らせしません。次の版が出たらお知らせします",
                        L"設定の画面\t新しい版があるときは、版の欄から開けます",
                        L"\tしおりの台詞を新しい画面に合わせました",
                        L"exe のプロパティ\t版が正しく出るようにしました",
                        L"\t細かな直し"};
    upd.focus = Part::UpdNow;
    upd.comment = withVersion(kUpdOffer, L"0.6.0");
    upd.commentTone = UiModel::Tone::Normal;
    shot(L"update", upd);
    UiModel updBusy = upd;
    updBusy.update.state = UiModel::Update::State::Download;
    updBusy.update.got = upd.update.size * 2 / 5;
    updBusy.focus = Part::Back;
    updBusy.comment = kUpdGetting;
    shot(L"update-busy", updBusy);
    UiModel updFail = upd;
    updFail.update.state = UiModel::Update::State::Failed;
    updFail.update.error = L"受け取れませんでした";
    updFail.comment = kUpdFailed;
    updFail.commentTone = UiModel::Tone::Bad;
    shot(L"update-failed", updFail);
    UiModel setUpd = set;
    setUpd.newVersion = upd.newVersion;
    setUpd.hot = Part::About;
    shot(L"settings-update", setUpd);
    // the buttons under the taskbar thumbnail, 4x the 16 px they are at 100%, for the dark and the light taskbar
    const std::pair<ThumbGlyph, const wchar_t*> glyphs[] = {{ThumbGlyph::Record, L"record"}, {ThumbGlyph::Audio, L"audio"},
                                                            {ThumbGlyph::Pause, L"pause"},   {ThumbGlyph::Resume, L"resume"},
                                                            {ThumbGlyph::Stop, L"stop"}};
    for (const bool light : {false, true})
        for (const auto& gl : glyphs) {
            const std::wstring name = std::wstring(L"thumb-") + gl.second + (light ? L"-light" : L"");
            const HRESULT hr = p.thumbPng(gl.first, 64, light, base + L"-" + name + L".png");
            Log::event("ui.snapshot", SUCCEEDED(hr), -1, u8(name), hr);
            if (FAILED(hr)) ++fails;
        }
    return fails ? 1 : 0;
}

LRESULT CALLBACK wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == g.taskbarMsg && msg) {   // the taskbar button exists (again, after an Explorer restart)
        g.ind.taskbarReady(h);
        if (!g.thumbIcons) makeThumbIcons();
        g.thumb.taskbarReady(h, thumbView());
        return 0;
    }
    switch (msg) {
        case WM_COMMAND:
            if (HIWORD(wp) == THBN_CLICKED) {   // a button under the taskbar thumbnail
                onThumb(LOWORD(wp));
                return 0;
            }
            break;
        case WM_SETTINGCHANGE:   // light / dark taskbar: the thumbnail buttons are drawn again in its colours
            if (lp && lstrcmpiW(reinterpret_cast<LPCWSTR>(lp), L"ImmersiveColorSet") == 0 && g.thumbIcons) makeThumbIcons();
            break;
        case WM_CREATE:
            g.hwnd = h;
            g.dpi = GetDpiForWindow(h);
            styleFrame(h);
            setWindowIcons();
            resizeToClient();
            fillItems(false);
            fillItems(true);
            refreshMonitors();
            refreshPath();
            registerHotkey();
            addSystemMenu();
            addTooltips();
            g.status = idleStatus();
            say(Scene::Start);
            g.updatedFrom = takeUpdatedNote();
            if (!g.updatedFrom.empty()) {   // opened by 今すぐ更新 of the version before
                Log::event("update.done", true, -1, u8(g.updatedFrom) + " -> " + kVersion);
                passText(withVersion(kUpdDone, wide(kVersion)), 8000, UiModel::Tone::Good);
            }
            startUpdateCheck();
            return 0;
        case WM_NOTIFY: {
            const auto* nm = reinterpret_cast<const NMHDR*>(lp);
            if (g.tip && nm->hwndFrom == g.tip && nm->code == TTN_GETDISPINFOW) {
                auto* di = reinterpret_cast<NMTTDISPINFOW*>(lp);
                g.tipText = tipText(nm->idFrom);
                di->lpszText = const_cast<wchar_t*>(g.tipText.c_str());
                return 0;
            }
            break;
        }
        case WM_SYSCOMMAND:
            if ((wp & 0xFFF0) == kSysTopmost) {
                setTopmost(!g.topmost, true);
                return 0;
            }
            if ((wp & 0xFFF0) == kSysShot) {
                takeShot("menu");
                return 0;
            }
            break;
        case WM_SIZE:
            uiCounters().resizes++;
            g.painter.resize(LOWORD(lp), HIWORD(lp));
            return 0;
        case WM_ERASEBKGND:
            return 1;   // the painter fills everything
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(h, &ps);
            uiCounters().paints++;
            g.painter.paint(h, g.dpi, model());
            EndPaint(h, &ps);
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (!g.mouseIn) {
                TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, h, 0};
                g.mouseIn = TrackMouseEvent(&t) != 0;
            }
            const Part p = enabledHit(lp);
            if (p != g.hot) {
                g.hot = p;
                invalidate();
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            g.mouseIn = false;
            if (g.hot != Part::None) {
                g.hot = Part::None;
                invalidate();
            }
            return 0;
        case WM_LBUTTONDOWN: {
            const Part p = enabledHit(lp);
            if (p != Part::None) {
                g.pressed = p;
                g.focus = p;
                SetCapture(h);
                invalidate();
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            const Part was = g.pressed;
            g.pressed = Part::None;
            if (GetCapture() == h) ReleaseCapture();
            invalidate();
            if (was != Part::None && enabledHit(lp) == was) activate(was);
            return 0;
        }
        case WM_CAPTURECHANGED:
            if (g.pressed != Part::None) {
                g.pressed = Part::None;
                invalidate();
            }
            return 0;
        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT) {
                SetCursor(LoadCursorW(nullptr, g.hot != Part::None ? IDC_HAND : IDC_ARROW));
                return TRUE;
            }
            break;
        case WM_KEYDOWN:
            if (lp & (1 << 30)) return 0;   // auto-repeat
            if (wp == VK_TAB) {
                moveFocus(GetKeyState(VK_SHIFT) < 0 ? -1 : 1);
                return 0;
            }
            if (wp == VK_ESCAPE && g.view != View::Main) {   // あとで in the update view (not while it updates)
                activate(Part::Back);
                return 0;
            }
            if (wp == VK_SPACE || wp == VK_RETURN ||
                ((wp == VK_DOWN || wp == VK_F4) && (g.focus == Part::Mic || g.focus == Part::Sys || g.focus == Part::Screen))) {
                g.showFocus = true;
                activate(g.focus);
                return 0;
            }
            break;
        case WM_HOTKEY:
            if (wp == kHotkey && !g.stopping && !g.starting) {
                if (g.recording && g.recAudio) notice(L"録音中は録画を始められません", UiModel::Tone::Warn);
                else toggleRecord();
            }
            if (wp == kHotkeyShot) takeShot("hotkey");
            if (wp == kHotkeyAudio && !g.stopping && !g.starting) {
                if (g.recording && !g.recAudio) notice(L"録画中は録音を始められません", UiModel::Tone::Warn);
                else toggleAudio();
            }
            return 0;
        case WM_TIMER:
            if (wp == kTimer) tick();
            if (wp == kTimerNotice) noticeOver();
            if (wp == kTimerSplit) splitStep();
            if (wp == kTimerSay) {
                KillTimer(h, kTimerSay);
                invalidate();
            }
            if (wp == kTimerFlash) {
                KillTimer(h, kTimerFlash);
                invalidate();
            }
            if (wp == kTimerUpdate) invalidate();   // 今すぐ更新's progress
            if (wp == kTimerResume) {   // ▶ under the taskbar thumbnail, 0.3 s ago (still paused and not stopping)
                KillTimer(h, kTimerResume);
                if (g.recording && !g.stopping && g.rec.paused()) togglePause();
            }
            return 0;
        case WM_APP_STOPPED:
            onStopped();
            return 0;
        case WM_APP_SHOT:
            onShotSaved();
            return 0;
        case WM_APP_LOGS:
            onLogsPacked();
            return 0;
        case WM_APP_UPDATE:
            onUpdateChecked();
            return 0;
        case WM_APP_INSTALLED:
            onUpdateInstalled();
            return 0;
        case WM_DPICHANGED: {
            g.dpi = HIWORD(wp);
            const RECT* r = reinterpret_cast<const RECT*>(lp);
            SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
            resizeToClient();
            setWindowIcons();
            moveTooltips();
            g.painter.dropTarget();
            invalidate();
            return 0;
        }
        case WM_DISPLAYCHANGE:   // a monitor came or went, or a resolution changed: the screen row and its map follow
            refreshMonitors();
            invalidate();
            break;
        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE) {
                g.inactiveSince = qpcNow();
                if (g.pressed != Part::None) {
                    g.pressed = Part::None;
                    invalidate();
                }
            } else if (g.inactiveSince && qpcMs(qpcNow() - g.inactiveSince) > 20 * 60 * 1000.0 && !g.recording &&
                       !g.starting && !g.stopping && !g.checking) {
                say(Scene::Back);   // used again after 20 minutes or more
            }
            break;
        case WM_CLOSE:
            if (g.recording) {
                const wchar_t* ask = g.recAudio ? L"録音中です。停止して保存してから閉じますか?"
                                                : L"録画中です。停止して保存してから閉じますか?";
                if (MessageBoxW(h, ask, kTitle, MB_ICONQUESTION | MB_OKCANCEL) != IDOK) return 0;
                if (g.stopper.joinable()) g.stopper.join();
                if (g.rec.active()) g.rec.stop();
                g.recording = false;
            }
            stopCheck();
            DestroyWindow(h);
            return 0;
        case WM_QUERYENDSESSION:     // logoff / shutdown / update restart: finish the file first
            if (g.recording || g.stopping) ShutdownBlockReasonCreate(h, L"Rec Cue! がファイルを保存しています");
            return TRUE;
        case WM_ENDSESSION:
            if (wp) {
                if (g.stopper.joinable()) g.stopper.join();
                if (g.rec.active()) g.rec.stop();
                g.recording = false;
                g.stopping = false;
                waitShooter(3000);   // the recording comes first; a stuck screenshot must not hold up the session end
                waitPacker(3000);
                Log::event("ui.endsession", true, -1, "saved before the session ended");
                ShutdownBlockReasonDestroy(h);
            }
            return 0;
        case WM_DESTROY:
            if (g.ticking) KillTimer(h, kTimer);
            KillTimer(h, kTimerNotice);
            KillTimer(h, kTimerFlash);
            KillTimer(h, kTimerSplit);
            KillTimer(h, kTimerSay);
            KillTimer(h, kTimerResume);
            KillTimer(h, kTimerUpdate);
            g.ind.shutdown();
            g.thumb.shutdown();
            g.painter.dropTarget();
            if (g.hotkeyOn) UnregisterHotKey(h, kHotkey);
            if (g.shotHotkeyOn) UnregisterHotKey(h, kHotkeyShot);
            if (g.audioHotkeyOn) UnregisterHotKey(h, kHotkeyAudio);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

// --quit: closes the window of a running Rec Cue! unless it is recording (deploy.sh, before replacing the exe).
// Exit code 0 = closed or not running, 3 = recording (left alone), 4 = still open after 10 s.
int quitRunning(const std::wstring& jsonOut) {
    std::string result = "not_running";
    int code = 0;
    if (HWND w = FindWindowW(kClass, nullptr)) {
        wchar_t t[128]{};
        GetWindowTextW(w, t, 128);
        const std::wstring title(t);
        if (title.find(L"録画中") != std::wstring::npos || title.find(L"録音中") != std::wstring::npos ||   // also while starting / saving
            title.find(L"更新中") != std::wstring::npos) {   // ... and while 今すぐ更新 replaces the exe
            result = "recording";
            code = 3;
        } else {
            DWORD pid = 0;
            GetWindowThreadProcessId(w, &pid);
            HANDLE ph = OpenProcess(SYNCHRONIZE, FALSE, pid);
            PostMessageW(w, WM_CLOSE, 0, 0);
            const DWORD r = ph ? WaitForSingleObject(ph, 10000) : WAIT_FAILED;
            if (ph) CloseHandle(ph);
            result = r == WAIT_OBJECT_0 ? "closed" : "timeout";
            code = r == WAIT_OBJECT_0 ? 0 : 4;
        }
    }
    Log::event("quit", code == 0, -1, result);
    writeText(jsonOut, std::string("{\"ok\":") + (code == 0 ? "true" : "false") + ",\"result\":" + json(result) + "}");
    return code;
}

int runUi(HINSTANCE inst, int show, const Args& a) {
    if (!a.pretend.empty()) g.version = a.pretend;
    g.mutex = CreateMutexW(nullptr, TRUE, L"Local\\RecCue.Window");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND other = FindWindowW(kClass, nullptr);
        if (other) {
            ShowWindow(other, SW_RESTORE);
            SetForegroundWindow(other);
        }
        if (g.mutex) CloseHandle(g.mutex);
        return 0;
    }
    std::vector<std::string> warn;
    g.cfg = loadConfig(&warn);
    for (auto& w : warn) Log::event("config", false, -1, w);
    g.inst = inst;
    std::vector<std::string> problems;
    Stopwatch ls;
    const bool loaded = g.lines.load(inst, &problems);
    Log::event("lines.load", loaded && problems.empty(), ls.ms(), fmt("lines=%zu problems=%zu", g.lines.size(), problems.size()));
    for (auto& q : problems) Log::event("lines", false, -1, q);
    if (!g.painter.init()) {
        Log::event("ui.init", false, -1, "Direct2D / DirectWrite unavailable");
        MessageBoxW(nullptr, L"画面の描画 (Direct2D) を初期化できませんでした。", kTitle, MB_ICONERROR | MB_OK);
        if (g.mutex) CloseHandle(g.mutex);
        return 1;
    }
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hIconSm = static_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                               GetSystemMetrics(SM_CYSMICON), 0));
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);
    g.taskbarMsg = RegisterWindowMessageW(L"TaskbarButtonCreated");
    HWND h = CreateWindowExW(0, kClass, kTitle, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT,
                             CW_USEDEFAULT, 500, 320, nullptr, nullptr, inst, nullptr);
    if (!h) return 1;
    if (g.cfg.topmost) setTopmost(true, false);
    ShowWindow(h, show);
    UpdateWindow(h);
    Log::event("ui.start", true, -1, std::string("version ") + kVersion);
    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    if (g.stopper.joinable()) g.stopper.join();
    if (g.rec.active()) g.rec.stop();
    if (!waitShooter(3000) || !waitPacker(3000) || !waitWorker(g.updater, g.updDone, 10000, "the update") ||
        !waitWorker(g.checker, g.checkDone, 300, "the update check")) {
        // A worker is stuck in file I/O: end the process here (ExitProcess stops that thread first and runs no
        // destructors that it could still touch), so nothing stays behind without a window.
        if (g.mutex) CloseHandle(g.mutex);
        ExitProcess(0);
    }
    if (g.mutex) CloseHandle(g.mutex);
    g.painter = UiPainter{};   // release WIC / Direct2D before COM is uninitialized
    return 0;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    Log::init();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    HRESULT mf = MFStartup(MF_VERSION, MFSTARTUP_FULL);
    if (FAILED(mf)) Log::event("mf.startup", false, -1, "Media Foundation is not available (Windows N edition?)", mf);
    Args a = parseArgs();
    int code = !a.snapshot.empty() ? runSnapshot(a.snapshot, static_cast<UINT>(std::max(0, a.dpi)), a.at)
               : a.quit            ? quitRunning(a.jsonOut)
               : a.any             ? runCli(a)
                                   : runUi(inst, show, a);
    if (SUCCEEDED(mf)) MFShutdown();
    CoUninitialize();
    return code;
}
