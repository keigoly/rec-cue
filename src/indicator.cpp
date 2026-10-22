// Rec Cue! — recording signs (see indicator.h).
#include "indicator.h"

#include <algorithm>
#include <cmath>

#include "video.h"

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

namespace rc {

namespace {

constexpr UINT32 kRed = 0xE11D48, kBlue = 0x3B82F6;   // the window's recording crimson / audio blue
const wchar_t* kDotClass = L"RecCueDot";

UINT32 colorOf(RecKind k) { return k == RecKind::Audio ? kBlue : kRed; }

// Anti-aliased disc of radius r at (cx, cy) in an n x n BGRA square, with an optional white rim of width `rim`
// around it. `alpha` scales the whole dot (the blinking). premul: premultiplied (layered window) or straight (icon).
void paintDot(uint32_t* px, int n, float cx, float cy, float r, UINT32 rgb, float alpha, float rim, bool premul) {
    const float R = static_cast<float>((rgb >> 16) & 0xFF), G = static_cast<float>((rgb >> 8) & 0xFF),
                B = static_cast<float>(rgb & 0xFF);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            const float d = std::hypot(static_cast<float>(x) + 0.5f - cx, static_cast<float>(y) + 0.5f - cy);
            const float fill = std::clamp(r - d + 0.5f, 0.f, 1.f);
            const float ring = rim > 0 ? (std::clamp(r + rim - d + 0.5f, 0.f, 1.f) - fill) * 0.9f : 0.f;
            const float cover = fill + ring;
            uint32_t& o = px[static_cast<size_t>(y) * static_cast<size_t>(n) + static_cast<size_t>(x)];
            if (cover <= 0) {
                o = 0;
                continue;
            }
            float cr = fill * R + ring * 255.f, cg = fill * G + ring * 255.f, cb = fill * B + ring * 255.f;
            if (premul) {
                cr *= alpha;
                cg *= alpha;
                cb *= alpha;
            } else {
                cr /= cover;
                cg /= cover;
                cb /= cover;
            }
            auto q = [](float v) { return static_cast<uint32_t>(std::clamp(v + 0.5f, 0.f, 255.f)); };
            o = (q(cover * alpha * 255.f) << 24) | (q(cr) << 16) | (q(cg) << 8) | q(cb);
        }
}

HBITMAP dib32(int n, void** bits) {
    BITMAPV5HEADER bi{};
    bi.bV5Size = sizeof(bi);
    bi.bV5Width = n;
    bi.bV5Height = -n;   // top-down
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;
    HDC dc = GetDC(nullptr);
    HBITMAP b = CreateDIBSection(dc, reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS, bits, nullptr, 0);
    ReleaseDC(nullptr, dc);
    return b;
}

// A small-icon-sized overlay: a dot about 10 px across (at 16 px) with a white rim, so it reads on the app icon.
HICON dotIcon(int n, UINT32 rgb, float alpha) {
    void* bits = nullptr;
    HBITMAP color = dib32(n, &bits);
    if (!color || !bits) {
        if (color) DeleteObject(color);
        return nullptr;
    }
    paintDot(static_cast<uint32_t*>(bits), n, n * 0.5f, n * 0.5f, n * 0.30f, rgb, alpha, std::max(1.f, n / 14.f), false);
    std::vector<uint8_t> zeros(static_cast<size_t>((n + 15) / 16 * 2) * static_cast<size_t>(n), 0);
    HBITMAP mask = CreateBitmap(n, n, 1, 1, zeros.data());   // transparency comes from the alpha channel
    ICONINFO ii{TRUE, 0, 0, mask, color};
    HICON h = CreateIconIndirect(&ii);
    DeleteObject(mask);
    DeleteObject(color);
    return h;
}

LRESULT CALLBACK dotProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCHITTEST) return HTTRANSPARENT;
    return DefWindowProcW(h, msg, wp, lp);
}

}  // namespace

Indicators::~Indicators() {
    stop();
    taskbar_.reset();
}

void Indicators::taskbarReady(HWND owner) {
    owner_ = owner;
    taskbar_.reset();
    Com<ITaskbarList3> t;
    HRESULT hr = CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, __uuidof(ITaskbarList3), t.put_void());
    if (SUCCEEDED(hr)) hr = t->HrInit();
    Log::event("ui.taskbar", SUCCEEDED(hr), -1, "taskbar button ready (overlay dot while recording)", hr);
    if (SUCCEEDED(hr)) taskbar_ = t;
    if (kind_ != RecKind::None) {   // Explorer restarted during a recording: put the dot back
        level_ = -1;
        applyOverlay(kLevels - 1);
    }
}

void Indicators::start(HWND owner, RecKind kind, int monitorIndex) {
    stop();
    owner_ = owner;
    kind_ = kind;
    makeIcons();
    applyOverlay(kLevels - 1);
    showDot(monitorIndex);
}

