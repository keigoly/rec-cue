#include "video.h"

#include <algorithm>

namespace rc {

namespace {

struct OutRef {
    Com<IDXGIAdapter1> adapter;
    Com<IDXGIOutput> output;
    DXGI_OUTPUT_DESC desc{};
    std::wstring adapterName;
    bool primary = false;
};

std::vector<OutRef> enumOutputs() {
    std::vector<OutRef> v;
    Com<IDXGIFactory1> f;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), f.put_void()))) return v;
    for (UINT i = 0;; ++i) {
        Com<IDXGIAdapter1> a;
        if (FAILED(f->EnumAdapters1(i, a.put()))) break;
        DXGI_ADAPTER_DESC1 ad{};
        a->GetDesc1(&ad);
        if (ad.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
        for (UINT j = 0;; ++j) {
            Com<IDXGIOutput> o;
            if (FAILED(a->EnumOutputs(j, o.put()))) break;
            OutRef r;
            r.adapter = a;
            r.output = o;
            o->GetDesc(&r.desc);
            if (!r.desc.AttachedToDesktop) continue;
            MONITORINFO mi{};
            mi.cbSize = sizeof(mi);
            if (GetMonitorInfoW(r.desc.Monitor, &mi)) r.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
            r.adapterName = ad.Description;
            v.push_back(r);
        }
    }
    std::stable_sort(v.begin(), v.end(), [](const OutRef& x, const OutRef& y) { return x.primary && !y.primary; });
    return v;
}

std::string explain(HRESULT hr) {
    switch (hr) {
        case E_ACCESSDENIED: return "desktop not accessible (lock screen, UAC prompt, or not the logged-on user's session)";
        case DXGI_ERROR_UNSUPPORTED: return "duplication unsupported (remote desktop session or GPU/monitor mismatch)";
        case DXGI_ERROR_NOT_CURRENTLY_AVAILABLE: return "too many duplication sessions (another recorder is running?)";
        case DXGI_ERROR_SESSION_DISCONNECTED: return "session disconnected";
        case DXGI_ERROR_ACCESS_LOST: return "access lost (desktop switch or display mode change)";
        case DXGI_ERROR_DEVICE_REMOVED: return "GPU device removed or reset";
        default: return "duplication failed";
    }
}

// The monitor behind each desktop (\\.\DISPLAY1 -> its EDID name and how it is connected), from the display
// configuration. Monitors it does not list keep an empty name.
void nameMonitors(std::vector<MonitorInfo>& mons) {
    UINT32 np = 0, nm = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &np, &nm) != ERROR_SUCCESS) return;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(np);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(nm);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &np, paths.data(), &nm, modes.data(), nullptr) != ERROR_SUCCESS) return;
    std::vector<bool> named(mons.size(), false);   // a built-in panel's name may be empty, so it is not the test
    for (UINT32 i = 0; i < np; ++i) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME src{};
        src.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        src.header.size = sizeof(src);
        src.header.adapterId = paths[i].sourceInfo.adapterId;
        src.header.id = paths[i].sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&src.header) != ERROR_SUCCESS) continue;
        DISPLAYCONFIG_TARGET_DEVICE_NAME tgt{};
        tgt.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
        tgt.header.size = sizeof(tgt);
        tgt.header.adapterId = paths[i].targetInfo.adapterId;
        tgt.header.id = paths[i].targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&tgt.header) != ERROR_SUCCESS) continue;
        const auto t = tgt.outputTechnology;
        for (size_t k = 0; k < mons.size(); ++k) {
            MonitorInfo& m = mons[k];
            if (m.device != src.viewGdiDeviceName || named[k]) continue;   // a cloned desktop keeps the first
            named[k] = true;
            m.name = tgt.monitorFriendlyDeviceName;
            m.internal = t == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INTERNAL || t == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_LVDS ||
                         t == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EMBEDDED ||
                         t == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_UDI_EMBEDDED;
        }
    }
}

}  // namespace

