// Rec Cue! — the window's look (see ui_draw.h).
#include "ui_draw.h"

#include <dwrite_1.h>

#include <algorithm>
#include <cmath>
#include <cwchar>

namespace rc {

namespace {

// ---- palette (sRGB, design A) -------------------------------------------------
constexpr UINT32 kCanvas = 0x13161C;
constexpr UINT32 kHeroTop = 0x191E28, kHeroBottom = 0x151921, kHeroBorder = 0x262C3A, kHeroEdge = 0x303747;
constexpr UINT32 kRecHeroTop = 0x21171D, kRecHeroBottom = 0x17151B, kRecHeroBorder = 0x4A2330, kRecHeroEdge = 0x5E2B3B;
constexpr UINT32 kAudHeroTop = 0x161C2A, kAudHeroBottom = 0x13161F, kAudHeroBorder = 0x1F3A5F, kAudHeroEdge = 0x2A4F7E;
constexpr UINT32 kWarnHeroTop = 0x231C16, kWarnHeroBottom = 0x181619, kWarnHeroBorder = 0x5C431C, kWarnHeroEdge = 0x735422;
constexpr UINT32 kControl = 0x1B202A, kControlHot = 0x232A38, kControlDown = 0x141820, kControlBorder = 0x2B3240,
                 kControlBorderHot = 0x3B4559, kEdge = 0x353D4F;
constexpr UINT32 kOffFill = 0x161922, kOffBorder = 0x222733, kOffText = 0x5A6270;
constexpr UINT32 kInput = 0x181C24, kInputBorder = 0x2A3140, kInputEdge = 0x222834;
constexpr UINT32 kWell = 0x151820, kWellBorder = 0x222733;
constexpr UINT32 kMeterBox = 0x161A22, kMeterBorder = 0x242A36, kMeterTrack = 0x0D1016, kTick = 0x2D3545, kPeak = 0xE2E8F0;
constexpr UINT32 kText = 0xF2F4F8, kText2 = 0xB4BCCB, kText3 = 0x6E7787, kLabel = 0xD0D6E2, kFieldText = 0xC8CDD8,
                 kIcon = 0x8C95A6, kTimerLive = 0xF8FAFC, kHint = 0x687182, kPath = 0x9AA2B2, kPathFile = 0xE2E8F0,
                 kDbLive = 0xD8DEE9;
constexpr UINT32 kCyan = 0x38BDF8, kCyanDeep = 0x0284C7, kCyanText = 0x7CE0FF, kTeal = 0x2DD4BF;
constexpr UINT32 kPinOnFill = 0x132838, kPinOnBorder = 0x26658C, kPinOnEdge = 0x3888B8, kFlashFill = 0x162E3E;
constexpr UINT32 kCrimson = 0xE11D48, kRose = 0xF43F5E, kRoseLight = 0xFB7185, kRosePale = 0xFDA4AF, kRecChipBg = 0x3B121E,
                 kRecOuter = 0x2D121B, kIdleOuter = 0x11141B, kIdleRing = 0x2E3646, kBusy = 0x3A3F48;
constexpr UINT32 kBlue = 0x3B82F6, kBlueHi = 0x60A5FA, kBluePale = 0xBFDBFE, kBlueChipBg = 0x0F213D, kBlueOuter = 0x0E1A2F,
                 kBlueText = 0x93C5FD;
constexpr UINT32 kAmber = 0xFBBF24, kAmberText = 0xFCD34D, kWarnBg = 0x2B1F11, kWarnBorder = 0xD97706, kSilentBorder = 0x92400E;
constexpr UINT32 kToastBg = 0x112A3A, kToastBorder = 0x0284C7;

D2D1_COLOR_F C(UINT32 rgb, float a = 1.f) { return D2D1::ColorF(rgb, a); }

// ---- layout (DIPs, client area 480 x 340) -----------------------------------
constexpr float kL = 14, kR = 466;
constexpr D2D1_RECT_F kBrandBox{kL, 8, 200, 36};
constexpr D2D1_RECT_F kPinBox{370, 8, 398, 36};    // header tools: icons only
constexpr D2D1_RECT_F kShotBox{404, 8, 432, 36};
constexpr D2D1_RECT_F kGearBox{438, 8, kR, 36};
constexpr float kDeckTop = 42, kDeckBottom = 110, kDeckGap = 6;
constexpr float kDeckMid = 237;                        // right edge of the recording side when neither records (halves)
constexpr float kEdgeRec = 396, kEdgeAud = 82;         // ... while the screen records / while audio only records
constexpr D2D1_POINT_2F kRec{50, 76};                  // recording side: its button stays on the left
constexpr float kRecR = 26;
constexpr D2D1_POINT_2F kAud{434, 76};                 // audio side: its button stays on the right
constexpr float kAudR = 22;
constexpr D2D1_RECT_F kTimerBox{88, 47, 222, 79};
constexpr float kChipX = 224, kChipY = 53, kChipH = 20;
constexpr float kStatusTop = 81, kStatusBottom = 103;
constexpr float kAudTimeRight = kAud.x - kAudR - 12;   // the audio time ends just before its button
// once a recording runs, ❚❚ sits beside its stop button and the time moves over to make room
constexpr D2D1_POINT_2F kPause{100, 76};
constexpr float kPauseR = 17;
constexpr D2D1_POINT_2F kAudPause{391, 76};
constexpr float kAudPauseR = 15;
constexpr D2D1_RECT_F kRecTimerBox{126, 47, 260, 79};
constexpr float kRecChipX = 262;
constexpr float kAudTimeRightLive = kAudPause.x - kAudPauseR - 10;
constexpr D2D1_RECT_F kFormatBox{316, 51, 364, 69};   // MP3 ▾ on the title line of the audio side, left of 録音
constexpr float kScreenY = 120, kMicY = 156, kSysY = 192, kRowH = 28;   // 画面 / マイク / PC の音声
constexpr float kFieldL = 98, kFieldR = 294, kMeterL = 302, kMeterR = 414, kDbL = 418;
constexpr D2D1_RECT_F kPathBox{kL, 230, 400, 256};
constexpr D2D1_RECT_F kCheckBox{kL, 266, 204, 296};
constexpr D2D1_RECT_F kOpenBox{210, 266, 400, 296};
constexpr D2D1_RECT_F kGuideBox{402, 240, 478, 338};   // the SD guide stands here, feet on the bottom edge
constexpr float kGuideH = 96;
constexpr D2D1_RECT_F kBubble{kL, 304, 394, 332};      // what しおり says, under the buttons, pointing at her
// the settings view (the gear): the same header with ← and 設定, then a section each for the save folders, the
// shortcuts, and the log and the links; しおり and her bubble stay where they are
constexpr D2D1_RECT_F kBackBox{kL, 8, 42, 36};
constexpr D2D1_RECT_F kSetTitleBox{50, 8, 96, 36};
constexpr float kSetChipX = 98, kSetChipY = 12;        // REC / AUDIO / PAUSE and the time, while a recording goes on
constexpr float kSecDirs = 44, kOutY = 62, kShotDirY = 96;   // 保存先: 録画・録音 / スクショ
constexpr float kDirR = 398, kChangeL = 404;           // the folder's field (from kFieldL) and 変更
constexpr float kSecKeys = 138, kKeysY = 156, kKeysH = 26;   // ショートカットキー
constexpr float kSecLog = 196;                         // ログと情報
constexpr D2D1_RECT_F kLogsBox{kL, 214, 154, 244};
constexpr D2D1_RECT_F kGithubBox{160, 214, 276, 244};
constexpr D2D1_RECT_F kSiteBox{282, 214, 396, 244};
constexpr D2D1_RECT_F kAboutBox{kL, 254, 396, 278};    // the version (what 「Rec Cue! について」 used to show)

// Segoe Fluent Icons / Segoe MDL2 Assets
constexpr wchar_t kIconMic = 0xE720, kIconSpeakers = 0xE767, kIconPulse = 0xE9D9, kIconStop = 0xE71A,
                  kIconFolderOpen = 0xE838, kIconSettings = 0xE713, kIconChevron = 0xE70D, kIconFolder = 0xE8B7,
                  kIconPin = 0xE718, kIconPinFill = 0xE842, kIconCamera = 0xE722, kIconWarn = 0xE7BA,
                  kIconCheck = 0xE73E, kIconMonitor = 0xE7F4, kIconBack = 0xE72B, kIconVideo = 0xE714,
                  kIconKeyboard = 0xE765, kIconDownload = 0xE896, kIconOpenIn = 0xE8A7, kIconInfo = 0xE946;

D2D1_RECT_F inset(const D2D1_RECT_F& r, float d) { return {r.left + d, r.top + d, r.right - d, r.bottom - d}; }
D2D1_RECT_F fieldRect(float y) { return {kFieldL, y, kFieldR, y + kRowH}; }
D2D1_RECT_F circleRect(D2D1_POINT_2F c, float r) { return {c.x - r, c.y - r, c.x + r, c.y + r}; }

std::wstring dbText(const ChannelView& c) {
    if (!c.active) return L"—";
    if (c.db <= -89) return L"無音";
    wchar_t b[24];
    const long v = std::lround(c.db);
    if (v < 0) swprintf(b, 24, L"−%ld dB", -v);
    else swprintf(b, 24, L"%ld dB", v);
    return b;
}

bool hasExtension(const std::wstring& p) {
    const size_t dot = p.find_last_of(L'.'), sep = p.find_last_of(L'\\');
    return dot != std::wstring::npos && (sep == std::wstring::npos || dot > sep);
}

// The part can be pressed now (ログを取得 waits while it packs the files).
bool usable(const UiModel& m, Part p) { return partEnabled(m.phase, m.audioOnly, p) && !(p == Part::Logs && m.logsBusy); }

}  // namespace

D2D1_RECT_F partRect(Part p) {
    switch (p) {
        case Part::Record: return circleRect(kRec, kRecR);
        case Part::Audio: return circleRect(kAud, kAudR);
        case Part::Pin: return kPinBox;
        case Part::Shot: return kShotBox;
        case Part::Gear: return kGearBox;
        case Part::Mic: return fieldRect(kMicY);
        case Part::Sys: return fieldRect(kSysY);
        case Part::Check: return kCheckBox;
        case Part::Open: return kOpenBox;
        case Part::Guide: return kGuideBox;
        case Part::Pause: return circleRect(kPause, kPauseR);
        case Part::AudioPause: return circleRect(kAudPause, kAudPauseR);
        case Part::Screen: return fieldRect(kScreenY);
        case Part::AudioFormat: return kFormatBox;
        case Part::Back: return kBackBox;
        case Part::OutDir: return {kFieldL, kOutY, kDirR, kOutY + kRowH};
        case Part::OutChange: return {kChangeL, kOutY, kR, kOutY + kRowH};
        case Part::ShotDir: return {kFieldL, kShotDirY, kDirR, kShotDirY + kRowH};
        case Part::ShotChange: return {kChangeL, kShotDirY, kR, kShotDirY + kRowH};
        case Part::Logs: return kLogsBox;
        case Part::Github: return kGithubBox;
        case Part::Site: return kSiteBox;
        case Part::None: break;
    }
    return {0, 0, 0, 0};
}

D2D1_RECT_F guideRect() { return kGuideBox; }

bool inView(Part p, bool settings) {
    switch (p) {
        case Part::Pin:
        case Part::Shot:
        case Part::Gear:
        case Part::Guide: return true;
        case Part::Back:
        case Part::OutDir:
        case Part::OutChange:
        case Part::ShotDir:
        case Part::ShotChange:
        case Part::Logs:
        case Part::Github:
        case Part::Site: return settings;
        case Part::None: return false;
        default: return !settings;
    }
}

Part hitTest(float x, float y, bool settings) {
    auto in = [&](const D2D1_RECT_F& r) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; };
    if (settings) {
        for (Part p : {Part::Back, Part::Pin, Part::Shot, Part::Gear, Part::OutChange, Part::ShotChange, Part::Logs,
                       Part::Github, Part::Site})
            if (in(partRect(p))) return p;
        if (in({kL, kOutY, kDirR, kOutY + kRowH})) return Part::OutDir;
        if (in({kL, kShotDirY, kDirR, kShotDirY + kRowH})) return Part::ShotDir;
        if (in(kGuideBox) || in(kBubble)) return Part::Guide;
        return Part::None;
    }
    auto inCircle = [&](D2D1_POINT_2F c, float r) { return (x - c.x) * (x - c.x) + (y - c.y) * (y - c.y) <= r * r; };
    if (inCircle(kRec, kRecR + 2)) return Part::Record;
    if (inCircle(kAud, kAudR + 2)) return Part::Audio;
    if (inCircle(kPause, kPauseR + 2)) return Part::Pause;   // only while recording (main.cpp keeps the enabled ones)
    if (inCircle(kAudPause, kAudPauseR + 2)) return Part::AudioPause;
    for (Part p : {Part::Pin, Part::Shot, Part::Gear, Part::Check, Part::Open, Part::AudioFormat})
        if (in(partRect(p))) return p;
    if (in({kL, kScreenY, kR, kScreenY + kRowH})) return Part::Screen;   // its map opens the menu too
    if (in({kL, kMicY, kFieldR, kMicY + kRowH})) return Part::Mic;
    if (in({kL, kSysY, kFieldR, kSysY + kRowH})) return Part::Sys;
    if (in(kGuideBox) || in(kBubble)) return Part::Guide;   // she says something else
    return Part::None;
}

