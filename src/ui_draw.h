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
enum class Part { None, Record, Audio, Pin, Shot, Gear, Mic, Sys, Check, Open, Guide };

constexpr float kClientW = 480, kClientH = 304;   // client area in DIPs

D2D1_RECT_F partRect(Part p);
Part hitTest(float x, float y);   // DIPs; the label column of a channel row opens its menu too
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
    std::wstring pathLabel = L"保存先";
    std::wstring path;
    Part hot = Part::None, pressed = Part::None, focus = Part::None;
    bool showFocus = false;   // the focus ring appears only after the keyboard was used
    float pulse = 1;          // 0..1, the REC lamp breathes
    bool topmost = false;     // pinned above other windows
    int shots = 0;            // screenshots in the current recording
    bool shotFlash = false;   // a screenshot was just taken
    bool alert = false;       // the recording needs attention (amber deck); a passing notice never sets it
    std::wstring recHotkey, shotHotkey, audioHotkey;   // shortcut hints (empty when not registered)
    std::wstring comment;     // what しおり says under the buttons
    Tone commentTone = Tone::Normal;
};

bool partEnabled(UiModel::Phase ph, bool audioOnly, Part p);

class UiPainter {
public:
    bool init();                                     // factories and text formats (device independent)
    void paint(HWND h, UINT dpi, const UiModel& m);  // creates the window target on demand
    void resize(UINT w, UINT h);                     // pixels
    void dropTarget();                               // DPI change / device lost: recreated on the next paint
    // Renders the client area to a PNG without a window (for checking the look; no screen content).
    HRESULT snapshot(const std::wstring& png, UINT dpi, const UiModel& m);
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
    void drawChip(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, const UiModel& m, float x, bool audio);
    void drawHint(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, const UiModel& m, float right);
    void drawStatus(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, const UiModel& m, const D2D1_RECT_F& r);
    void drawChannel(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, ID2D1SolidColorBrush* fx, const UiModel& m, Part part,
                     const ChannelView& c, const wchar_t* label, wchar_t icon, float y);
    void drawMeter(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, const ChannelView& c, float y);
    void drawPath(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, ID2D1SolidColorBrush* fx, const UiModel& m);
    void drawButton(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, const UiModel& m, Part p, wchar_t icon,
                    const std::wstring& label, bool toggled);
    void drawGuide(ID2D1RenderTarget* rt, const UiModel& m);
    void drawComment(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, const UiModel& m);
    void drawSideLabel(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, const D2D1_RECT_F& r, const wchar_t* title,
                       const wchar_t* sub, const std::wstring& key, bool trailing);
    void panel(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, const D2D1_RECT_F& r, float radius, UINT32 fill,
               UINT32 border, UINT32 edge);
    void focusRing(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, const D2D1_RECT_F& r, float radius);
    void loadGuides(ID2D1RenderTarget* rt, UINT dpi);
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
        button_, icon_, iconLarge_, iconSmall_, chevron_, counter_, title_, sub_, name_;
    Com<ID2D1Bitmap> guide_[2];   // idle / recording, scaled to the DPI of the target they belong to
    D2D1_SIZE_F guideDip_[2] = {};
    UINT guideDpi_ = 0;
    bool guideTried_ = false;   // loading was tried for this target and DPI (a missing picture is not retried)
    UINT dpi_ = 96;
};

}  // namespace rc