std::vector<MonitorInfo> listMonitors(bool names) {
    std::vector<MonitorInfo> out;
    int i = 0;
    for (auto& r : enumOutputs()) {
        MonitorInfo m;
        m.index = i++;
        m.device = r.desc.DeviceName;
        m.adapter = r.adapterName;
        m.rect = r.desc.DesktopCoordinates;
        m.primary = r.primary;
        out.push_back(m);
    }
    if (names) nameMonitors(out);
    return out;
}

VideoSource::~VideoSource() {
    dup_.reset();
    if (ctx_) {
        ctx_->ClearState();
        ctx_->Flush();
    }
}

HRESULT VideoSource::init(int monitorIndex, bool testPattern, std::string* info) {
    Stopwatch sw;
    test_ = testPattern;
    D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                  D3D_FEATURE_LEVEL_10_0};
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
    HRESULT hr = E_FAIL;
    std::string desc;
    if (!test_) {
        auto outs = enumOutputs();
        if (outs.empty()) {
            Log::event("video.init", false, sw.ms(), "no monitor is attached to the desktop");
            return E_FAIL;
        }
        if (monitorIndex < 0 || monitorIndex >= static_cast<int>(outs.size())) {
            Log::event("video.monitor", false, -1, fmt("monitor=%d not found, using the primary one", monitorIndex));
            monitorIndex = 0;
        }
        OutRef& r = outs[static_cast<size_t>(monitorIndex)];
        adapter_ = r.adapter;
        r.output.as(output_);
        deviceName_ = r.desc.DeviceName;
        rect_ = r.desc.DesktopCoordinates;
        for (int attempt = 0; attempt < 2 && FAILED(hr); ++attempt) {
            hr = D3D11CreateDevice(adapter_.get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags, levels, ARRAYSIZE(levels),
                                   D3D11_SDK_VERSION, dev_.put(), nullptr, ctx_.put());
            if (FAILED(hr)) flags &= ~static_cast<UINT>(D3D11_CREATE_DEVICE_VIDEO_SUPPORT);
        }
        w_ = static_cast<UINT>(rect_.right - rect_.left) & ~1u;
        h_ = static_cast<UINT>(rect_.bottom - rect_.top) & ~1u;
        modeW_ = static_cast<UINT>(rect_.right - rect_.left);
        modeH_ = static_cast<UINT>(rect_.bottom - rect_.top);
        desc = fmt("monitor=%d %s %ux%u at (%ld,%ld) adapter=%s", monitorIndex, u8(deviceName_).c_str(), w_, h_,
                   rect_.left, rect_.top, u8(r.adapterName).c_str());
    } else {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, ARRAYSIZE(levels),
                               D3D11_SDK_VERSION, dev_.put(), nullptr, ctx_.put());
        if (FAILED(hr))
            hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels,
                                   ARRAYSIZE(levels), D3D11_SDK_VERSION, dev_.put(), nullptr, ctx_.put());
        // Same size as the screen so memory and encoder load match a real recording (no screen content is read).
        w_ = static_cast<UINT>(GetSystemMetrics(SM_CXSCREEN)) & ~1u;
        h_ = static_cast<UINT>(GetSystemMetrics(SM_CYSCREEN)) & ~1u;
        if (w_ < 320 || h_ < 240) { w_ = 1280; h_ = 720; }
        desc = fmt("test pattern %ux%u", w_, h_);
    }
    if (FAILED(hr)) {
        Log::event("video.device", false, sw.ms(), "D3D11CreateDevice failed", hr);
        return hr;
    }
    desc += fmt(" video_support=%s", (flags & D3D11_CREATE_DEVICE_VIDEO_SUPPORT) ? "yes" : "no");
    Com<ID3D10Multithread> mt;
    if (SUCCEEDED(dev_.as(mt))) mt->SetMultithreadProtected(TRUE);

    D3D11_TEXTURE2D_DESC td{};
    td.Width = w_;
    td.Height = h_;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    hr = dev_->CreateTexture2D(&td, nullptr, last_.put());
    if (SUCCEEDED(hr)) {
        td.MiscFlags = D3D11_RESOURCE_MISC_GDI_COMPATIBLE;
        hr = dev_->CreateTexture2D(&td, nullptr, compose_.put());
    }
    if (FAILED(hr)) {
        Log::event("video.texture", false, sw.ms(), "CreateTexture2D failed", hr);
        return hr;
    }
    Com<ID3D11RenderTargetView> rtv;
    if (SUCCEEDED(dev_->CreateRenderTargetView(last_.get(), nullptr, rtv.put()))) {
        const float black[4] = {0, 0, 0, 1};
        ctx_->ClearRenderTargetView(rtv.get(), black);
    }
    if (!test_ && !duplicate(true)) {
        lostSince_ = qpcNow();
        stats.lostEvents++;
    }
    if (info) *info = desc;
    Log::event("video.init", true, sw.ms(), desc + (dup_ ? "" : " (no desktop image yet, retrying)"));
    return S_OK;
}