// Each side of the deck starts and stops its own kind of recording; the other side waits meanwhile.
bool partEnabled(UiModel::Phase ph, bool audioOnly, Part p) {
    using P = UiModel::Phase;
    switch (p) {
        case Part::Record: return ph == P::Idle || ph == P::Checking || (ph == P::Recording && !audioOnly);
        case Part::Audio: return ph == P::Idle || ph == P::Checking || (ph == P::Recording && audioOnly);
        case Part::Mic:
        case Part::Sys:
        case Part::Screen:
        case Part::AudioFormat:
        case Part::OutChange:
        case Part::ShotChange:
        case Part::Check: return ph == P::Idle || ph == P::Checking;
        case Part::Pin:
        case Part::Shot:
        case Part::Gear:
        case Part::Back:
        case Part::OutDir:
        case Part::ShotDir:
        case Part::Logs:
        case Part::Github:
        case Part::Site:
        case Part::Open: return true;
        case Part::Guide: return ph != P::Starting && ph != P::Saving;
        case Part::Pause: return ph == P::Recording && !audioOnly;
        case Part::AudioPause: return ph == P::Recording && audioOnly;
        case Part::None: break;
    }
    return false;
}

// ---------------------------------------------------------------- setup

bool UiPainter::hasFont(const wchar_t* family) const {
    Com<IDWriteFontCollection> fc;
    if (FAILED(dw_->GetSystemFontCollection(fc.put(), FALSE))) return false;
    UINT32 i = 0;
    BOOL found = FALSE;
    return SUCCEEDED(fc->FindFamilyName(family, &i, &found)) && found;
}

