// Rec Cue! — screenshot of one monitor without the Rec Cue window, saved as PNG (and copied to the clipboard).
// The window stays on the monitor: WDA_EXCLUDEFROMCAPTURE leaves it out of the picture only (no flicker),
// so the shot shows what is behind it even when it is pinned on top.
#pragma once

#include "common.h"

namespace rc {

struct ShotImage {
    UINT w = 0, h = 0;
    std::vector<uint32_t> px;   // top-down BGRX
};

struct ShotInfo {
    bool excluded = false;   // the window was left out with display affinity
    bool hidden = false;     // fallback (older Windows): the window was hidden for the moment of the shot
    double flushMs = 0;      // waiting for the compositor to draw without the window
    double bltMs = 0;
};

// Captures monitor `index` (0 = primary, same numbering as the recording) without the window `self`.
bool captureMonitor(int index, HWND self, ShotImage* out, ShotInfo* info, std::string* err);
// PNG (24-bit) via WIC. Written to <path>.part first, then renamed, so a half-written file never appears.
HRESULT savePng(const ShotImage& img, const std::wstring& path);
bool copyToClipboard(HWND owner, const ShotImage& img);
// <dir>\<base><ext>, or <dir>\<base> (2)<ext> ... when the name is taken.
std::wstring uniquePath(const std::wstring& dir, const std::wstring& base, const wchar_t* ext);

}  // namespace rc
