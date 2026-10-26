// Rec Cue! — what 湊 しおり says in the bubble under the buttons (voice: a proper secretary, polite and reserved).
// About 1,000 lines live in res/shiori-lines.txt (UTF-8, RCDATA 103). They are read once at start (about 1 ms, no
// copies: one UTF-16 buffer and an index). Each line belongs to a scene and may be limited to a time of day, a day of
// the week, a month or season, the kind of recording, or one moment of it (minute 30, the 5th screenshot).
// A line comes back only after half of the other lines of its scene have been said.
#pragma once

#include "common.h"

namespace rc {

constexpr int kResLines = 103;

enum class Scene {
    Start, Back, Talk, Tip,                             // the window opens / is used again after a while / clicked
    Check, CheckMic, CheckSys, CheckQuiet, CheckEnd,    // the level check
    Starting, Rec, RecOn, Audio, AudioOn,               // a recording starts; ...On: every 10 min and milestones
    Shot, ShotIdle,                                     // a screenshot in a recording / outside one
    WarnScreen, WarnSys, WarnMic, WarnNoSys, Recover,   // the recording needs attention / is fine again
    Saving, Saved, FailStart, FailSave, FailShot,
    PinOn, PinOff,
    Pause, Resume,                                      // the recording is paused / goes on
    Count
};

const char* sceneName(Scene s);   // its [header] in the file

// What a line may refer to or depend on.
struct SayContext {
    bool audio = false;      // the recording is audio only
    int n = 0;               // the screenshot's number (Shot), or minutes into the recording (RecOn / AudioOn)
    int shots = 0;           // screenshots in the recording that was saved
    double sec = 0;          // length of that recording
    double mb = 0;           // its size
    bool autoStop = false;   // it stopped by itself
    bool clip = false;       // the screenshot is on the clipboard too
    std::wstring recKey, audioKey, shotKey;   // shortcut texts; a line naming one that is not registered is skipped
};

class Lines {
public:
    Lines();
    // Reads RCDATA kResLines. A line with an unknown scene, tag or {name} is skipped and reported in `problems`.
    bool load(HINSTANCE inst, std::vector<std::string>* problems);
    void seed(uint64_t s) { rng_ = s ? s : 1; }   // the same choices every time (--ui-snapshot)
    void setClock(const SYSTEMTIME& t) { clock_ = t; fixedClock_ = true; }   // pick for this time (--ui-snapshot --at)
    int roll(int n) { return n > 0 ? static_cast<int>(next() % static_cast<uint64_t>(n)) : 0; }   // 0..n-1
    // A line for the scene at this moment with {n} {t} {mb} {shots} {rec_key} ... filled in; empty if none fits.
    std::wstring pick(Scene s, const SayContext& c);
    bool milestone(Scene s, int n) const;   // a line is meant for exactly this minute / screenshot
    int lastSource() const { return lastSource_; }   // its line number in shiori-lines.txt (for the log)

    // --lines-check: every line, filled in with the longest values, to measure it.
    size_t size() const { return lines_.size(); }
    Scene sceneAt(size_t i) const { return lines_[i].scene; }
    int sourceAt(size_t i) const { return lines_[i].source; }
    std::wstring longestAt(size_t i) const;

private:
    struct Line {
        Scene scene = Scene::Start;
        uint8_t bands = 0;    // times of day (a bit per band), 0 = any
        uint8_t days = 0;     // days of the week (bit 0 = Sunday), 0 = any
        uint16_t months = 0;  // bits 1..12, 0 = any
        uint16_t flags = 0;   // kind of recording, short / long / with screenshots, special days, shortcuts it names
        int16_t n = -1;       // only at this minute / screenshot (-1 = any)
        int source = 0;       // line number in the file
        uint32_t at = 0, len = 0;   // in text_
        uint32_t said = 0;    // the scene's count when it was last said (0 = not yet)
    };
    bool fits(const Line& l, const SayContext& c, const SYSTEMTIME& now) const;
    std::wstring fill(const Line& l, const SayContext& c) const;
    uint64_t next();

    std::wstring text_;
    std::vector<Line> lines_;
    std::vector<uint16_t> byScene_[static_cast<int>(Scene::Count)];
    uint32_t said_[static_cast<int>(Scene::Count)] = {};
    uint64_t rng_ = 1;
    int lastSource_ = 0;
    SYSTEMTIME clock_{};
    bool fixedClock_ = false;
};

}  // namespace rc