Com<IDWriteTextFormat> UiPainter::format(const wchar_t* family, float size, DWRITE_FONT_WEIGHT w, DWRITE_FONT_STYLE st) const {
    Com<IDWriteTextFormat> f;
    if (FAILED(dw_->CreateTextFormat(family, nullptr, w, st, DWRITE_FONT_STRETCH_NORMAL, size, L"ja-JP", f.put()))) return f;
    f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    Com<IDWriteInlineObject> sign;
    if (SUCCEEDED(dw_->CreateEllipsisTrimmingSign(f.get(), sign.put()))) {
        const DWRITE_TRIMMING t{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        f->SetTrimming(&t, sign.get());
    }
    return f;
}

bool UiPainter::init() {
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory), nullptr, d2d_.put_void())))
        return false;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(dw_.put()))))
        return false;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IWICImagingFactory), wic_.put_void());
    const wchar_t* display = hasFont(L"Segoe UI Variable Display") ? L"Segoe UI Variable Display" : L"Segoe UI";
    const wchar_t* text = hasFont(L"Segoe UI Variable Text") ? L"Segoe UI Variable Text" : L"Segoe UI";
    const wchar_t* mono = hasFont(L"Cascadia Mono") ? L"Cascadia Mono" : L"Consolas";
    const wchar_t* icons = hasFont(L"Segoe Fluent Icons") ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets";
    // Latin faces first, kana and kanji fall back (ja-JP); a path keeps "\" instead of a yen sign.
    brand_ = format(display, 13, DWRITE_FONT_WEIGHT_BOLD);
    timer_ = format(mono, 26, DWRITE_FONT_WEIGHT_BOLD);
    chip_ = format(mono, 11, DWRITE_FONT_WEIGHT_BOLD);
    chipJp_ = format(text, 11, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    hint_ = format(mono, 10, DWRITE_FONT_WEIGHT_NORMAL);
    status_ = format(text, 11.5f, DWRITE_FONT_WEIGHT_NORMAL);
    label_ = format(text, 11.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    field_ = format(text, 11, DWRITE_FONT_WEIGHT_NORMAL);
    db_ = format(mono, 11, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    dbJp_ = format(text, 11, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    path_ = format(mono, 11, DWRITE_FONT_WEIGHT_NORMAL);
    badge_ = format(text, 11, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    button_ = format(text, 11.5f, DWRITE_FONT_WEIGHT_MEDIUM);
    icon_ = format(icons, 13, DWRITE_FONT_WEIGHT_NORMAL);
    iconLarge_ = format(icons, 15, DWRITE_FONT_WEIGHT_NORMAL);
    iconSmall_ = format(icons, 11, DWRITE_FONT_WEIGHT_NORMAL);
    chevron_ = format(icons, 9, DWRITE_FONT_WEIGHT_NORMAL);
    counter_ = format(mono, 9.5f, DWRITE_FONT_WEIGHT_BOLD);
    title_ = format(text, 14, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    sub_ = format(text, 10.5f, DWRITE_FONT_WEIGHT_NORMAL);
    name_ = format(text, 10.5f, DWRITE_FONT_WEIGHT_BOLD);
    thumbMic_ = format(icons, 8, DWRITE_FONT_WEIGHT_NORMAL);   // in a 16 DIP taskbar button
    key_ = format(mono, 10.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD);   // a shortcut in its key cap (the settings view)
    for (auto* f : {&brand_, &timer_, &chip_, &chipJp_, &hint_, &status_, &label_, &field_, &db_, &dbJp_, &path_, &badge_,
                    &button_, &icon_, &iconLarge_, &iconSmall_, &chevron_, &counter_, &title_, &sub_, &name_, &thumbMic_,
                    &key_})
        if (!*f) return false;
    Com<IDWriteInlineObject> sign;
    if (SUCCEEDED(dw_->CreateEllipsisTrimmingSign(path_.get(), sign.put()))) {
        const DWRITE_TRIMMING t{DWRITE_TRIMMING_GRANULARITY_CHARACTER, L'\\', 1};   // keep the file name visible
        path_->SetTrimming(&t, sign.get());
    }
    hint_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
    db_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
    for (auto* f : {&chip_, &chipJp_, &dbJp_, &icon_, &iconLarge_, &iconSmall_, &chevron_, &counter_, &thumbMic_, &key_})
        (*f)->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    return true;
}

// The SD guide pictures from the exe's resources, scaled once per DPI with WIC (Direct2D's own scaling of a
// render target bitmap is bilinear and would shimmer), then drawn 1:1.
void UiPainter::loadGuides(ID2D1RenderTarget* rt, UINT dpi) {
    if (guideTried_ && guideDpi_ == dpi) return;
    guide_[0].reset();
    guide_[1].reset();
    guideDpi_ = dpi;
    guideTried_ = true;
    if (!wic_) return;
    const int ids[2] = {kResGuideIdle, kResGuideRec};
    for (int k = 0; k < 2; ++k) {
        HRSRC res = FindResourceW(nullptr, MAKEINTRESOURCEW(ids[k]), MAKEINTRESOURCEW(10));   // RT_RCDATA
        HGLOBAL g = res ? LoadResource(nullptr, res) : nullptr;
        const void* data = g ? LockResource(g) : nullptr;
        const DWORD size = res ? SizeofResource(nullptr, res) : 0;
        if (!data || !size) continue;
        Com<IWICStream> st;
        Com<IWICBitmapDecoder> dec;
        Com<IWICBitmapFrameDecode> fr;
        Com<IWICFormatConverter> cv;
        Com<IWICBitmapScaler> sc;
        UINT w = 0, h = 0;
        HRESULT hr = wic_->CreateStream(st.put());
        if (SUCCEEDED(hr)) hr = st->InitializeFromMemory(static_cast<BYTE*>(const_cast<void*>(data)), size);
        if (SUCCEEDED(hr)) hr = wic_->CreateDecoderFromStream(st.get(), nullptr, WICDecodeMetadataCacheOnDemand, dec.put());
        if (SUCCEEDED(hr)) hr = dec->GetFrame(0, fr.put());
        if (SUCCEEDED(hr)) hr = fr->GetSize(&w, &h);
        if (SUCCEEDED(hr)) hr = wic_->CreateFormatConverter(cv.put());
        if (SUCCEEDED(hr))
            hr = cv->Initialize(fr.get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                                WICBitmapPaletteTypeCustom);
        const UINT ph = static_cast<UINT>(std::lround(kGuideH * static_cast<float>(dpi) / 96.f));
        const UINT pw = h ? std::max(1u, static_cast<UINT>(std::lround(static_cast<double>(w) * ph / h))) : 1;
        if (SUCCEEDED(hr)) hr = wic_->CreateBitmapScaler(sc.put());
        if (SUCCEEDED(hr)) hr = sc->Initialize(cv.get(), pw, ph, WICBitmapInterpolationModeFant);
        const D2D1_BITMAP_PROPERTIES bp = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), static_cast<float>(dpi),
            static_cast<float>(dpi));
        if (SUCCEEDED(hr)) hr = rt->CreateBitmapFromWicBitmap(sc.get(), &bp, guide_[k].put());
        if (SUCCEEDED(hr)) guideDip_[k] = guide_[k]->GetSize();
        else Log::event("ui.guide", false, -1, fmt("picture %d not loaded", ids[k]), hr);
    }
}

// ---------------------------------------------------------------- targets

void UiPainter::paint(HWND h, UINT dpi, const UiModel& m) {
    if (!d2d_) return;
    RECT rc;
    GetClientRect(h, &rc);
    if (!target_) {
        const D2D1_RENDER_TARGET_PROPERTIES p = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
            static_cast<float>(dpi), static_cast<float>(dpi));
        const D2D1_HWND_RENDER_TARGET_PROPERTIES hp = D2D1::HwndRenderTargetProperties(
            h, D2D1::SizeU(static_cast<UINT32>(rc.right - rc.left), static_cast<UINT32>(rc.bottom - rc.top)),
            D2D1_PRESENT_OPTIONS_IMMEDIATELY);
        if (FAILED(d2d_->CreateHwndRenderTarget(p, hp, target_.put()))) return;
        uiCounters().targets++;
        target_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
        guide_[0].reset();   // bitmaps belong to the target they were made for
        guide_[1].reset();
        guideTried_ = false;
    }
    dpi_ = dpi;
    loadGuides(target_.get(), dpi);
    target_->BeginDraw();
    draw(target_.get(), m);
    if (target_->EndDraw() == static_cast<HRESULT>(D2DERR_RECREATE_TARGET)) {
        dropTarget();
        InvalidateRect(h, nullptr, FALSE);
    }
}

void UiPainter::resize(UINT w, UINT h) {
    if (target_) target_->Resize(D2D1::SizeU(w, h));
}

void UiPainter::dropTarget() {
    target_.reset();
    guide_[0].reset();
    guide_[1].reset();
    guideTried_ = false;
}

HRESULT UiPainter::snapshot(const std::wstring& png, UINT dpi, const UiModel& m) {
    if (!d2d_ || !wic_) return E_FAIL;
    const UINT w = static_cast<UINT>(MulDiv(static_cast<int>(kClientW), static_cast<int>(dpi), 96));
    const UINT h = static_cast<UINT>(MulDiv(static_cast<int>(kClientH), static_cast<int>(dpi), 96));
    Com<IWICBitmap> bmp;
    Com<ID2D1RenderTarget> rt;
    HRESULT hr = wic_->CreateBitmap(w, h, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, bmp.put());
    if (SUCCEEDED(hr)) {
        const D2D1_RENDER_TARGET_PROPERTIES p = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            static_cast<float>(dpi), static_cast<float>(dpi));
        hr = d2d_->CreateWicBitmapRenderTarget(bmp.get(), p, rt.put());
    }
    if (SUCCEEDED(hr)) {
        rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        guide_[0].reset();
        guide_[1].reset();
        guideTried_ = false;
        dpi_ = dpi;
        loadGuides(rt.get(), dpi);
        rt->BeginDraw();
        draw(rt.get(), m);
        hr = rt->EndDraw();
        guide_[0].reset();   // made for this target only
        guide_[1].reset();
        guideTried_ = false;
    }
    return SUCCEEDED(hr) ? writePng(bmp.get(), png) : hr;
}

HRESULT UiPainter::writePng(IWICBitmap* bmp, const std::wstring& png) const {
    Com<IWICStream> st;
    Com<IWICBitmapEncoder> enc;
    Com<IWICBitmapFrameEncode> fr;
    HRESULT hr = wic_->CreateStream(st.put());
    if (SUCCEEDED(hr)) hr = st->InitializeFromFilename(png.c_str(), GENERIC_WRITE);
    if (SUCCEEDED(hr)) hr = wic_->CreateEncoder(GUID_ContainerFormatPng, nullptr, enc.put());
    if (SUCCEEDED(hr)) hr = enc->Initialize(st.get(), WICBitmapEncoderNoCache);
    if (SUCCEEDED(hr)) hr = enc->CreateNewFrame(fr.put(), nullptr);
    if (SUCCEEDED(hr)) hr = fr->Initialize(nullptr);
    if (SUCCEEDED(hr)) hr = fr->WriteSource(bmp, nullptr);
    if (SUCCEEDED(hr)) hr = fr->Commit();
    if (SUCCEEDED(hr)) hr = enc->Commit();
    return hr;
}

// ---------------------------------------------------------------- taskbar thumbnail buttons

// Drawn on a 16 x 16 DIP square scaled to px: the same red dot and blue microphone as the window's buttons, and
// plain media glyphs for pause, resume and stop.
HRESULT UiPainter::renderThumb(ThumbGlyph g, int px, bool light, Com<IWICBitmap>* out) {
    if (!d2d_ || !wic_ || px < 8) return E_FAIL;
    Com<IWICBitmap> bmp;
    Com<ID2D1RenderTarget> rt;
    Com<ID2D1SolidColorBrush> b;
    const float dpi = 96.f * static_cast<float>(px) / 16.f;
    HRESULT hr = wic_->CreateBitmap(static_cast<UINT>(px), static_cast<UINT>(px), GUID_WICPixelFormat32bppPBGRA,
                                    WICBitmapCacheOnLoad, bmp.put());
    if (SUCCEEDED(hr))
        hr = d2d_->CreateWicBitmapRenderTarget(
            bmp.get(),
            D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                                         D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), dpi, dpi),
            rt.put());
    if (SUCCEEDED(hr)) hr = rt->CreateSolidColorBrush(C(kText), b.put());
    if (FAILED(hr)) return hr;
    rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    rt->BeginDraw();
    rt->Clear(D2D1::ColorF(0, 0, 0, 0));
    const D2D1_POINT_2F c{8, 8};
    const UINT32 ink = light ? 0x1E2430 : 0xF2F4F8;
    auto disc = [&](UINT32 top, UINT32 bottom, UINT32 rim) {
        const D2D1_GRADIENT_STOP st[2] = {{0.f, C(top)}, {1.f, C(bottom)}};
        Com<ID2D1GradientStopCollection> sc;
        Com<ID2D1LinearGradientBrush> gb;
        if (SUCCEEDED(rt->CreateGradientStopCollection(st, 2, sc.put())) &&
            SUCCEEDED(rt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({8, 1.5f}, {8, 14.5f}), sc.get(), gb.put())))
            rt->FillEllipse(D2D1::Ellipse(c, 6.5f, 6.5f), gb.get());
        b->SetColor(C(rim));
        rt->DrawEllipse(D2D1::Ellipse(c, 6.1f, 6.1f), b.get(), 0.75f);
    };
    switch (g) {
        case ThumbGlyph::Record:
            disc(kRose, kCrimson, kRoseLight);
            b->SetColor(C(0xFFFFFF));
            rt->FillEllipse(D2D1::Ellipse(c, 2.2f, 2.2f), b.get());
            break;
        case ThumbGlyph::Audio:
            disc(kBlueHi, kBlue, kBlueText);
            b->SetColor(C(0xFFFFFF));
            text(rt.get(), b.get(), std::wstring(1, kIconMic), thumbMic_.get(), {0, 0.25f, 16, 16.25f});
            break;
        case ThumbGlyph::Pause:
            b->SetColor(C(ink));
            for (float x : {4.25f, 9.25f})
                rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x, 3.5f, x + 2.5f, 12.5f), 0.75f, 0.75f), b.get());
            break;
        case ThumbGlyph::Resume: {
            Com<ID2D1PathGeometry> tri;
            Com<ID2D1GeometrySink> sink;
            if (SUCCEEDED(d2d_->CreatePathGeometry(tri.put())) && SUCCEEDED(tri->Open(sink.put()))) {
                sink->BeginFigure({4.75f, 3.25f}, D2D1_FIGURE_BEGIN_FILLED);
                sink->AddLine({13, 8});
                sink->AddLine({4.75f, 12.75f});
                sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                sink->Close();
                b->SetColor(C(ink));
                rt->FillGeometry(tri.get(), b.get());
            }
            break;
        }
        case ThumbGlyph::Stop:
            b->SetColor(C(ink));
            rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(4, 4, 12, 12), 1.5f, 1.5f), b.get());
            break;
    }
    hr = rt->EndDraw();
    if (SUCCEEDED(hr)) *out = bmp;
    return hr;
}