void Indicators::tick(float pulse) {
    if (kind_ == RecKind::None) return;
    applyOverlay(std::clamp(static_cast<int>(std::lround(pulse * (kLevels - 1))), 0, kLevels - 1));
    const int64_t now = qpcNow();
    if (dot_ && now >= nextRaise_) {
        SetWindowPos(dot_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        nextRaise_ = now + qpcFreq() * 2;
    }
}

void Indicators::stop() {
    if (taskbar_ && owner_ && kind_ != RecKind::None) taskbar_->SetOverlayIcon(owner_, nullptr, nullptr);
    kind_ = RecKind::None;
    level_ = -1;
    freeIcons();
    if (dot_) {
        DestroyWindow(dot_);
        dot_ = nullptr;
    }
}

void Indicators::shutdown() {
    stop();
    taskbar_.reset();
    owner_ = nullptr;
}

void Indicators::makeIcons() {
    freeIcons();
    const int n = GetSystemMetricsForDpi(SM_CXSMICON, GetDpiForSystem());   // the taskbar draws overlays at this size
    for (int i = 0; i < kLevels; ++i)
        icons_[i] = dotIcon(n, colorOf(kind_), 0.15f + 0.85f * static_cast<float>(i) / (kLevels - 1));
}

void Indicators::freeIcons() {
    for (auto& h : icons_) {
        if (h) DestroyIcon(h);
        h = nullptr;
    }
}

void Indicators::applyOverlay(int level) {
    if (!taskbar_ || !owner_ || level == level_ || !icons_[level]) return;
    const bool first = level_ < 0;
    level_ = level;
    const HRESULT hr = taskbar_->SetOverlayIcon(owner_, icons_[level], kind_ == RecKind::Audio ? L"録音中" : L"録画中");
    uiCounters().overlays++;
    if (first || FAILED(hr)) Log::event("ui.overlay", SUCCEEDED(hr), -1, kind_ == RecKind::Audio ? "blue (audio)" : "red (screen)", hr);
}

void Indicators::showDot(int monitorIndex) {
    const auto mons = listMonitors();
    if (mons.empty()) return;
    if (monitorIndex < 0 || monitorIndex >= static_cast<int>(mons.size())) monitorIndex = 0;
    const RECT mr = mons[static_cast<size_t>(monitorIndex)].rect;
    HINSTANCE inst = GetModuleHandleW(nullptr);
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = dotProc;
        wc.hInstance = inst;
        wc.lpszClassName = kDotClass;
        registered = RegisterClassExW(&wc) != 0;
    }
    dot_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                           kDotClass, L"", WS_POPUP, mr.left, mr.top, 1, 1, nullptr, nullptr, inst, nullptr);
    if (!dot_) {
        Log::event("ui.dot", false, -1, fmt("CreateWindowEx failed (%lu)", GetLastError()));
        return;
    }
    // A screen recording must not contain its own dot: without the capture exclusion (before Windows 10 2004) skip it.
    const bool excluded = SetWindowDisplayAffinity(dot_, WDA_EXCLUDEFROMCAPTURE) != 0;
    if (!excluded && kind_ == RecKind::Screen) {
        DestroyWindow(dot_);
        dot_ = nullptr;
        Log::event("ui.dot", false, -1, "not shown: it cannot be left out of the recording on this Windows");
        return;
    }
    const float s = static_cast<float>(GetDpiForWindow(dot_)) / 96.f;   // the monitor's DPI (per-monitor aware)
    const int n = std::max(8, static_cast<int>(std::lround(12 * s)));
    const int off = static_cast<int>(std::lround(5 * s));
    void* bits = nullptr;
    HBITMAP bmp = dib32(n, &bits);
    bool ok = false;
    if (bmp && bits) {
        paintDot(static_cast<uint32_t*>(bits), n, n * 0.5f, n * 0.5f, 4.f * s, colorOf(kind_), 1.f, 0, true);   // 8 DIP across
        HDC screen = GetDC(nullptr);
        HDC mem = CreateCompatibleDC(screen);
        HGDIOBJ old = SelectObject(mem, bmp);
        POINT pos{mr.left + off, mr.top + off}, src{0, 0};
        SIZE size{n, n};
        BLENDFUNCTION bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        ok = UpdateLayeredWindow(dot_, screen, &pos, &size, mem, &src, 0, &bf, ULW_ALPHA) != 0;
        SelectObject(mem, old);
        DeleteDC(mem);
        ReleaseDC(nullptr, screen);
    }
    if (bmp) DeleteObject(bmp);
    ShowWindow(dot_, SW_SHOWNOACTIVATE);
    nextRaise_ = qpcNow() + qpcFreq() * 2;
    Log::event("ui.dot", ok, -1,
               fmt("monitor=%d %dpx %s excluded=%s", monitorIndex, n, kind_ == RecKind::Audio ? "blue" : "red",
                   excluded ? "true" : "false"));
}

}  // namespace rc
