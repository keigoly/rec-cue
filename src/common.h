// Rec Cue! — shared helpers: COM pointer, clocks, strings, JSON-lines log.
#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace rc {

constexpr const char* kVersion = "0.4.1";

// Minimal intrusive COM pointer (no WRL dependency).
template <class T>
class Com {
public:
    Com() = default;
    Com(const Com& o) : p_(o.p_) { if (p_) p_->AddRef(); }
    Com(Com&& o) noexcept : p_(o.p_) { o.p_ = nullptr; }
    ~Com() { reset(); }
    Com& operator=(const Com& o) {
        if (this != &o) { reset(); p_ = o.p_; if (p_) p_->AddRef(); }
        return *this;
    }
    Com& operator=(Com&& o) noexcept {
        if (this != &o) { reset(); p_ = o.p_; o.p_ = nullptr; }
        return *this;
    }
    static Com from(T* p) { Com c; c.p_ = p; if (p) p->AddRef(); return c; }
    T* get() const { return p_; }
    T* operator->() const { return p_; }
    explicit operator bool() const { return p_ != nullptr; }
    T** put() { reset(); return &p_; }
    void** put_void() { reset(); return reinterpret_cast<void**>(&p_); }
    void reset() { if (p_) { p_->Release(); p_ = nullptr; } }
    template <class U>
    HRESULT as(Com<U>& out) const {
        return p_ ? p_->QueryInterface(__uuidof(U), out.put_void()) : E_POINTER;
    }

private:
    T* p_ = nullptr;
};

// ---- clocks -------------------------------------------------------------
int64_t qpcNow();
int64_t qpcFreq();
int64_t qpcTo100ns(int64_t ticks);  // overflow-safe
double qpcMs(int64_t ticks);

class Stopwatch {
public:
    Stopwatch() : t0_(qpcNow()) {}
    double ms() const { return qpcMs(qpcNow() - t0_); }
private:
    int64_t t0_;
};

// ---- strings ------------------------------------------------------------
std::string u8(const std::wstring& w);
std::wstring wide(const std::string& s);
std::string hrHex(HRESULT hr);
std::string json(const std::string& s);   // quoted + escaped JSON string
std::string json(const std::wstring& s);
std::string fmt(const char* f, ...);
std::wstring lower(std::wstring s);
std::string nowIso();                      // 2026-10-20T19:30:01.234+09:00
std::wstring stamp();                      // 2026-10-20 193001 (for file names)

// ---- paths --------------------------------------------------------------
std::wstring appDir();                      // %USERPROFILE%\.rec-cue
bool ensureDir(const std::wstring& path);
std::wstring exeDir();

// ---- counters -----------------------------------------------------------
// What the window has done since it opened, for the 10 s stats line (2026-10-23: memory kept growing while
// recording from the window, not with --record). Counted on the window thread, read by the recorder.
struct UiCounters {
    std::atomic<uint64_t> paints{0};     // WM_PAINT drawn
    std::atomic<uint64_t> ticks{0};      // 100 ms ticks
    std::atomic<uint64_t> resizes{0};    // WM_SIZE (minimize / restore)
    std::atomic<uint64_t> targets{0};    // Direct2D window targets created
    std::atomic<uint64_t> overlays{0};   // taskbar overlay icon changes
};
UiCounters& uiCounters();

// ---- log ----------------------------------------------------------------
// One JSON object per line in %USERPROFILE%\.rec-cue\logs\rec-cue-YYYYMMDD.log.
// Every line carries ts / sid / phase; events add ok / ms / hr / msg.
namespace Log {
void init();
void setSession(const std::string& sid);
std::string session();
std::string newSessionId();
void event(const char* phase, bool ok, double ms, const std::string& msg, HRESULT hr = S_OK);
void line(const char* phase, const std::string& fieldsJson);  // fieldsJson: "\"k\":1,..."
std::wstring dir();
std::string lastError();
}  // namespace Log

}  // namespace rc