HICON UiPainter::thumbIcon(ThumbGlyph g, int px, bool light) {
    Com<IWICBitmap> bmp;
    if (FAILED(renderThumb(g, px, light, &bmp))) return nullptr;
    const size_t n = static_cast<size_t>(px) * static_cast<size_t>(px);
    std::vector<uint32_t> pix(n);
    if (FAILED(bmp->CopyPixels(nullptr, static_cast<UINT>(px) * 4, static_cast<UINT>(n * 4), reinterpret_cast<BYTE*>(pix.data()))))
        return nullptr;
    BITMAPV5HEADER bi{};   // icons take straight (not premultiplied) alpha
    bi.bV5Size = sizeof(bi);
    bi.bV5Width = px;
    bi.bV5Height = -px;
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;
    void* bits = nullptr;
    HDC dc = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(dc, reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, dc);
    if (!color || !bits) {
        if (color) DeleteObject(color);
        return nullptr;
    }
    uint32_t* o = static_cast<uint32_t*>(bits);
    for (size_t i = 0; i < n; ++i) {
        const uint32_t p = pix[i], a = p >> 24;
        auto un = [a](uint32_t v) { return a ? std::min<uint32_t>(255, (v * 255 + a / 2) / a) : 0; };
        o[i] = (a << 24) | (un((p >> 16) & 0xFF) << 16) | (un((p >> 8) & 0xFF) << 8) | un(p & 0xFF);
    }
    std::vector<uint8_t> zeros(static_cast<size_t>((px + 15) / 16 * 2) * static_cast<size_t>(px), 0);
    HBITMAP mask = CreateBitmap(px, px, 1, 1, zeros.data());   // transparency comes from the alpha channel
    ICONINFO ii{TRUE, 0, 0, mask, color};
    HICON h = CreateIconIndirect(&ii);
    DeleteObject(mask);
    DeleteObject(color);
    return h;
}

HRESULT UiPainter::thumbPng(ThumbGlyph g, int px, bool light, const std::wstring& png) {
    Com<IWICBitmap> bmp;
    const HRESULT hr = renderThumb(g, px, light, &bmp);
    return SUCCEEDED(hr) ? writePng(bmp.get(), png) : hr;
}

// ---------------------------------------------------------------- text

Com<IDWriteTextLayout> UiPainter::layout(const std::wstring& s, IDWriteTextFormat* f, const D2D1_RECT_F& r) const {
    Com<IDWriteTextLayout> l;
    dw_->CreateTextLayout(s.c_str(), static_cast<UINT32>(s.size()), f, std::max(0.f, r.right - r.left),
                          std::max(0.f, r.bottom - r.top), l.put());
    return l;
}

float UiPainter::textWidth(const std::wstring& s, IDWriteTextFormat* f) const {
    Com<IDWriteTextLayout> l = layout(s, f, {0, 0, 400, 30});
    DWRITE_TEXT_METRICS tm{};
    if (l) l->GetMetrics(&tm);
    return tm.width;
}