bool VideoSource::duplicate(bool logIt) {
    Stopwatch sw;
    dup_.reset();
    HRESULT hr = output_ ? output_->DuplicateOutput(dev_.get(), dup_.put()) : E_POINTER;
    if (FAILED(hr) && hr != E_ACCESSDENIED) {
        // The output object goes stale after a display change: look it up again by name.
        for (auto& r : enumOutputs()) {
            if (deviceName_ != r.desc.DeviceName) continue;
            Com<IDXGIOutput1> o1;
            if (SUCCEEDED(r.output.as(o1))) {
                output_ = o1;
                hr = output_->DuplicateOutput(dev_.get(), dup_.put());
            }
            break;
        }
    }
    if (FAILED(hr)) {
        dup_.reset();
        if (logIt || hr != lastFail_) Log::event("video.duplicate", false, sw.ms(), explain(hr), hr);
        lastFail_ = hr;
        stats.recreateFail++;
        return false;
    }
    DXGI_OUTDUPL_DESC dd{};
    dup_->GetDesc(&dd);
    // After a display change the desktop can have a new size or position. The recording keeps its
    // size: the new picture is cropped or padded with black, and the cursor follows the new origin.
    DXGI_OUTPUT_DESC od{};
    if (SUCCEEDED(output_->GetDesc(&od))) rect_ = od.DesktopCoordinates;
    const UINT dw = static_cast<UINT>(rect_.right - rect_.left), dh = static_cast<UINT>(rect_.bottom - rect_.top);
    if (modeW_ && (dw != modeW_ || dh != modeH_)) {
        Com<ID3D11RenderTargetView> rtv;
        if (SUCCEEDED(dev_->CreateRenderTargetView(last_.get(), nullptr, rtv.put()))) {
            const float black[4] = {0, 0, 0, 1};
            ctx_->ClearRenderTargetView(rtv.get(), black);
        }
        Log::event("video.size_changed", false, -1,
                   fmt("desktop %ux%u -> %ux%u; the recording stays %ux%u (cropped or padded)", modeW_, modeH_, dw, dh, w_, h_));
    }
    modeW_ = dw;
    modeH_ = dh;
    if (logIt || lastFail_ != S_OK)
        Log::event("video.duplicate", true, sw.ms(),
                   fmt("%ux%u format=%d rotation=%d", dd.ModeDesc.Width, dd.ModeDesc.Height,
                       static_cast<int>(dd.ModeDesc.Format), static_cast<int>(dd.Rotation)));
    lastFail_ = S_OK;
    return true;
}

void VideoSource::lost(HRESULT hr) {
    dup_.reset();
    stats.lostEvents++;
    lostSince_ = qpcNow();
    nextRetry_ = 0;
    Log::event("video.lost", false, -1, explain(hr), hr);
}

