// Rec Cue! — "recording" signs outside the window, red while recording the screen and blue while recording audio only:
//   - a small dot on the taskbar button (overlay icon, bottom right) that slowly blinks
//   - a small steady dot at the top-left corner of the screen (like the macOS recording dot). It is left out of every
//     capture, so it never shows up in a recording or a screenshot, and clicks go through it.
// And the buttons under the window's thumbnail on the taskbar (ThumbBar), to start, pause and stop from there.
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
    // Paused: the taskbar dot stops blinking and shows ❚❚, and the corner dot becomes a ring (nothing is recorded).
    void setPaused(bool paused);
    void stop();
    void shutdown();          // stop() and let go of the taskbar (before COM is uninitialized)

private:
    void makeIcons();
    void freeIcons();
    void showDot(int monitorIndex);
    void paintCornerDot();
    void applyOverlay(int level);

    HWND owner_ = nullptr;
    Com<ITaskbarList3> taskbar_;
    RecKind kind_ = RecKind::None;
    bool paused_ = false;
    static constexpr int kLevels = 5;   // brightness steps of the blinking taskbar dot; icons_[kLevels]: paused (❚❚)
    HICON icons_[kLevels + 1] = {};
    int level_ = -1;
    HWND dot_ = nullptr;
    POINT dotPos_{};
    int dotN_ = 0;            // the corner dot's window size (px) ...
    float dotScale_ = 1;      // ... and its monitor's DPI scale
    int64_t nextRaise_ = 0;   // keeps the screen dot above other always-on-top windows
};

// Buttons under the window's thumbnail, shown when the pointer rests on the taskbar button (as a music player has
// them): record and audio only while nothing records, pause (resume) and stop while recording. A click comes to the
// window as WM_COMMAND with HIWORD(wParam) == THBN_CLICKED and LOWORD(wParam) = the button.
enum ThumbButton : UINT { kThumbRecord = 1, kThumbAudio, kThumbPause, kThumbStop };

struct ThumbView {
    bool recording = false;   // a recording runs, or is being saved
    bool busy = false;        // a recording is starting or being saved: nothing can be pressed
    bool paused = false;
    bool audioOnly = false;
    std::wstring recKey, audioKey;   // shortcut texts for the tips (empty: not registered)
    std::wstring audioFile;          // what recording audio only makes (MP3), for its tip
    bool operator==(const ThumbView& o) const {
        return recording == o.recording && busy == o.busy && paused == o.paused && audioOnly == o.audioOnly &&
               recKey == o.recKey && audioKey == o.audioKey && audioFile == o.audioFile;
    }
};

class ThumbBar {
public:
    enum { kRecordIcon, kAudioIcon, kPauseIcon, kResumeIcon, kStopIcon, kIcons };
    ~ThumbBar();
    // The taskbar button exists ("TaskbarButtonCreated", sent again when Explorer restarts): adds the buttons.
    void taskbarReady(HWND owner, const ThumbView& v);
    // Takes the icons (kIcons of them, in the order above) and keeps them until new ones come or shutdown().
    void setIcons(const HICON* icons);
    void show(const ThumbView& v);   // the buttons for this state (does nothing when it has not changed)
    void shutdown();

private:
    void apply(bool add);
    void freeIcons();

    HWND owner_ = nullptr;
    Com<ITaskbarList3> taskbar_;
    bool added_ = false;
    HICON icons_[kIcons] = {};
    ThumbView view_;
    bool shown_ = false;   // view_ is on the taskbar
};

}  // namespace rc
