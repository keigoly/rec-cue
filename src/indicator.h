// Rec Cue! — "recording" signs outside the window, red while recording the screen and blue while recording audio only:
//   - a small dot on the taskbar button (overlay icon, bottom right) that slowly blinks
//   - a small steady dot at the top-left corner of the screen (like the macOS recording dot). It is left out of every
//     capture, so it never shows up in a recording or a screenshot, and clicks go through it.
#pragma once

#include <shobjidl.h>

#include "common.h"

namespace rc {

enum class RecKind { None, Screen, Audio };

class Indicators {
public:
    ~Indicators();
    // The taskbar button exists (the "TaskbarButtonCreated" message; sent again when Explorer restarts).
    void taskbarReady(HWND owner);
    // Shows both signs. monitorIndex: the recorded monitor (0 = primary), same numbering as the recording.
    void start(HWND owner, RecKind kind, int monitorIndex);
    void tick(float pulse);   // 0..1 (the 2 s breathing of the REC lamp); updates the taskbar dot when its step changes
    void stop();
    void shutdown();          // stop() and let go of the taskbar (before COM is uninitialized)

private:
    void makeIcons();
    void freeIcons();
    void showDot(int monitorIndex);
    void applyOverlay(int level);

    HWND owner_ = nullptr;
    Com<ITaskbarList3> taskbar_;
    RecKind kind_ = RecKind::None;
    static constexpr int kLevels = 5;   // brightness steps of the blinking taskbar dot
    HICON icons_[kLevels] = {};
    int level_ = -1;
    HWND dot_ = nullptr;
    int64_t nextRaise_ = 0;   // keeps the screen dot above other always-on-top windows
};

}  // namespace rc
