#include "common.h"

#include <shlobj.h>

#include <cstdarg>
#include <cstdio>

namespace rc {

static int64_t freqOnce() {
    static const int64_t f = [] { LARGE_INTEGER v; QueryPerformanceFrequency(&v); return v.QuadPart; }();
    return f;
}

int64_t qpcNow() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
int64_t qpcFreq() { return freqOnce(); }
int64_t qpcTo100ns(int64_t t) {
    const int64_t f = freqOnce();
    return (t / f) * 10000000LL + (t % f) * 10000000LL / f;
}
double qpcMs(int64_t t) { return static_cast<double>(t) * 1000.0 / static_cast<double>(freqOnce()); }

UiCounters& uiCounters() {
    static UiCounters c;
    return c;
}

std::string u8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string hrHex(HRESULT hr) { return fmt("0x%08lX", static_cast<unsigned long>(hr)); }

std::string json(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 2);
    o += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (c < 0x20) o += fmt("\\u%04x", c);
                else o += static_cast<char>(c);
        }
    }
    o += '"';
    return o;
}

std::string json(const std::wstring& s) { return json(u8(s)); }

std::string fmt(const char* f, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, f);
    int n = vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    if (n < 0) return {};
    if (n < static_cast<int>(sizeof(buf))) return std::string(buf, static_cast<size_t>(n));
    std::string big(static_cast<size_t>(n) + 1, '\0');
    va_start(ap, f);
    vsnprintf(big.data(), big.size(), f, ap);
    va_end(ap);
    big.resize(static_cast<size_t>(n));
    return big;
}

std::wstring lower(std::wstring s) {
    if (!s.empty()) CharLowerBuffW(s.data(), static_cast<DWORD>(s.size()));
    return s;
}

std::string nowIso() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    TIME_ZONE_INFORMATION tz;
    DWORD r = GetTimeZoneInformation(&tz);
    long bias = tz.Bias + (r == TIME_ZONE_ID_DAYLIGHT ? tz.DaylightBias : 0);
    long off = -bias;
    char sign = off >= 0 ? '+' : '-';
    if (off < 0) off = -off;
    return fmt("%04u-%02u-%02uT%02u:%02u:%02u.%03u%c%02ld:%02ld", st.wYear, st.wMonth, st.wDay, st.wHour,
               st.wMinute, st.wSecond, st.wMilliseconds, sign, off / 60, off % 60);
}

std::wstring stamp() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t b[32];
    swprintf(b, 32, L"%04u-%02u-%02u %02u%02u%02u", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return b;
}

std::wstring appDir() {
    PWSTR p = nullptr;
    std::wstring d;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &p))) d = p;
    CoTaskMemFree(p);
    if (d.empty()) d = L"C:\\";
    return d + L"\\.rec-cue";
}

bool ensureDir(const std::wstring& path) {
    int r = SHCreateDirectoryExW(nullptr, path.c_str(), nullptr);
    return r == ERROR_SUCCESS || r == ERROR_ALREADY_EXISTS || r == ERROR_FILE_EXISTS;
}

std::wstring exeDir() {
    wchar_t b[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, b, MAX_PATH * 2);
    std::wstring s(b, n);
    size_t k = s.find_last_of(L"\\/");
    return k == std::wstring::npos ? s : s.substr(0, k);
}

namespace Log {
namespace {
std::mutex g_mu;
HANDLE g_file = INVALID_HANDLE_VALUE;
std::wstring g_dir;
std::string g_sid = "-";
std::string g_lastErr;

void writeRaw(const std::string& s) {
    if (g_file == INVALID_HANDLE_VALUE) return;
    DWORD w = 0;
    WriteFile(g_file, s.data(), static_cast<DWORD>(s.size()), &w, nullptr);
}
}  // namespace

void init() {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_file != INVALID_HANDLE_VALUE) return;
    g_dir = appDir() + L"\\logs";
    ensureDir(g_dir);
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t name[64];
    swprintf(name, 64, L"\\rec-cue-%04u%02u%02u.log", st.wYear, st.wMonth, st.wDay);
    g_file = CreateFileW((g_dir + name).c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                         OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
}

void setSession(const std::string& sid) { std::lock_guard<std::mutex> lk(g_mu); g_sid = sid; }
std::string session() { std::lock_guard<std::mutex> lk(g_mu); return g_sid; }

std::string newSessionId() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    unsigned r = static_cast<unsigned>(qpcNow() ^ (GetCurrentProcessId() << 8)) & 0xffff;
    return fmt("%04u%02u%02u-%02u%02u%02u-%04x", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, r);
}

void event(const char* phase, bool ok, double ms, const std::string& msg, HRESULT hr) {
    std::string s = "{\"ts\":" + json(nowIso());
    std::lock_guard<std::mutex> lk(g_mu);
    s += ",\"sid\":" + json(g_sid) + ",\"phase\":" + json(std::string(phase)) + ",\"ok\":" + (ok ? "true" : "false");
    if (ms >= 0) s += fmt(",\"ms\":%.1f", ms);
    if (hr != S_OK) s += ",\"hr\":" + json(hrHex(hr));
    if (!msg.empty()) s += ",\"msg\":" + json(msg);
    s += "}\r\n";
    writeRaw(s);
    if (!ok) g_lastErr = std::string(phase) + ": " + msg + (hr != S_OK ? " (" + hrHex(hr) + ")" : "");
}

void line(const char* phase, const std::string& fields) {
    std::string s = "{\"ts\":" + json(nowIso());
    std::lock_guard<std::mutex> lk(g_mu);
    s += ",\"sid\":" + json(g_sid) + ",\"phase\":" + json(std::string(phase));
    if (!fields.empty()) s += "," + fields;
    s += "}\r\n";
    writeRaw(s);
}

std::wstring dir() { std::lock_guard<std::mutex> lk(g_mu); return g_dir; }
std::string lastError() { std::lock_guard<std::mutex> lk(g_mu); return g_lastErr; }
}  // namespace Log

}  // namespace rc