bool VideoSource::update() {
    if (test_) {
        testFrame();
        return true;
    }
    const int64_t now = qpcNow();
    if (!dup_) {
        if (now < nextRetry_) return false;
        nextRetry_ = now + qpcFreq() / 2;
        if (!duplicate(false)) return false;
        if (lostSince_) {
            double ms = qpcMs(now - lostSince_);
            stats.lostMs += ms;
            Log::event("video.recovered", true, ms, "desktop image is back");
            lostSince_ = 0;
        }
    }
    DXGI_OUTDUPL_FRAME_INFO fi{};
    Com<IDXGIResource> res;
    HRESULT hr = dup_->AcquireNextFrame(0, &fi, res.put());
    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        stats.unchanged++;
        return false;
    }
    if (FAILED(hr)) {
        lost(hr);
        return false;
    }
    bool changed = false;
    if (fi.LastPresentTime.QuadPart != 0 || !have_) {
        Com<ID3D11Texture2D> tex;
        if (SUCCEEDED(res.as(tex))) {
            D3D11_TEXTURE2D_DESC d{};
            tex->GetDesc(&d);
            if (d.Format == DXGI_FORMAT_B8G8R8A8_UNORM || d.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) {
                D3D11_BOX box{0, 0, 0, std::min(d.Width, w_), std::min(d.Height, h_), 1};
                ctx_->CopySubresourceRegion(last_.get(), 0, 0, 0, 0, tex.get(), 0, &box);
                have_ = true;
                changed = true;
                stats.newFrames++;
            } else if (stats.formatMismatch++ == 0) {
                Log::event("video.format", false, -1,
                           fmt("desktop format %d is not BGRA (HDR is on?) - frames stay black", static_cast<int>(d.Format)));
            }
        }
    } else {
        stats.pointerOnly++;
    }
    dup_->ReleaseFrame();
    return changed;
}

ID3D11Texture2D* VideoSource::compose(bool cursor) {
    ctx_->CopyResource(compose_.get(), last_.get());
    if (cursor && !test_ && dup_) drawCursor();
    return compose_.get();
}

void VideoSource::drawCursor() {
    CURSORINFO ci{};
    ci.cbSize = sizeof(ci);
    if (!GetCursorInfo(&ci) || !(ci.flags & CURSOR_SHOWING) || !ci.hCursor) {
        stats.cursorHidden++;
        return;
    }
    const int x = ci.ptScreenPos.x - rect_.left;
    const int y = ci.ptScreenPos.y - rect_.top;
    if (x < -64 || y < -64 || x > static_cast<int>(w_) + 64 || y > static_cast<int>(h_) + 64) {
        stats.cursorAway++;
        return;
    }
    if (ci.hCursor != cursor_) {
        stats.cursorShapes++;
        ICONINFO ii{};
        hotX_ = hotY_ = 0;
        if (GetIconInfo(ci.hCursor, &ii)) {
            hotX_ = static_cast<int>(ii.xHotspot);
            hotY_ = static_cast<int>(ii.yHotspot);
            if (ii.hbmMask) DeleteObject(ii.hbmMask);
            if (ii.hbmColor) DeleteObject(ii.hbmColor);
        }
        cursor_ = ci.hCursor;
    }
    Com<IDXGISurface1> s;
    if (FAILED(compose_.as(s))) return;
    HDC dc = nullptr;
    if (FAILED(s->GetDC(FALSE, &dc))) return;
    DrawIconEx(dc, x - hotX_, y - hotY_, ci.hCursor, 0, 0, 0, nullptr, DI_NORMAL);
    s->ReleaseDC(nullptr);
    stats.cursorDrawn++;
}

// Synthetic picture for self-tests: scrolling color bars and a moving white block (no screen content).
void VideoSource::testFrame() {
    testBuf_.resize(static_cast<size_t>(w_) * h_);
    static const uint32_t bars[8] = {0xFFFFFFFF, 0xFFFFFF00, 0xFF00FFFF, 0xFF00FF00,
                                     0xFFFF00FF, 0xFFFF0000, 0xFF0000FF, 0xFF000000};
    const UINT shift = static_cast<UINT>(testN_ * 8 % w_);
    const UINT bx = static_cast<UINT>(testN_ * 16 % (w_ - 96));
    for (UINT y = 0; y < h_; ++y) {
        uint32_t* row = testBuf_.data() + static_cast<size_t>(y) * w_;
        for (UINT x = 0; x < w_; ++x) row[x] = bars[((x + shift) % w_) * 8 / w_];
        if (y >= h_ / 2 - 48 && y < h_ / 2 + 48)
            for (UINT x = bx; x < bx + 96; ++x) row[x] = 0xFFFFFFFF;
    }
    ctx_->UpdateSubresource(last_.get(), 0, nullptr, testBuf_.data(), w_ * 4, 0);
    ++testN_;
    stats.newFrames++;
    have_ = true;
}

}  // namespace rc
