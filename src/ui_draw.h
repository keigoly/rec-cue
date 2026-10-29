// Rec Cue! — the window's look: one Direct2D / DirectWrite scene drawn from a plain model.
// Design A "Executive Dark" (Gemini 4 Argon, 2026-10-22): graphite surfaces with a 1 px top highlight, one ice-cyan
// accent, crimson only for recording the screen and blue only for recording audio, and the SD guide (the secretary of
// the app icon) standing in the bottom-right corner. The deck is split: recording on the left, audio only on the
// right; the side that records widens (animated) and shows the time.
// main.cpp owns the state and the input handling; this file only draws (to the window or to a PNG).
#pragma once

#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>

#include "common.h"

namespace rc {

// Clickable parts of the window. Drawing and hit-testing share the same rectangles. Guide: しおり or her bubble.
// Pause / AudioPause: ❚❚ (▶ while paused) beside the stop button of the screen / the audio-only side.
// Screen: the row that picks the monitor to record (its field or its small map).
// AudioFormat: the file of a recording of sound alone (MP3 / WAV / M4A), a chip beside 録音 while nothing records.
// The settings view (the gear) has its own parts: Back, the two save folders (OutDir / ShotDir: the field opens the
// folder in Explorer, OutChange / ShotChange: 変更 picks another), Logs (ログを取得) and the two links.
// Pin, Shot, Gear and Guide are in both views.
enum class Part { None, Record, Audio, Pin, Shot, Gear, Mic, Sys, Check, Open, Guide, Pause, AudioPause, Screen, AudioFormat,
                  Back, OutDir, OutChange, ShotDir, ShotChange, Logs, Github, Site };

constexpr float kClientW = 480, kClientH = 340;   // client area in DIPs

D2D1_RECT_F partRect(Part p);
// DIPs; the label column of a channel row opens its menu too (and of a folder row, the folder)
Part hitTest(float x, float y, bool settings);
bool inView(Part p, bool settings);   // the part is in the settings view / in the main view
D2D1_RECT_F guideRect();          // where the SD guide stands (for her tooltip)

// The title bar in the window's own colours (DWM), so the two read as one surface.
constexpr COLORREF kFrameColor = RGB(0x0E, 0x10, 0x15);
constexpr COLORREF kFrameText = RGB(0xC8, 0xCD, 0xD8);
constexpr COLORREF kFrameBorder = RGB(0x2A, 0x30, 0x3C);

// The SD guide pictures (RCDATA in app.rc): standing with the booklet / taking notes while recording.
constexpr int kResGuideIdle = 101, kResGuideRec = 102;

struct ChannelView {
    std::wstring choice;   // the setting as listed in the menu (shown while nothing is open)
    std::wstring mode;     // 自動 / 既定 / 通話用の既定 when the device was picked for the user (empty otherwise)
    std::wstring device;   // device actually open (empty while idle)
    bool active = false;
    bool warn = false;     // silent for a long time while recording (amber)
    double db = -90;       // peak over the last 100 ms (dBFS)
    double hold = -90;     // peak hold
};

// The monitor to record: the setting as the field shows it, and the desktop's layout for the small map beside it.
struct ScreenView {
    std::wstring choice;            // 1: メイン 1920×1080
    bool active = false;            // the screen is being recorded (or starting / saving)
    std::vector<RECT> monitors;     // desktop coordinates, numbered 1.. in this order
    int selected = 0;               // the one recorded
};

struct UiModel {
    enum class Phase { Idle, Checking, Starting, Recording, Saving };
    enum class Tone { Normal, Good, Warn, Bad };
    Phase phase = Phase::Idle;
    bool audioOnly = false;   // the recording being started, made or saved is audio only (blue instead of red)
    float split = 0;          // the deck: 0 halves, 1 the recording side open, -1 the audio side open (animated)
    std::wstring timer = L"00:00:00";
    std::wstring status;
    Tone tone = Tone::Normal;
    ChannelView mic, sys;
    ScreenView screen;
    std::wstring pathLabel = L"保存先";
    std::wstring path;
    Part hot = Part::None, pressed = Part::None, focus = Part::None;
    bool showFocus = false;   // the focus ring appears only after the keyboard was used
    float pulse = 1;          // 0..1, the REC lamp breathes
    bool topmost = false;     // pinned above other windows
    int shots = 0;            // screenshots in the current recording
    bool shotFlash = false;   // a screenshot was just taken
    bool alert = false;       // the recording needs attention (amber deck); a passing notice never sets it
    bool paused = false;      // the recording is paused (amber PAUSE chip and band, the time stands still)
    std::wstring audioFormat = L"MP3";   // what recording audio only makes (the chip beside 録音)
    std::wstring recHotkey, shotHotkey, audioHotkey;   // shortcut hints (empty when not registered)
    std::wstring comment;     // what しおり says under the buttons
    Tone commentTone = Tone::Normal;
    // the settings view (the gear), in place of the deck and the rows
    bool settings = false;
    std::wstring outDir;      // where recordings go
    std::wstring shotDir;     // where screenshots go (empty: with the recordings)
    struct Key {
        std::wstring text;    // as set (なし when there is none)
        bool taken = false;   // another app has it, so it does nothing
    };
    Key keys[3];              // record, audio only, screenshot
    bool logsBusy = false;    // ログを取得 is packing the files
};

bool partEnabled(UiModel::Phase ph, bool audioOnly, Part p);

// The pictures of the buttons under the taskbar thumbnail (ThumbBar in indicator.h).
enum class ThumbGlyph { Record, Audio, Pause, Resume, Stop };

class UiPainter {
public:
    bool init();                                     // factories and text formats (device independent)
    void paint(HWND h, UINT dpi, const UiModel& m);  // creates the window target on demand
    void resize(UINT w, UINT h);                     // pixels
    void dropTarget();                               // DPI change / device lost: recreated on the next paint
    // Renders the client area to a PNG without a window (for checking the look; no screen content).
    HRESULT snapshot(const std::wstring& png, UINT dpi, const UiModel& m);
    // A px x px icon for a taskbar thumbnail button, record red and audio blue as in the window, the others white
    // (dark on the light taskbar), and the same as a PNG for checking the look (--ui-snapshot).
    HICON thumbIcon(ThumbGlyph g, int px, bool light);
    HRESULT thumbPng(ThumbGlyph g, int px, bool light, const std::wstring& png);
    // The room for しおり's line in her bubble and the width a line takes there (DIPs; --lines-check).
    float commentRoom() const;
    float commentWidth(const std::wstring& s) const;

private:
    enum class Look { Plain, Accent, Flash };
    void draw(ID2D1RenderTarget* rt, const UiModel& m);
    void drawIconButton(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, const UiModel& m, Part p, wchar_t icon, Look look,
                        int counter);
    void drawDeck(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, const UiModel& m);
    void deckPanel(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, const D2D1_RECT_F& r, int tint);
    void drawRecord(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, const UiModel& m);
    void drawAudio(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, const UiModel& m);
    void drawPause(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, const UiModel& m, Part p);
    void drawChip(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, const UiModel& m, float x, float y, bool audio);
    void drawHint(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, const UiModel& m, float right);
    void drawStatus(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, const UiModel& m, const D2D1_RECT_F& r);
    void drawChannel(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, ID2D1SolidColorBrush* fx, const UiModel& m, Part part,
                     const ChannelView& c, const wchar_t* label, wchar_t icon, float y);
    void drawMeter(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, const ChannelView& c, float y);
    void drawScreen(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, ID2D1SolidColorBrush* fx, const UiModel& m);
    void drawScreenMap(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, const ScreenView& s, const D2D1_RECT_F& box);
    void drawPath(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, ID2D1SolidColorBrush* fx, const UiModel& m);
    void drawButton(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, const UiModel& m, Part p, wchar_t icon,
                    const std::wstring& label, bool toggled);
    void drawGuide(ID2D1RenderTarget* rt, const UiModel& m);
    void drawComment(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, const UiModel& m);
    void drawSideLabel(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, const D2D1_RECT_F& r, const wchar_t* title,
                       const wchar_t* sub, const std::wstring& key, bool trailing);
    void drawFormatChip(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, const UiModel& m);
    void drawSettings(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, const UiModel& m);
    void drawSection(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, const wchar_t* title, float y);
    void drawDirRow(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, const UiModel& m, Part field, Part change, wchar_t icon,
                    const wchar_t* label, const std::wstring& path, const wchar_t* unset, float y);
    void drawKeys(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, const UiModel& m, float y);
    void drawAbout(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b);
    void panel(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, const D2D1_RECT_F& r, float radius, UINT32 fill,
               UINT32 border, UINT32 edge);
    void focusRing(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, const D2D1_RECT_F& r, float radius);
    void loadGuides(ID2D1RenderTarget* rt, UINT dpi);
    HRESULT renderThumb(ThumbGlyph g, int px, bool light, Com<IWICBitmap>* out);
    HRESULT writePng(IWICBitmap* bmp, const std::wstring& png) const;
    void text(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, const std::wstring& s, IDWriteTextFormat* f,
              const D2D1_RECT_F& r, float spacing = 0, float* width = nullptr);
    float textWidth(const std::wstring& s, IDWriteTextFormat* f) const;
    Com<IDWriteTextLayout> layout(const std::wstring& s, IDWriteTextFormat* f, const D2D1_RECT_F& r) const;
    Com<IDWriteTextFormat> format(const wchar_t* family, float size, DWRITE_FONT_WEIGHT w,
                                  DWRITE_FONT_STYLE st = DWRITE_FONT_STYLE_NORMAL) const;
    bool hasFont(const wchar_t* family) const;

    Com<ID2D1Factory> d2d_;
    Com<IDWriteFactory> dw_;
    Com<IWICImagingFactory> wic_;
    Com<ID2D1HwndRenderTarget> target_;
    Com<IDWriteTextFormat> brand_, timer_, chip_, chipJp_, hint_, status_, label_, field_, db_, dbJp_, path_, badge_,
        button_, icon_, iconLarge_, iconSmall_, chevron_, counter_, title_, sub_, name_, thumbMic_, key_;
    Com<ID2D1Bitmap> guide_[2];   // idle / recording, scaled to the DPI of the target they belong to
    D2D1_SIZE_F guideDip_[2] = {};
    UINT guideDpi_ = 0;
    bool guideTried_ = false;   // loading was tried for this target and DPI (a missing picture is not retried)
    UINT dpi_ = 96;
};

}  // namespace rc