void UiPainter::text(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, const std::wstring& s, IDWriteTextFormat* f,
                     const D2D1_RECT_F& r, float spacing, float* width) {
    Com<IDWriteTextLayout> l = layout(s, f, r);
    if (!l) return;
    if (spacing > 0 && s.size() > 1) {   // letter-spaced caps; no space after the last letter
        Com<IDWriteTextLayout1> l1;
        if (SUCCEEDED(l.as(l1))) l1->SetCharacterSpacing(0, spacing, 0, {0, static_cast<UINT32>(s.size() - 1)});
    }
    if (width) {
        DWRITE_TEXT_METRICS tm{};
        l->GetMetrics(&tm);
        *width = tm.width;
    }
    rt->DrawTextLayout({r.left, r.top}, l.get(), b, D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

// ---------------------------------------------------------------- pieces

// A flat surface with a 1 px border and, instead of a shadow, a 1 px lighter line along its top edge.
void UiPainter::panel(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, const D2D1_RECT_F& r, float radius, UINT32 fill,
                      UINT32 border, UINT32 edge) {
    const D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(inset(r, 0.5f), radius, radius);
    b->SetColor(C(fill));
    rt->FillRoundedRectangle(rr, b);
    b->SetColor(C(border));
    rt->DrawRoundedRectangle(rr, b, 1);
    if (edge) {
        b->SetColor(C(edge));
        rt->DrawLine({r.left + radius, r.top + 0.5f}, {r.right - radius, r.top + 0.5f}, b, 1);
    }
}

void UiPainter::focusRing(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, const D2D1_RECT_F& r, float radius) {
    b->SetColor(C(kCyan));
    rt->DrawRoundedRectangle(D2D1::RoundedRect(inset(r, -2.f), radius + 2, radius + 2), b, 1.5f);
}

// ---------------------------------------------------------------- scene

void UiPainter::draw(ID2D1RenderTarget* rt, const UiModel& m) {
    using P = UiModel::Phase;
    rt->SetTransform(D2D1::Matrix3x2F::Identity());
    rt->Clear(C(kCanvas));
    Com<ID2D1SolidColorBrush> b, fx;   // fx colours a range inside a text layout
    if (FAILED(rt->CreateSolidColorBrush(C(kText), b.put())) || FAILED(rt->CreateSolidColorBrush(C(kText2), fx.put())))
        return;
    const bool rec = m.phase == P::Recording;

    // header: the name (in the settings view ← and 設定), then three icon tools on the right (pin, screenshot, settings)
    b->SetColor(C(kText));
    if (m.settings) {
        drawIconButton(rt, b.get(), m, Part::Back, kIconBack, Look::Plain, 0);
        b->SetColor(C(kText));
        text(rt, b.get(), L"設定", brand_.get(), kSetTitleBox, 0.2f);
        if (rec) {   // a recording goes on meanwhile: its chip and its time beside the title
            drawChip(rt, b.get(), m, kSetChipX, kSetChipY, m.audioOnly);
            const float x = kSetChipX + (m.paused || m.audioOnly ? 66.f : 52.f) + 8;
            b->SetColor(C(m.paused ? kText3 : kText2));
            text(rt, b.get(), m.timer, path_.get(), {x, 8, x + 70, 36});
        }
    } else {
        text(rt, b.get(), L"Rec Cue!", brand_.get(), kBrandBox, 0.2f);
    }
    drawIconButton(rt, b.get(), m, Part::Pin, m.topmost ? kIconPinFill : kIconPin, m.topmost ? Look::Accent : Look::Plain, 0);
    drawIconButton(rt, b.get(), m, Part::Shot, kIconCamera, m.shotFlash ? Look::Flash : Look::Plain, rec ? m.shots : 0);
    drawIconButton(rt, b.get(), m, Part::Gear, kIconSettings, m.settings ? Look::Accent : Look::Plain, 0);

    if (m.settings) {
        drawSettings(rt, b.get(), m);
    } else {
        drawDeck(rt, b.get(), m);
        drawScreen(rt, b.get(), fx.get(), m);
        drawChannel(rt, b.get(), fx.get(), m, Part::Mic, m.mic, L"マイク", kIconMic, kMicY);
        drawChannel(rt, b.get(), fx.get(), m, Part::Sys, m.sys, L"PC の音声", kIconSpeakers, kSysY);
        drawPath(rt, b.get(), fx.get(), m);

        const bool checking = m.phase == P::Checking;
        drawButton(rt, b.get(), m, Part::Check, checking ? kIconStop : kIconPulse, checking ? L"確認を止める" : L"音量を確認",
                   checking);
        drawButton(rt, b.get(), m, Part::Open, kIconFolderOpen, L"保存先を開く", false);
    }
    drawComment(rt, b.get(), m);
    drawGuide(rt, m);

    if (rec) {   // everyone in the room can see that it records: a 2 px band across the top (amber while paused)
        b->SetColor(C(m.paused ? kAmber : m.audioOnly ? kBlue : kCrimson));
        rt->FillRectangle(D2D1::RectF(0, 0, kClientW, 2), b.get());
    }
}

void UiPainter::drawIconButton(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, const UiModel& m, Part p, wchar_t icon,
                               Look look, int counter) {
    const D2D1_RECT_F r = partRect(p);
    const bool en = partEnabled(m.phase, m.audioOnly, p), hot = en && m.hot == p, down = hot && m.pressed == p;
    UINT32 fill = kControl, border = kControlBorder, edge = kEdge, ic = kFieldText;
    if (!en) {
        fill = kOffFill, border = kOffBorder, edge = 0, ic = kOffText;
    } else if (look == Look::Accent) {
        fill = kPinOnFill, border = hot ? kCyan : kPinOnBorder, edge = kPinOnEdge, ic = kCyanText;
    } else if (look == Look::Flash) {
        fill = kFlashFill, border = kCyan, edge = 0, ic = kCyan;
    } else if (down) {
        fill = kControlDown, edge = 0;
    } else if (hot) {
        fill = kControlHot, border = kControlBorderHot, ic = kText;
    }
    panel(rt, b, r, 5, fill, border, edge);
    const float dy = down ? 1.f : 0.f;
    b->SetColor(C(ic));
    text(rt, b, std::wstring(1, icon), icon_.get(), {r.left, r.top + dy, r.right, r.bottom + dy});
    if (counter > 0) {   // screenshots taken in this recording, on the button's corner
        const D2D1_POINT_2F c{r.right - 2, r.top + 2};
        b->SetColor(C(kCanvas));
        rt->FillEllipse(D2D1::Ellipse(c, 9, 9), b);
        b->SetColor(C(kCyanDeep));
        rt->FillEllipse(D2D1::Ellipse(c, 8, 8), b);
        b->SetColor(C(0xFFFFFF));
        text(rt, b, counter > 99 ? L"99" : std::to_wstring(counter), counter_.get(), {c.x - 8, c.y - 8, c.x + 8, c.y + 8});
    }
    if (m.showFocus && m.focus == p) focusRing(rt, b, r, 5);
}

// tint: 0 plain, 1 recording (crimson), 2 audio (blue), 3 needs attention (amber)
void UiPainter::deckPanel(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, const D2D1_RECT_F& r, int tint) {
    static const UINT32 kTints[4][4] = {{kHeroTop, kHeroBottom, kHeroBorder, kHeroEdge},
                                        {kRecHeroTop, kRecHeroBottom, kRecHeroBorder, kRecHeroEdge},
                                        {kAudHeroTop, kAudHeroBottom, kAudHeroBorder, kAudHeroEdge},
                                        {kWarnHeroTop, kWarnHeroBottom, kWarnHeroBorder, kWarnHeroEdge}};
    const UINT32* t = kTints[std::clamp(tint, 0, 3)];
    const D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(inset(r, 0.5f), 8, 8);
    const D2D1_GRADIENT_STOP st[2] = {{0.f, C(t[0])}, {1.f, C(t[1])}};
    Com<ID2D1GradientStopCollection> sc;
    Com<ID2D1LinearGradientBrush> g;
    if (SUCCEEDED(rt->CreateGradientStopCollection(st, 2, sc.put())) &&
        SUCCEEDED(rt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({0, r.top}, {0, r.bottom}), sc.get(),
                                                g.put())))
        rt->FillRoundedRectangle(rr, g.get());
    b->SetColor(C(t[2]));
    rt->DrawRoundedRectangle(rr, b, 1);
    if (r.right - r.left > 20) {
        b->SetColor(C(t[3]));
        rt->DrawLine({r.left + 8, r.top + 0.5f}, {r.right - 8, r.top + 0.5f}, b, 1);
    }
}

// The deck in two halves: recording (screen + sound) on the left, audio only on the right. The side that records
// opens wide (m.split 1 / -1) and shows the time; when nothing records each half shows what it does.
void UiPainter::drawDeck(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, const UiModel& m) {
    using P = UiModel::Phase;
    const float s = std::clamp(m.split, -1.f, 1.f);
    const float edge = std::round(s >= 0 ? kDeckMid + (kEdgeRec - kDeckMid) * s : kDeckMid + (kEdgeAud - kDeckMid) * -s);
    const D2D1_RECT_F left{kL, kDeckTop, edge, kDeckBottom};
    const D2D1_RECT_F right{edge + kDeckGap, kDeckTop, kR, kDeckBottom};
    const bool rec = m.phase == P::Recording;
    const bool live = rec || m.phase == P::Starting || m.phase == P::Saving;   // a recording is starting, made or saved
    const bool screenSide = live && !m.audioOnly, audioSide = live && m.audioOnly;
    // ❚❚ and the room it takes once the recording runs (while it starts the deck has not opened yet)
    const bool started = rec || m.phase == P::Saving;
    const UINT32 timeColor = m.phase == P::Starting ? kIcon : m.paused ? kText3 : kTimerLive;
    deckPanel(rt, b, left, rec && !m.audioOnly && !m.paused ? (m.alert ? 3 : 1) : 0);
    deckPanel(rt, b, right, rec && m.audioOnly && !m.paused ? (m.alert ? 3 : 2) : 0);

    // recording side: stop and pause on the left; the time, REC and status (or what it records) to their right
    rt->PushAxisAlignedClip(inset(left, 1), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    drawRecord(rt, b, m);
    if (screenSide) {
        const D2D1_RECT_F tb = started ? kRecTimerBox : kTimerBox;
        if (started) drawPause(rt, b, m, Part::Pause);
        Com<IDWriteTextLayout> l = layout(m.timer, timer_.get(), tb);
        if (l) {
            b->SetColor(C(timeColor));
            rt->DrawTextLayout({tb.left, tb.top}, l.get(), b, D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
        drawChip(rt, b, m, started ? kRecChipX : kChipX, kChipY, false);
        drawHint(rt, b, m, edge - 10);
        drawStatus(rt, b, m, {tb.left, kStatusTop, edge - 8, kStatusBottom});
    } else {
        drawSideLabel(rt, b, {kTimerBox.left, kDeckTop, std::max(kTimerBox.left, edge - 10), kDeckBottom}, L"録画",
                      L"画面・マイク・PC の音声", m.recHotkey, false);
    }
    rt->PopAxisAlignedClip();

    // audio side: stop on the right with pause beside it; the time and status (or what it records) to their left
    rt->PushAxisAlignedClip(inset(right, 1), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    drawAudio(rt, b, m);
    if (audioSide) {
        const float tr = started ? kAudTimeRightLive : kAudTimeRight;
        if (started) drawPause(rt, b, m, Part::AudioPause);
        Com<IDWriteTextLayout> l = layout(m.timer, timer_.get(), {tr - 200, kTimerBox.top, tr, kTimerBox.bottom});
        float tw = 0;
        if (l) {
            l->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
            DWRITE_TEXT_METRICS tm{};
            l->GetMetrics(&tm);
            tw = tm.width;
            b->SetColor(C(timeColor));
            rt->DrawTextLayout({tr - 200, kTimerBox.top}, l.get(), b, D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
        drawChip(rt, b, m, tr - tw - 10 - 66, kChipY, true);
        drawStatus(rt, b, m, {std::max(right.left + 12, kEdgeAud + kDeckGap + 14), kStatusTop, tr + 4, kStatusBottom});
    } else if (!screenSide || s < 0.99f) {
        drawSideLabel(rt, b, {std::min(kAudTimeRight, right.left + 10), kDeckTop, kAudTimeRight, kDeckBottom}, L"録音",
                      L"マイクと PC の音声", m.audioHotkey, true);
        if (partEnabled(m.phase, m.audioOnly, Part::AudioFormat)) drawFormatChip(rt, b, m);
    }
    rt->PopAxisAlignedClip();
}

// MP3 ▾ left of 録音: the file a recording of sound alone makes; a click opens the choice (MP3 / WAV / M4A).
void UiPainter::drawFormatChip(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, const UiModel& m) {
    const D2D1_RECT_F r = kFormatBox;
    const bool hot = m.hot == Part::AudioFormat, down = hot && m.pressed == Part::AudioFormat;
    panel(rt, b, r, 4, down ? kControlDown : hot ? kControlHot : kBlueChipBg, hot ? kBlueHi : kAudHeroBorder, 0);
    const float dy = down ? 1.f : 0.f;
    b->SetColor(C(hot ? kBluePale : kBlueText));
    text(rt, b, m.audioFormat, chip_.get(), {r.left + 3, r.top + dy, r.right - 13, r.bottom + dy});
    b->SetColor(C(hot ? kText2 : kText3));
    text(rt, b, std::wstring(1, kIconChevron), chevron_.get(), {r.right - 15, r.top + dy, r.right - 3, r.bottom + dy});
    if (m.showFocus && m.focus == Part::AudioFormat) focusRing(rt, b, r, 4);
}

// What a half does while it waits: its name, what it records, and its shortcut.
void UiPainter::drawSideLabel(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, const D2D1_RECT_F& r, const wchar_t* title,
                              const wchar_t* sub, const std::wstring& key, bool trailing) {
    if (r.right - r.left < 20) return;
    const DWRITE_TEXT_ALIGNMENT a = trailing ? DWRITE_TEXT_ALIGNMENT_TRAILING : DWRITE_TEXT_ALIGNMENT_LEADING;
    auto line = [&](const std::wstring& s, IDWriteTextFormat* f, float top, float bottom, UINT32 color) {
        Com<IDWriteTextLayout> l = layout(s, f, {r.left, top, r.right, bottom});
        if (!l) return;
        l->SetTextAlignment(a);
        b->SetColor(C(color));
        rt->DrawTextLayout({r.left, top}, l.get(), b, D2D1_DRAW_TEXT_OPTIONS_CLIP);
    };
    line(title, title_.get(), r.top + 8, r.top + 28, kText);
    line(sub, sub_.get(), r.top + 28, r.top + 44, kText2);
    if (!key.empty()) line(key, hint_.get(), r.top + 46, r.top + 62, kHint);
}

void UiPainter::drawRecord(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, const UiModel& m) {
    using P = UiModel::Phase;
    const bool on = m.phase == P::Recording && !m.audioOnly;
    const bool busy = (m.phase == P::Starting || m.phase == P::Saving) && !m.audioOnly;
    const bool en = partEnabled(m.phase, m.audioOnly, Part::Record);
    const bool hot = en && m.hot == Part::Record, down = hot && m.pressed == Part::Record;
    b->SetColor(C(on ? kRecOuter : kIdleOuter));
    rt->FillEllipse(D2D1::Ellipse(kRec, kRecR, kRecR), b);
    b->SetColor(C(on ? kCrimson : hot ? kControlBorderHot : kIdleRing));
    rt->DrawEllipse(D2D1::Ellipse(kRec, kRecR - 1, kRecR - 1), b, 2);

    if (busy || (!en && !on)) {   // preparing / saving, or waiting while audio records
        b->SetColor(C(kBusy));
        rt->FillEllipse(D2D1::Ellipse(kRec, 17, 17), b);
        if (m.phase == P::Saving && !m.audioOnly) {
            b->SetColor(C(kText, 0.45f));
            rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(kRec.x - 6, kRec.y - 6, kRec.x + 6, kRec.y + 6), 2, 2), b);
        }
    } else if (on) {   // stop
        const float s = down ? 9.f : 10.f;
        const D2D1_ROUNDED_RECT sq = D2D1::RoundedRect(D2D1::RectF(kRec.x - s, kRec.y - s, kRec.x + s, kRec.y + s), 4, 4);
        b->SetColor(C(kRose));
        rt->FillRoundedRectangle(sq, b);
        b->SetColor(C(kRosePale));
        rt->DrawRoundedRectangle(sq, b, 1);
    } else {   // record: red dot with a white centre
        const float r = down ? 18.f : 19.f;
        const D2D1_GRADIENT_STOP st[2] = {{0.f, C(hot ? kRoseLight : kRose)}, {1.f, C(hot ? kRose : kCrimson)}};
        Com<ID2D1GradientStopCollection> sc;
        Com<ID2D1LinearGradientBrush> g;
        if (SUCCEEDED(rt->CreateGradientStopCollection(st, 2, sc.put())) &&
            SUCCEEDED(rt->CreateLinearGradientBrush(
                D2D1::LinearGradientBrushProperties({kRec.x, kRec.y - r}, {kRec.x, kRec.y + r}), sc.get(), g.put())))
            rt->FillEllipse(D2D1::Ellipse(kRec, r, r), g.get());
        b->SetColor(C(kRoseLight));
        rt->DrawEllipse(D2D1::Ellipse(kRec, r - 0.5f, r - 0.5f), b, 1);
        b->SetColor(C(0xFFFFFF));
        rt->FillEllipse(D2D1::Ellipse(kRec, 6, 6), b);
    }
    if (m.showFocus && m.focus == Part::Record) {
        b->SetColor(C(kCyan));
        rt->DrawEllipse(D2D1::Ellipse(kRec, kRecR + 3, kRecR + 3), b, 1.5f);
    }
}

void UiPainter::drawAudio(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, const UiModel& m) {
    using P = UiModel::Phase;
    const bool on = m.phase == P::Recording && m.audioOnly;
    const bool busy = (m.phase == P::Starting || m.phase == P::Saving) && m.audioOnly;
    const bool en = partEnabled(m.phase, m.audioOnly, Part::Audio);
    const bool hot = en && m.hot == Part::Audio, down = hot && m.pressed == Part::Audio;
    b->SetColor(C(on ? kBlueOuter : kIdleOuter));
    rt->FillEllipse(D2D1::Ellipse(kAud, kAudR, kAudR), b);
    b->SetColor(C(on ? kBlue : hot ? kControlBorderHot : kIdleRing));
    rt->DrawEllipse(D2D1::Ellipse(kAud, kAudR - 1, kAudR - 1), b, 2);

    const D2D1_RECT_F glyph{kAud.x - 12, kAud.y - 12 + (down ? 1.f : 0.f), kAud.x + 12, kAud.y + 12 + (down ? 1.f : 0.f)};
    if (busy || (!en && !on)) {   // preparing / saving, or waiting while the screen records
        b->SetColor(C(kBusy));
        rt->FillEllipse(D2D1::Ellipse(kAud, 15, 15), b);
        b->SetColor(C(kText3));
        text(rt, b, std::wstring(1, kIconMic), iconLarge_.get(), glyph);
    } else if (on) {   // stop
        const float s = down ? 7.5f : 8.5f;
        const D2D1_ROUNDED_RECT sq = D2D1::RoundedRect(D2D1::RectF(kAud.x - s, kAud.y - s, kAud.x + s, kAud.y + s), 3, 3);
        b->SetColor(C(kBlueHi));
        rt->FillRoundedRectangle(sq, b);
        b->SetColor(C(kBluePale));
        rt->DrawRoundedRectangle(sq, b, 1);
    } else {   // record audio: blue dot with a white microphone
        const float r = down ? 15.f : 16.f;
        const D2D1_GRADIENT_STOP st[2] = {{0.f, C(hot ? kBlueText : kBlueHi)}, {1.f, C(hot ? kBlueHi : kBlue)}};
        Com<ID2D1GradientStopCollection> sc;
        Com<ID2D1LinearGradientBrush> g;
        if (SUCCEEDED(rt->CreateGradientStopCollection(st, 2, sc.put())) &&
            SUCCEEDED(rt->CreateLinearGradientBrush(
                D2D1::LinearGradientBrushProperties({kAud.x, kAud.y - r}, {kAud.x, kAud.y + r}), sc.get(), g.put())))
            rt->FillEllipse(D2D1::Ellipse(kAud, r, r), g.get());
        b->SetColor(C(kBlueText));
        rt->DrawEllipse(D2D1::Ellipse(kAud, r - 0.5f, r - 0.5f), b, 1);
        b->SetColor(C(0xFFFFFF));
        text(rt, b, std::wstring(1, kIconMic), iconLarge_.get(), glyph);
    }
    if (m.showFocus && m.focus == Part::Audio) {
        b->SetColor(C(kCyan));
        rt->DrawEllipse(D2D1::Ellipse(kAud, kAudR + 3, kAudR + 3), b, 1.5f);
    }
}

// ❚❚ beside the stop button while recording; ▶ in amber while paused. It stays (greyed) while the file is saved.
void UiPainter::drawPause(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, const UiModel& m, Part p) {
    const bool audio = p == Part::AudioPause;
    const D2D1_POINT_2F c = audio ? kAudPause : kPause;
    const float r = audio ? kAudPauseR : kPauseR, k = r / kPauseR;
    const bool en = partEnabled(m.phase, m.audioOnly, p), hot = en && m.hot == p, down = hot && m.pressed == p;
    const bool paused = en && m.paused;
    b->SetColor(C(paused ? kWarnBg : kIdleOuter));
    rt->FillEllipse(D2D1::Ellipse(c, r, r), b);
    b->SetColor(C(!en ? kOffBorder : paused ? kAmber : hot ? kControlBorderHot : kIdleRing));
    rt->DrawEllipse(D2D1::Ellipse(c, r - 1, r - 1), b, 2);
    const float dy = down ? 1.f : 0.f;
    if (paused) {   // resume
        Com<ID2D1PathGeometry> tri;
        Com<ID2D1GeometrySink> sink;
        if (SUCCEEDED(d2d_->CreatePathGeometry(tri.put())) && SUCCEEDED(tri->Open(sink.put()))) {
            sink->BeginFigure({c.x - 4.5f * k, c.y - 6.5f * k + dy}, D2D1_FIGURE_BEGIN_FILLED);
            sink->AddLine({c.x + 6.5f * k, c.y + dy});
            sink->AddLine({c.x - 4.5f * k, c.y + 6.5f * k + dy});
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            sink->Close();
            b->SetColor(C(hot ? 0xFFFFFF : kAmberText));
            rt->FillGeometry(tri.get(), b);
        }
    } else {        // pause
        const float w = 3.5f * k, h = 6 * k, g = 1.75f * k;
        b->SetColor(C(!en ? kOffText : hot ? kText : kLabel));
        for (float x : {c.x - g - w, c.x + g})
            rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x, c.y - h + dy, x + w, c.y + h + dy), 1, 1), b);
    }
    if (m.showFocus && m.focus == p) {
        b->SetColor(C(kCyan));
        rt->DrawEllipse(D2D1::Ellipse(c, r + 3, r + 3), b, 1.5f);
    }
}

// REC (screen, crimson) / AUDIO (blue) with a breathing dot while recording, 確認中 while checking the levels.
void UiPainter::drawChip(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, const UiModel& m, float x, float y, bool audio) {
    using P = UiModel::Phase;
    if (m.phase == P::Checking && !audio) {
        const D2D1_RECT_F r{x, y, x + 56, y + kChipH};
        b->SetColor(C(kCyan, 0.8f));
        rt->DrawRoundedRectangle(D2D1::RoundedRect(inset(r, 0.5f), 4, 4), b, 1);
        b->SetColor(C(kCyanText));
        text(rt, b, L"確認中", chipJp_.get(), r);
        return;
    }
    if (m.phase != P::Recording) return;
    if (m.paused) {   // PAUSE with two breathing bars, in amber
        const D2D1_RECT_F r{x, y, x + 66, y + kChipH};
        const float cy = (r.top + r.bottom) / 2;
        panel(rt, b, r, 4, kWarnBg, kWarnBorder, 0);
        b->SetColor(C(kAmber, 0.35f + 0.65f * m.pulse));
        rt->FillRectangle(D2D1::RectF(r.left + 7, cy - 3.5f, r.left + 9, cy + 3.5f), b);
        rt->FillRectangle(D2D1::RectF(r.left + 11, cy - 3.5f, r.left + 13, cy + 3.5f), b);
        b->SetColor(C(kAmberText));
        text(rt, b, L"PAUSE", chip_.get(), {r.left + 15, r.top, r.right - 3, r.bottom}, 0.8f);
        return;
    }
    const std::wstring s = audio ? L"AUDIO" : L"REC";
    const D2D1_RECT_F r{x, y, x + (audio ? 66.f : 52.f), y + kChipH};
    panel(rt, b, r, 4, audio ? kBlueChipBg : kRecChipBg, audio ? kBlue : kCrimson, 0);
    b->SetColor(C(audio ? kBlueHi : kRose, 0.35f + 0.65f * m.pulse));
    rt->FillEllipse(D2D1::Ellipse({r.left + 11, (r.top + r.bottom) / 2}, 3, 3), b);
    b->SetColor(C(audio ? kBluePale : kRosePale));
    text(rt, b, s, chip_.get(), {r.left + 15, r.top, r.right - 3, r.bottom}, 0.8f);
}

// The shortcut that matters now, small at the right of the recording side: record while idle, screenshot while
// recording the screen.
void UiPainter::drawHint(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, const UiModel& m, float right) {
    using P = UiModel::Phase;
    const bool rec = m.phase == P::Recording;
    const std::wstring& s = rec ? m.shotHotkey : m.phase == P::Idle ? m.recHotkey : std::wstring();
    if (s.empty()) return;
    const D2D1_RECT_F box{right - 160, 54, right, 72};
    const float tw = textWidth(s, hint_.get());
    const float clear = kRecChipX + (m.paused ? 66.f : 52.f) + 6;   // right of the REC / PAUSE chip
    if (rec && box.right - tw < clear) return;   // a long shortcut does not fit beside ❚❚, the time and the chip
    b->SetColor(C(kHint));
    text(rt, b, s, hint_.get(), box);
    const float x = box.right - tw - 6;
    const float cy = (box.top + box.bottom) / 2;
    if (rec) {
        if (x - 14 >= clear)   // the camera only when it clears the chip too
            text(rt, b, std::wstring(1, kIconCamera), iconSmall_.get(), {x - 14, box.top, x, box.bottom});
    } else {
        b->SetColor(C(kRose, 0.85f));
        rt->FillEllipse(D2D1::Ellipse({x - 5, cy}, 3, 3), b);
    }
}

void UiPainter::drawStatus(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, const UiModel& m, const D2D1_RECT_F& r) {
    using T = UiModel::Tone;
    if (m.tone == T::Normal) {
        b->SetColor(C(kText2));
        text(rt, b, m.status, status_.get(), r);
        return;
    }
    UINT32 fill = kToastBg, border = kToastBorder, fg = kCyanText;
    wchar_t icon = kIconCheck;
    if (m.tone == T::Warn) fill = kWarnBg, border = kWarnBorder, fg = kAmberText, icon = kIconWarn;
    if (m.tone == T::Bad) fill = kRecChipBg, border = kCrimson, fg = kRosePale, icon = kIconWarn;
    panel(rt, b, r, 4, fill, border, 0);
    b->SetColor(C(fg));
    text(rt, b, std::wstring(1, icon), iconSmall_.get(), {r.left + 6, r.top, r.left + 20, r.bottom});
    Com<IDWriteTextLayout> l = layout(m.status, status_.get(), {r.left + 24, r.top, r.right - 7, r.bottom});
    if (!l) return;
    l->SetFontWeight(DWRITE_FONT_WEIGHT_MEDIUM, {0, static_cast<UINT32>(m.status.size())});
    if (m.tone == T::Warn) l->SetFontSize(11, {0, static_cast<UINT32>(m.status.size())});
    rt->DrawTextLayout({r.left + 24, r.top}, l.get(), b, D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void UiPainter::drawChannel(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, ID2D1SolidColorBrush* fx, const UiModel& m,
                            Part part, const ChannelView& c, const wchar_t* label, wchar_t icon, float y) {
    const D2D1_RECT_F f = partRect(part);
    const bool en = partEnabled(m.phase, m.audioOnly, part), hot = en && m.hot == part, down = hot && m.pressed == part;
    const bool open = !c.device.empty();
    b->SetColor(C(c.warn ? kAmber : open && c.active ? kCyan : kIcon));
    text(rt, b, std::wstring(1, icon), icon_.get(), {kL, y, kL + 18, y + kRowH});
    b->SetColor(C(c.warn ? kAmber : kLabel));
    text(rt, b, label, label_.get(), {kL + 20, y, kFieldL - 2, y + kRowH});

    panel(rt, b, f, 5, down ? kControlDown : hot ? kControlHot : kInput,
          c.warn ? kWarnBorder : hot ? kControlBorderHot : kInputBorder, c.warn ? 0 : kInputEdge);
    // the setting while idle; "自動 → device" (the device actually recorded) while a device is open
    const D2D1_RECT_F tr{f.left + 8, f.top, f.right - 22, f.bottom};
    std::wstring s = c.choice;
    UINT32 lead = 0;
    if (open) {
        s = c.mode.empty() ? c.device : c.mode + L" → " + c.device;
        lead = c.mode.empty() ? 0 : static_cast<UINT32>(c.mode.size() + 3);
    }
    Com<IDWriteTextLayout> l = layout(s, field_.get(), tr);
    if (l) {
        if (open) {
            fx->SetColor(C(c.warn ? kAmberText : kCyanText));
            const DWRITE_TEXT_RANGE dev{lead, static_cast<UINT32>(s.size()) - lead};
            l->SetDrawingEffect(fx, dev);
            l->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD, dev);
        }
        b->SetColor(C(en || open ? kFieldText : kText3));
        rt->DrawTextLayout({tr.left, tr.top}, l.get(), b, D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
    b->SetColor(C(en ? (hot ? kText2 : kText3) : kOffText));
    text(rt, b, std::wstring(1, kIconChevron), chevron_.get(), {f.right - 20, f.top, f.right - 6, f.bottom});
    if (m.showFocus && m.focus == part) focusRing(rt, b, f, 5);

    drawMeter(rt, b, c, y);
    const D2D1_RECT_F d{kDbL, y, kR, y + kRowH};
    if (c.warn && c.active && c.db <= -89) {   // silent for a long time: amber badge
        panel(rt, b, {d.left + 4, d.top + 3, d.right, d.bottom - 3}, 4, kWarnBg, kSilentBorder, 0);
        b->SetColor(C(kAmber));
        text(rt, b, L"無音", dbJp_.get(), {d.left + 4, d.top, d.right, d.bottom});
    } else {
        const std::wstring t = dbText(c);
        b->SetColor(C(c.active ? kDbLive : kHint));
        text(rt, b, t, c.active && c.db <= -89 ? dbJp_.get() : db_.get(), d);
    }
}

void UiPainter::drawMeter(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, const ChannelView& c, float y) {
    panel(rt, b, {kMeterL, y, kMeterR, y + kRowH}, 5, kMeterBox, c.warn ? kSilentBorder : kMeterBorder, 0);
    const float tl = kMeterL + 8, tw = 96, tt = y + 8, tb = tt + 6;
    b->SetColor(C(kMeterTrack));
    rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(tl, tt, tl + tw, tb), 3, 3), b);
    if (c.active) {
        const float w = tw * static_cast<float>(std::clamp((c.db + 60) / 60, 0.0, 1.0));
        if (w > 0.5f) {
            const D2D1_GRADIENT_STOP st[3] = {{0.f, C(kCyanDeep)}, {0.78f, C(kCyan)}, {1.f, C(kTeal)}};
            Com<ID2D1GradientStopCollection> sc;
            Com<ID2D1LinearGradientBrush> g;
            if (SUCCEEDED(rt->CreateGradientStopCollection(st, 3, sc.put())) &&
                SUCCEEDED(rt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({tl, 0}, {tl + tw, 0}), sc.get(),
                                                        g.put())))
                rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(tl, tt, tl + w, tb), 2, 2), g.get());
        }
        if (c.hold > c.db && c.hold > -60) {   // peak hold
            const float x = tl + tw * static_cast<float>(std::clamp((c.hold + 60) / 60, 0.0, 1.0));
            b->SetColor(C(kPeak));
            rt->FillRectangle(D2D1::RectF(std::min(x, tl + tw) - 2, tt, std::min(x, tl + tw), tb), b);
        }
    }
    b->SetColor(C(kTick));   // scale: every 10 dB from -60 to 0
    for (int i = 0; i <= 6; ++i) {
        const float x = tl + std::min(tw - 1, static_cast<float>(i) * tw / 6);
        rt->FillRectangle(D2D1::RectF(x, y + 17, x + 1, y + 19), b);
    }
}

