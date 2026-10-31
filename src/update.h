// Rec Cue! — アップデート: when the window opens it asks GitHub for the latest release (one HTTPS GET to the API, on a
// worker; nothing is sent but the request itself). A newer version that was not skipped is offered as OBS does:
// 今すぐ更新 / あとで / この版をスキップ. 今すぐ更新 downloads that release's rec-cue.exe beside this exe, checks its size
// and SHA-256 against the release, runs it once with --version, swaps the two files (this one is kept as
// <name>.prev.exe) and opens the new one in place of this window.
#pragma once

#include <atomic>

#include "common.h"

namespace rc {

extern const wchar_t* const kUpdateApi;   // api.github.com/repos/keigoly/rec-cue/releases/latest

// The latest release as GitHub tells it.
struct Release {
    std::wstring version;              // 0.5.1 (the tag without its v)
    std::wstring page;                 // the release's page on GitHub
    std::wstring published;            // 2026-11-02 (local date)
    std::wstring assetUrl;             // its rec-cue.exe
    uint64_t size = 0;
    std::string sha256;                // lowercase hex (empty: GitHub gave no digest)
    std::vector<std::wstring> notes;   // 「x.y.z の変更」 as plain lines, "title\tthe rest" (the title may be empty)
};

struct UpdateCheck {
    bool ok = false;     // a release with a rec-cue.exe came back
    int status = 0;      // HTTP status (0: no answer)
    double ms = 0;
    std::string error;
    Release latest;
};

UpdateCheck checkLatest(const std::wstring& api = kUpdateApi);
// <0 / 0 / >0 as a is older than / the same as / newer than b (dotted numbers; anything else ends a part)
int compareVersions(const std::wstring& a, const std::wstring& b);

// What 今すぐ更新 is doing: written by the worker, read by the window.
struct UpdateProgress {
    enum Stage { Download, Verify, Swap, Done };
    std::atomic<int> stage{Download};
    std::atomic<uint64_t> got{0};
};

struct UpdateResult {
    bool ok = false;
    std::string step;     // where it stopped: folder / download / size / sha256 / smoke / swap
    std::string error;
    uint64_t bytes = 0;
    double downloadMs = 0, verifyMs = 0, smokeMs = 0, swapMs = 0;
};

// Puts release `r` in the place of `exe`, which is kept as <name>.prev.exe. Does not open it.
UpdateResult installRelease(const Release& r, const std::wstring& exe, UpdateProgress* p);
std::string updateCheckJson(const UpdateCheck& c);
std::string updateResultJson(const UpdateResult& u);
// Opens `exe` as the Start menu would (no arguments, so any version opens), in front as this window is.
bool relaunch(const std::wstring& exe);
std::wstring selfPath();   // this exe

}  // namespace rc
