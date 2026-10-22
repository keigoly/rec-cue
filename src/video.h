// Rec Cue! — screen source: DXGI Desktop Duplication on the GPU that owns the monitor.
#pragma once

#include <d3d11.h>
#include <dxgi1_2.h>

#include "common.h"

namespace rc {

struct MonitorInfo {
    int index = 0;            // 0 = primary
    std::wstring device;      // \\.\DISPLAY1
    std::wstring adapter;     // Intel(R) Iris(R) Xe Graphics
    RECT rect{};
    bool primary = false;
};
std::vector<MonitorInfo> listMonitors();

class VideoSource {
public:
    struct Stats {
        uint64_t newFrames = 0;     // desktop changed and was copied
        uint64_t unchanged = 0;     // nothing new since the last tick
        uint64_t pointerOnly = 0;   // only the mouse moved
        uint64_t lostEvents = 0;    // duplication lost (lock screen, UAC, mode change)
        uint64_t recreateFail = 0;
        double lostMs = 0;          // total time without a desktop image
        uint64_t formatMismatch = 0;
        uint64_t cursorDrawn = 0;    // ticks with the mouse pointer drawn into the frame (GetDC + DrawIconEx)
        uint64_t cursorHidden = 0;   // pointer not showing (hidden while typing, touch)
        uint64_t cursorAway = 0;     // pointer on another monitor
        uint64_t cursorShapes = 0;   // pointer shape changes
    };

    ~VideoSource();
    // Creates the D3D11 device. Duplication failures are not fatal: frames stay black and
    // duplication is retried every 500 ms (lock screen, secure desktop, remote session).
    HRESULT init(int monitorIndex, bool testPattern, std::string* info);
    ID3D11Device* device() const { return dev_.get(); }
    ID3D11DeviceContext* context() const { return ctx_.get(); }
    UINT width() const { return w_; }
    UINT height() const { return h_; }
    bool capturing() const { return dup_ ? true : false; }

    // Pulls the newest desktop image (non-blocking). Returns true when the picture changed.
    bool update();
    // Current picture plus the mouse cursor, in a GDI-compatible BGRA texture.
    ID3D11Texture2D* compose(bool cursor);

    Stats stats;

private:
    bool duplicate(bool logIt);
    void lost(HRESULT hr);
    void drawCursor();
    void testFrame();

    Com<ID3D11Device> dev_;
    Com<ID3D11DeviceContext> ctx_;
    Com<IDXGIAdapter1> adapter_;
    Com<IDXGIOutput1> output_;
    Com<IDXGIOutputDuplication> dup_;
    Com<ID3D11Texture2D> last_;
    Com<ID3D11Texture2D> compose_;
    std::wstring deviceName_;
    RECT rect_{};
    UINT w_ = 0, h_ = 0;
    UINT modeW_ = 0, modeH_ = 0;   // current desktop size (may change during a recording)
    bool test_ = false;
    bool have_ = false;
    int64_t lostSince_ = 0;
    int64_t nextRetry_ = 0;
    HRESULT lastFail_ = S_OK;
    HCURSOR cursor_ = nullptr;
    int hotX_ = 0, hotY_ = 0;
    uint64_t testN_ = 0;
    std::vector<uint32_t> testBuf_;
};

}  // namespace rc