// The monitor to record, in a field like the devices (the setting while idle, in cyan while it is recorded), and
// beside it a small map of the desktop with that monitor lit: the numbers in its menu are the map's numbers.
void UiPainter::drawScreen(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, ID2D1SolidColorBrush* fx, const UiModel& m) {
    const float y = kScreenY;
    const D2D1_RECT_F f = partRect(Part::Screen);
    const bool en = partEnabled(m.phase, m.audioOnly, Part::Screen), hot = en && m.hot == Part::Screen,
               down = hot && m.pressed == Part::Screen;
    const bool live = m.screen.active;
    b->SetColor(C(live ? kCyan : kIcon));
    text(rt, b, std::wstring(1, kIconMonitor), icon_.get(), {kL, y, kL + 18, y + kRowH});
    b->SetColor(C(kLabel));
    text(rt, b, L"画面", label_.get(), {kL + 20, y, kFieldL - 2, y + kRowH});
    panel(rt, b, f, 5, down ? kControlDown : hot ? kControlHot : kInput, hot ? kControlBorderHot : kInputBorder, kInputEdge);
    const D2D1_RECT_F tr{f.left + 8, f.top, f.right - 22, f.bottom};
    Com<IDWriteTextLayout> l = layout(m.screen.choice, field_.get(), tr);
    if (l) {
        if (live) {
            const DWRITE_TEXT_RANGE all{0, static_cast<UINT32>(m.screen.choice.size())};
            fx->SetColor(C(kCyanText));
            l->SetDrawingEffect(fx, all);
            l->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD, all);
        }
        b->SetColor(C(en || live ? kFieldText : kText3));
        rt->DrawTextLayout({tr.left, tr.top}, l.get(), b, D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
    b->SetColor(C(en ? (hot ? kText2 : kText3) : kOffText));
    text(rt, b, std::wstring(1, kIconChevron), chevron_.get(), {f.right - 20, f.top, f.right - 6, f.bottom});
    if (m.showFocus && m.focus == Part::Screen) focusRing(rt, b, f, 5);
    drawScreenMap(rt, b, m.screen, {kMeterL, y, kR, y + kRowH});
}

void UiPainter::drawScreenMap(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, const ScreenView& s, const D2D1_RECT_F& box) {
    panel(rt, b, box, 5, kMeterBox, kMeterBorder, 0);
    if (s.monitors.empty()) return;
    RECT u = s.monitors[0];
    for (const RECT& r : s.monitors) {
        u.left = std::min(u.left, r.left);
        u.top = std::min(u.top, r.top);
        u.right = std::max(u.right, r.right);
        u.bottom = std::max(u.bottom, r.bottom);
    }
    const float uw = static_cast<float>(std::max(1L, u.right - u.left)), uh = static_cast<float>(std::max(1L, u.bottom - u.top));
    const float k = std::min((box.right - box.left - 16) / uw, (box.bottom - box.top - 5) / uh);
    const float ox = std::round((box.left + box.right) / 2 - uw * k / 2), oy = std::round((box.top + box.bottom) / 2 - uh * k / 2);
    for (size_t i = 0; i < s.monitors.size(); ++i) {
        const RECT& r = s.monitors[i];
        const D2D1_RECT_F q{ox + static_cast<float>(r.left - u.left) * k + 0.75f, oy + static_cast<float>(r.top - u.top) * k + 0.75f,
                            ox + static_cast<float>(r.right - u.left) * k - 0.75f,
                            oy + static_cast<float>(r.bottom - u.top) * k - 0.75f};
        const bool sel = static_cast<int>(i) == s.selected;
        const D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(q, 1.5f, 1.5f);
        b->SetColor(C(sel ? (s.active ? kRecChipBg : kPinOnFill) : kControl));
        rt->FillRoundedRectangle(rr, b);
        b->SetColor(C(sel ? (s.active ? kCrimson : kCyan) : kControlBorderHot));
        rt->DrawRoundedRectangle(rr, b, 1);
        if (q.right - q.left >= 9 && q.bottom - q.top >= 9) {
            b->SetColor(C(sel ? (s.active ? kRosePale : kCyanText) : kText3));
            text(rt, b, std::to_wstring(i + 1), counter_.get(), q);
        }
    }
}

void UiPainter::drawPath(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, ID2D1SolidColorBrush* fx, const UiModel& m) {
    using P = UiModel::Phase;
    const D2D1_RECT_F r = kPathBox;
    panel(rt, b, r, 5, kWell, kWellBorder, 0);
    const bool live = m.phase == P::Recording;
    const UINT32 tone = live ? (m.audioOnly ? kBlueText : kRosePale) : kIcon;
    b->SetColor(C(tone));
    text(rt, b, std::wstring(1, kIconFolder), iconSmall_.get(), {r.left + 7, r.top, r.left + 21, r.bottom});
    float bw = 0;
    text(rt, b, m.pathLabel, badge_.get(), {r.left + 26, r.top, r.left + 90, r.bottom}, 0, &bw);
    const D2D1_RECT_F pr{r.left + 26 + bw + 7, r.top, r.right - 8, r.bottom};
    Com<IDWriteTextLayout> l = layout(m.path, path_.get(), pr);
    if (!l) return;
    const size_t k = m.path.find_last_of(L'\\');
    if (hasExtension(m.path) && k != std::wstring::npos) {   // a file: make its name stand out
        const DWRITE_TEXT_RANGE name{static_cast<UINT32>(k + 1), static_cast<UINT32>(m.path.size() - k - 1)};
        fx->SetColor(C(kPathFile));
        l->SetDrawingEffect(fx, name);
        l->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD, name);
    }
    b->SetColor(C(kPath));
    rt->DrawTextLayout({pr.left, pr.top}, l.get(), b, D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

// icon 0: the label alone
void UiPainter::drawButton(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, const UiModel& m, Part p, wchar_t icon,
                           const std::wstring& label, bool toggled) {
    const D2D1_RECT_F r = partRect(p);
    const bool en = usable(m, p), hot = en && m.hot == p, down = hot && m.pressed == p;
    UINT32 fill = kControl, border = kControlBorder, edge = kEdge, fg = kLabel, ic = kLabel;
    if (!en) fill = kOffFill, border = kOffBorder, edge = 0, fg = ic = kOffText;
    else if (toggled) fill = kPinOnFill, border = hot ? kCyan : kPinOnBorder, edge = kPinOnEdge, fg = ic = kCyanText;
    else if (down) fill = kControlDown, edge = 0;
    else if (hot) fill = kControlHot, border = kControlBorderHot;
    panel(rt, b, r, 5, fill, border, edge);
    const float tw = textWidth(label, button_.get());
    const float iw = icon ? 13.f : 0.f, gap = icon ? 6.f : 0.f, dy = down ? 1.f : 0.f;
    const float x = std::round((r.left + r.right) / 2 - (iw + gap + tw) / 2);
    b->SetColor(C(ic));
    if (icon) text(rt, b, std::wstring(1, icon), icon_.get(), {x, r.top + dy, x + iw, r.bottom + dy});
    b->SetColor(C(fg));
    text(rt, b, label, button_.get(), {x + iw + gap, r.top + dy, r.right - 4, r.bottom + dy});
    if (m.showFocus && m.focus == p) focusRing(rt, b, r, 5);
}

// The settings view (the gear): the save folders, the shortcuts, and the log and the links, each under a heading,
// in the main view's own fields and buttons.
void UiPainter::drawSettings(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, const UiModel& m) {
    drawSection(rt, b, L"保存先", kSecDirs);
    drawDirRow(rt, b, m, Part::OutDir, Part::OutChange, kIconVideo, L"録画・録音", m.outDir, L"", kOutY);
    drawDirRow(rt, b, m, Part::ShotDir, Part::ShotChange, kIconCamera, L"スクショ", m.shotDir, L"録画と同じ場所", kShotDirY);
    drawSection(rt, b, L"ショートカットキー", kSecKeys);
    drawKeys(rt, b, m, kKeysY);
    drawSection(rt, b, L"ログと情報", kSecLog);
    drawButton(rt, b, m, Part::Logs, kIconDownload, m.logsBusy ? L"まとめています…" : L"ログを取得", false);
    drawButton(rt, b, m, Part::Github, kIconOpenIn, L"GitHub", false);
    drawButton(rt, b, m, Part::Site, kIconOpenIn, L"公式サイト", false);
    drawAbout(rt, b);
}

// A heading and a hairline to the right edge.
void UiPainter::drawSection(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, const wchar_t* title, float y) {
    float w = 0;
    b->SetColor(C(kText3));
    text(rt, b, title, badge_.get(), {kL, y, kR, y + 16}, 0, &w);
    b->SetColor(C(kWellBorder));
    rt->FillRectangle(D2D1::RectF(std::round(kL + w + 10), y + 8, kR, y + 9), b);
}

// A save folder: the field shows it and opens it in Explorer (also while recording); 変更 picks another in Explorer's
// own folder picker and waits while a recording runs, like the device fields. unset: what an empty path means.
void UiPainter::drawDirRow(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, const UiModel& m, Part field, Part change,
                           wchar_t icon, const wchar_t* label, const std::wstring& path, const wchar_t* unset, float y) {
    const D2D1_RECT_F f = partRect(field);
    const bool hot = m.hot == field, down = hot && m.pressed == field;
    b->SetColor(C(hot ? kText2 : kIcon));
    text(rt, b, std::wstring(1, icon), icon_.get(), {kL, y, kL + 18, y + kRowH});
    b->SetColor(C(kLabel));
    text(rt, b, label, label_.get(), {kL + 20, y, kFieldL - 2, y + kRowH});
    panel(rt, b, f, 5, down ? kControlDown : hot ? kControlHot : kInput, hot ? kControlBorderHot : kInputBorder, kInputEdge);
    const D2D1_RECT_F tr{f.left + 8, f.top, f.right - 24, f.bottom};
    if (path.empty()) {
        b->SetColor(C(kText2));
        text(rt, b, unset, field_.get(), tr);
    } else {
        b->SetColor(C(hot ? kPathFile : kPath));
        text(rt, b, path, path_.get(), tr);
    }
    b->SetColor(C(hot ? kText2 : kText3));
    text(rt, b, std::wstring(1, kIconFolderOpen), iconSmall_.get(), {f.right - 22, f.top, f.right - 6, f.bottom});
    if (m.showFocus && m.focus == field) focusRing(rt, b, f, 5);
    drawButton(rt, b, m, change, 0, L"変更", false);
}

// The three shortcuts as set, each in a key cap: amber when another app has the key (it does nothing then).
void UiPainter::drawKeys(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, const UiModel& m, float y) {
    static const wchar_t* const kNames[3] = {L"録画", L"録音", L"スクショ"};
    b->SetColor(C(kIcon));
    text(rt, b, std::wstring(1, kIconKeyboard), icon_.get(), {kL, y, kL + 18, y + kKeysH});
    rt->PushAxisAlignedClip({kL, y, kR, y + kKeysH}, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    float x = kL + 26;
    for (int i = 0; i < 3; ++i) {
        const UiModel::Key& k = m.keys[i];
        float lw = 0;
        b->SetColor(C(kText2));
        text(rt, b, kNames[i], sub_.get(), {x, y, x + 60, y + kKeysH}, 0, &lw);
        const std::wstring s = k.taken ? k.text + L" 使用中" : k.text;
        const float left = std::round(x + lw + 7);
        const D2D1_RECT_F r{left, y + 3, left + std::round(textWidth(s, key_.get())) + 16, y + kKeysH - 3};
        panel(rt, b, r, 4, k.taken ? kWarnBg : kWell, k.taken ? kWarnBorder : kControlBorder, k.taken ? 0 : kInputEdge);
        b->SetColor(C(k.taken ? kAmberText : k.text == L"なし" ? kText3 : kFieldText));
        text(rt, b, s, key_.get(), r);
        x = r.right + 16;
    }
    rt->PopAxisAlignedClip();
}

// What 「Rec Cue! について」 told before the settings view: the version and who guides.
void UiPainter::drawAbout(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b) {
    const D2D1_RECT_F r = kAboutBox;
    panel(rt, b, r, 5, kWell, kWellBorder, 0);
    b->SetColor(C(kIcon));
    text(rt, b, std::wstring(1, kIconInfo), iconSmall_.get(), {r.left + 7, r.top, r.left + 21, r.bottom});
    float w = 0;
    b->SetColor(C(kFieldText));
    text(rt, b, L"Rec Cue! " + wide(kVersion), badge_.get(), {r.left + 26, r.top, r.left + 140, r.bottom}, 0, &w);
    b->SetColor(C(kText3));
    text(rt, b, L"案内役 湊 しおり", sub_.get(), {r.left + 26 + w + 10, r.top, r.right - 8, r.bottom});
}

// しおり's line under the buttons: a speech bubble whose tail points at her.
void UiPainter::drawComment(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, const UiModel& m) {
    using T = UiModel::Tone;
    if (m.comment.empty()) return;
    const D2D1_RECT_F r = kBubble;
    UINT32 fill = kControl, border = m.hot == Part::Guide ? kControlBorderHot : kControlBorder, fg = kLabel;
    if (m.commentTone == T::Good) border = kToastBorder, fg = kCyanText;
    if (m.commentTone == T::Warn) fill = kWarnBg, border = kWarnBorder, fg = kAmberText;
    if (m.commentTone == T::Bad) fill = kRecChipBg, border = kCrimson, fg = kRosePale;
    const float cy = (r.top + r.bottom) / 2, x = r.right - 0.5f;
    Com<ID2D1PathGeometry> tail;   // a small point toward her, drawn under the bubble's edge
    Com<ID2D1GeometrySink> sink;
    if (SUCCEEDED(d2d_->CreatePathGeometry(tail.put())) && SUCCEEDED(tail->Open(sink.put()))) {
        sink->BeginFigure({x - 1, cy - 6}, D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLine({x + 8, cy});
        sink->AddLine({x - 1, cy + 6});
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close();
    }
    panel(rt, b, r, 8, fill, border, m.commentTone == T::Normal ? kEdge : 0);
    if (tail) {
        b->SetColor(C(fill));
        rt->FillGeometry(tail.get(), b);
        b->SetColor(C(border));
        rt->DrawLine({x, cy - 5.5f}, {x + 8, cy}, b, 1);
        rt->DrawLine({x + 8, cy}, {x, cy + 5.5f}, b, 1);
    }
    b->SetColor(C(kRoseLight));
    float nw = 0;
    text(rt, b, L"しおり", name_.get(), {r.left + 10, r.top, r.left + 60, r.bottom}, 0, &nw);
    b->SetColor(C(fg));
    text(rt, b, m.comment, status_.get(), {r.left + 10 + nw + 8, r.top, r.right - 8, r.bottom});
}

float UiPainter::commentRoom() const {   // as laid out above
    return (kBubble.right - 8) - (kBubble.left + 10 + textWidth(L"しおり", name_.get()) + 8);
}

float UiPainter::commentWidth(const std::wstring& s) const {
    Com<IDWriteTextLayout> l = layout(s, status_.get(), {0, 0, 2000, 30});
    DWRITE_TEXT_METRICS tm{};
    if (l) l->GetMetrics(&tm);
    return tm.widthIncludingTrailingWhitespace;
}

// The SD guide stands in the corner with her booklet, and takes notes while something is being recorded (she puts
// the pen down while it is paused).
void UiPainter::drawGuide(ID2D1RenderTarget* rt, const UiModel& m) {
    const int k = m.phase == UiModel::Phase::Recording && !m.paused && guide_[1] ? 1 : 0;
    if (!guide_[k]) return;
    const float s = static_cast<float>(dpi_) / 96.f;
    const D2D1_SIZE_F z = guideDip_[k];
    // whole device pixels, so the pre-scaled picture is drawn 1:1
    const float x = std::round(((kGuideBox.left + kGuideBox.right) / 2 - z.width / 2) * s) / s;
    const float y = std::round((kGuideBox.bottom - z.height) * s) / s;
    rt->DrawBitmap(guide_[k].get(), D2D1::RectF(x, y, x + z.width, y + z.height), 1.f,
                   D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
}

}  // namespace rc
