// Rec Cue! — アップデート (see update.h).
#include "update.h"

#include <bcrypt.h>
#include <winhttp.h>

#include <cctype>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <functional>

namespace rc {

const wchar_t* const kUpdateApi = L"https://api.github.com/repos/keigoly/rec-cue/releases/latest";

namespace {

#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4   // the system's proxy settings, PAC included (Windows 8.1 and later)
#endif

constexpr uint64_t kApiLimit = 1ull << 20;    // the API's answer (a release is a few KB)
constexpr uint64_t kExeLimit = 64ull << 20;   // rec-cue.exe is about 1.3 MB
constexpr DWORD kSmokeMs = 20000;             // the new exe answers --version within this

struct Net {   // a WinHTTP handle
    HINTERNET h = nullptr;
    explicit Net(HINTERNET x) : h(x) {}
    ~Net() { if (h) WinHttpCloseHandle(h); }
    Net(const Net&) = delete;
    Net& operator=(const Net&) = delete;
};

std::string failed(const std::string& what, DWORD e = GetLastError()) { return what + fmt(" failed (%lu)", e); }

// GET `url` (https only; redirects are followed, as to the server that holds the release's files). The body goes to
// `sink` in pieces; false from it stops. true when all of a 200 answer was read.
bool httpGet(const std::wstring& url, const wchar_t* accept, uint64_t limit,
             const std::function<bool(const char*, DWORD)>& sink, int* status, std::string* err) {
    *status = 0;
    wchar_t host[256]{}, path[2048]{}, extra[2048]{};
    URL_COMPONENTS u{};
    u.dwStructSize = sizeof(u);
    u.lpszHostName = host;
    u.dwHostNameLength = 256;
    u.lpszUrlPath = path;
    u.dwUrlPathLength = 2048;
    u.lpszExtraInfo = extra;
    u.dwExtraInfoLength = 2048;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &u) || u.nScheme != INTERNET_SCHEME_HTTPS) {
        *err = "not an https address: " + u8(url);
        return false;
    }
    const std::wstring agent = L"RecCue/" + wide(kVersion) + L" (+https://github.com/keigoly/rec-cue)";
    Net s(WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!s.h) {
        *err = failed("WinHttpOpen");
        return false;
    }
    WinHttpSetTimeouts(s.h, 10000, 10000, 15000, 15000);   // resolve, connect, send, receive
    Net c(WinHttpConnect(s.h, host, u.nPort, 0));
    if (!c.h) {
        *err = failed("WinHttpConnect");
        return false;
    }
    const std::wstring target = std::wstring(path) + extra;
    Net r(WinHttpOpenRequest(c.h, L"GET", target.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                             WINHTTP_FLAG_SECURE));
    if (!r.h) {
        *err = failed("WinHttpOpenRequest");
        return false;
    }
    const std::wstring headers = std::wstring(L"Accept: ") + accept + L"\r\nX-GitHub-Api-Version: 2022-11-28\r\n";
    if (!WinHttpSendRequest(r.h, headers.c_str(), static_cast<DWORD>(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        *err = failed("WinHttpSendRequest");
        return false;
    }
    if (!WinHttpReceiveResponse(r.h, nullptr)) {
        *err = failed("WinHttpReceiveResponse");
        return false;
    }
    DWORD code = 0, size = sizeof(code);
    WinHttpQueryHeaders(r.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code,
                        &size, WINHTTP_NO_HEADER_INDEX);
    *status = static_cast<int>(code);
    if (code != 200) {
        *err = fmt("HTTP %lu", code);
        return false;
    }
    std::vector<char> buf(64 * 1024);
    uint64_t total = 0;
    for (;;) {
        DWORD n = 0;
        if (!WinHttpReadData(r.h, buf.data(), static_cast<DWORD>(buf.size()), &n)) {
            *err = failed("WinHttpReadData");
            return false;
        }
        if (n == 0) return true;
        total += n;
        if (total > limit) {
            *err = fmt("larger than %llu bytes", static_cast<unsigned long long>(limit));
            return false;
        }
        if (!sink(buf.data(), n)) {
            *err = "could not keep what came";
            return false;
        }
    }
}

// ---- the API's answer: a small JSON reader (objects, arrays, strings; numbers as double)

struct Json {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    std::string str;   // a String, UTF-8
    double num = 0;
    std::vector<Json> items;                            // an Array
    std::vector<std::pair<std::string, Json>> fields;   // an Object
    const Json* get(const char* key) const {
        for (auto& f : fields)
            if (f.first == key) return &f.second;
        return nullptr;
    }
    std::string text(const char* key) const {
        const Json* j = get(key);
        return j && j->type == String ? j->str : "";
    }
};

class JsonReader {
public:
    explicit JsonReader(const std::string& s) : s_(s) {}
    bool read(Json* out) {
        space();
        if (!value(out, 0)) return false;
        space();
        return i_ == s_.size();
    }

private:
    void space() {
        while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\n' || s_[i_] == '\r')) ++i_;
    }
    bool word(const char* w) {
        const size_t n = strlen(w);
        if (s_.compare(i_, n, w) != 0) return false;
        i_ += n;
        return true;
    }
    bool value(Json* v, int depth) {
        if (depth > 32 || i_ >= s_.size()) return false;
        const char c = s_[i_];
        if (c == '{') return object(v, depth);
        if (c == '[') return array(v, depth);
        if (c == '"') {
            v->type = Json::String;
            return string(&v->str);
        }
        if (word("true") || word("false")) {
            v->type = Json::Bool;
            return true;
        }
        if (word("null")) return true;
        char* end = nullptr;
        v->num = strtod(s_.c_str() + i_, &end);
        if (end == s_.c_str() + i_) return false;
        i_ = static_cast<size_t>(end - s_.c_str());
        v->type = Json::Number;
        return true;
    }
    bool object(Json* v, int depth) {
        v->type = Json::Object;
        ++i_;
        space();
        if (i_ < s_.size() && s_[i_] == '}') return ++i_, true;
        for (;;) {
            space();
            std::string key;
            if (i_ >= s_.size() || s_[i_] != '"' || !string(&key)) return false;
            space();
            if (i_ >= s_.size() || s_[i_++] != ':') return false;
            space();
            v->fields.emplace_back(std::move(key), Json{});
            if (!value(&v->fields.back().second, depth + 1)) return false;
            space();
            if (i_ >= s_.size()) return false;
            const char c = s_[i_++];
            if (c == '}') return true;
            if (c != ',') return false;
        }
    }
    bool array(Json* v, int depth) {
        v->type = Json::Array;
        ++i_;
        space();
        if (i_ < s_.size() && s_[i_] == ']') return ++i_, true;
        for (;;) {
            space();
            v->items.emplace_back();
            if (!value(&v->items.back(), depth + 1)) return false;
            space();
            if (i_ >= s_.size()) return false;
            const char c = s_[i_++];
            if (c == ']') return true;
            if (c != ',') return false;
        }
    }
    bool hex4(unsigned* out) {
        if (i_ + 4 > s_.size()) return false;
        unsigned x = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = s_[i_++];
            x <<= 4;
            if (c >= '0' && c <= '9') x |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') x |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') x |= static_cast<unsigned>(c - 'A' + 10);
            else return false;
        }
        *out = x;
        return true;
    }
    static void put(std::string* o, unsigned cp) {   // as UTF-8
        if (cp < 0x80) {
            *o += static_cast<char>(cp);
        } else if (cp < 0x800) {
            *o += static_cast<char>(0xC0 | (cp >> 6));
            *o += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            *o += static_cast<char>(0xE0 | (cp >> 12));
            *o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            *o += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            *o += static_cast<char>(0xF0 | (cp >> 18));
            *o += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            *o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            *o += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    bool string(std::string* o) {
        ++i_;   // the opening quote
        while (i_ < s_.size()) {
            const char c = s_[i_++];
            if (c == '"') return true;
            if (c != '\\') {
                *o += c;
                continue;
            }
            if (i_ >= s_.size()) return false;
            const char e = s_[i_++];
            switch (e) {
                case 'n': *o += '\n'; break;
                case 'r': *o += '\r'; break;
                case 't': *o += '\t'; break;
                case 'b': *o += '\b'; break;
                case 'f': *o += '\f'; break;
                case 'u': {
                    unsigned cp = 0;
                    if (!hex4(&cp)) return false;
                    if (cp >= 0xD800 && cp < 0xDC00 && s_.compare(i_, 2, "\\u") == 0) {   // a surrogate pair
                        i_ += 2;
                        unsigned lo = 0;
                        if (!hex4(&lo)) return false;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    put(o, cp);
                    break;
                }
                default: *o += e;   // \" \\ \/
            }
        }
        return false;
    }

    const std::string& s_;
    size_t i_ = 0;
};

// ---- the release notes: the bullets under 「## x.y.z の変更」 as plain text

// **bold**, `code` and [text](address) without their marks.
std::wstring plain(const std::wstring& s) {
    std::wstring o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s.compare(i, 2, L"**") == 0) {
            ++i;
            continue;
        }
        if (s[i] == L'`') continue;
        if (s[i] == L'[') {
            const size_t close = s.find(L"](", i);
            const size_t end = close == std::wstring::npos ? close : s.find(L')', close);
            if (end != std::wstring::npos) {
                o += s.substr(i + 1, close - i - 1);
                i = end;
                continue;
            }
        }
        o += s[i];
    }
    return o;
}

std::vector<std::wstring> releaseNotes(const std::wstring& body, const std::wstring& version) {
    std::vector<std::wstring> lines;
    size_t at = 0;
    while (at <= body.size()) {
        size_t nl = body.find(L'\n', at);
        if (nl == std::wstring::npos) nl = body.size();
        std::wstring l = body.substr(at, nl - at);
        if (!l.empty() && l.back() == L'\r') l.pop_back();
        lines.push_back(l);
        at = nl + 1;
    }
    // 「## 0.5.1 の変更」, else the first heading about changes
    size_t from = lines.size();
    for (int pass = 0; pass < 2 && from == lines.size(); ++pass)
        for (size_t i = 0; i < lines.size(); ++i)
            if (lines[i].rfind(L"## ", 0) == 0 && lines[i].find(L"変更") != std::wstring::npos &&
                (pass == 1 || lines[i].find(version) != std::wstring::npos)) {
                from = i + 1;
                break;
            }
    std::vector<std::wstring> notes;
    for (size_t i = from; i < lines.size() && notes.size() < 12; ++i) {
        const std::wstring& l = lines[i];
        if (l.rfind(L"#", 0) == 0 || l.rfind(L"---", 0) == 0) break;
        if (l.rfind(L"- ", 0) != 0) continue;
        std::wstring t = l.substr(2), title;
        if (t.rfind(L"**", 0) == 0) {   // - **title**: the rest
            const size_t end = t.find(L"**", 2);
            if (end != std::wstring::npos) {
                title = t.substr(2, end - 2);
                t = t.substr(end + 2);
                while (!t.empty() && (t[0] == L':' || t[0] == L'：' || t[0] == L' ')) t.erase(0, 1);
            }
        }
        notes.push_back(plain(title) + L"\t" + plain(t));
    }
    return notes;
}

// 2026-10-29T17:36:27Z → the local date (2026-10-30 in Japan)
std::wstring localDate(const std::string& iso) {
    SYSTEMTIME utc{}, local{};
    if (sscanf(iso.c_str(), "%hu-%hu-%huT%hu:%hu:%hu", &utc.wYear, &utc.wMonth, &utc.wDay, &utc.wHour, &utc.wMinute,
               &utc.wSecond) != 6)
        return wide(iso.substr(0, 10));
    if (!SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local)) local = utc;
    return wide(fmt("%04u-%02u-%02u", local.wYear, local.wMonth, local.wDay));
}

std::wstring dirOf(const std::wstring& path) {
    const size_t k = path.find_last_of(L"\\/");
    return k == std::wstring::npos ? L"." : path.substr(0, k);
}

// rec-cue.exe → rec-cue<suffix> beside it
std::wstring sibling(const std::wstring& exe, const wchar_t* suffix) {
    std::wstring s = exe;
    if (s.size() > 4 && lower(s.substr(s.size() - 4)) == L".exe") s.resize(s.size() - 4);
    return s + suffix;
}

std::string sha256File(const std::wstring& path, std::string* err) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        *err = failed("open");
        return {};
    }
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE h = nullptr;
    std::string hex;
    UCHAR d[32]{};
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0 &&
        BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0) >= 0) {
        std::vector<UCHAR> buf(64 * 1024);
        DWORD n = 0;
        bool ok = true;
        while (ok && ReadFile(f, buf.data(), static_cast<DWORD>(buf.size()), &n, nullptr) && n > 0)
            ok = BCryptHashData(h, buf.data(), n, 0) >= 0;
        if (ok && BCryptFinishHash(h, d, sizeof(d), 0) >= 0)
            for (UCHAR b : d) hex += fmt("%02x", b);
    }
    if (hex.empty()) *err = "SHA-256 could not be computed";
    if (h) BCryptDestroyHash(h);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    CloseHandle(f);
    return hex;
}

// Runs the new exe once with --version: it has to start here (not stopped by the security software) and be the
// release's version.
bool smoke(const std::wstring& exe, const std::wstring& version, std::string* err) {
    wchar_t tmp[MAX_PATH + 1]{};
    GetTempPathW(MAX_PATH, tmp);
    const std::wstring out = std::wstring(tmp) + wide(fmt("rec-cue-update-%lu.json", GetCurrentProcessId()));
    DeleteFileW(out.c_str());
    std::wstring cmd = L"\"" + exe + L"\" --version --json-out \"" + out + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, dirOf(exe).c_str(), &si, &pi)) {
        *err = failed("CreateProcess");
        return false;
    }
    CloseHandle(pi.hThread);
    const DWORD w = WaitForSingleObject(pi.hProcess, kSmokeMs);
    DWORD code = 0;
    if (w != WAIT_OBJECT_0) TerminateProcess(pi.hProcess, 1);
    else GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    std::string said;
    HANDLE f = CreateFileW(out.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
        char b[256]{};
        DWORD n = 0;
        if (ReadFile(f, b, sizeof(b) - 1, &n, nullptr)) said.assign(b, n);
        CloseHandle(f);
        DeleteFileW(out.c_str());
    }
    if (w != WAIT_OBJECT_0) *err = fmt("no answer to --version in %lu ms", kSmokeMs);
    else if (code != 0) *err = fmt("--version ended with %lu", code);
    else if (said.find("\"version\":" + json(u8(version))) == std::string::npos) *err = "--version said " + said;
    return err->empty();
}

}  // namespace

UpdateCheck checkLatest(const std::wstring& api) {
    UpdateCheck c;
    Stopwatch sw;
    std::string body;
    const bool got = httpGet(api, L"application/vnd.github+json", kApiLimit,
                             [&body](const char* p, DWORD n) { return body.append(p, n), true; }, &c.status, &c.error);
    c.ms = sw.ms();
    if (!got) return c;
    Json j;
    if (!JsonReader(body).read(&j) || j.type != Json::Object) {
        c.error = "the answer is not a release";
        return c;
    }
    Release& r = c.latest;
    std::wstring tag = wide(j.text("tag_name"));
    if (!tag.empty() && (tag[0] == L'v' || tag[0] == L'V')) tag.erase(0, 1);
    r.version = tag;
    r.page = wide(j.text("html_url"));
    r.published = localDate(j.text("published_at"));
    r.notes = releaseNotes(wide(j.text("body")), r.version);
    if (const Json* assets = j.get("assets"))
        for (const Json& a : assets->items)
            if (lower(wide(a.text("name"))) == L"rec-cue.exe") {
                r.assetUrl = wide(a.text("browser_download_url"));
                if (const Json* s = a.get("size")) r.size = s->num > 0 ? static_cast<uint64_t>(s->num) : 0;
                const std::string d = a.text("digest");   // sha256:<hex>
                if (d.rfind("sha256:", 0) == 0) r.sha256 = d.substr(7);
                for (char& ch : r.sha256) ch = static_cast<char>(tolower(static_cast<unsigned char>(ch)));
            }
    if (r.version.empty()) c.error = "the release has no tag";
    else if (r.assetUrl.empty()) c.error = "the release has no rec-cue.exe";
    c.ok = c.error.empty();
    return c;
}

int compareVersions(const std::wstring& a, const std::wstring& b) {
    size_t i = 0, k = 0;
    while (i < a.size() || k < b.size()) {
        auto part = [](const std::wstring& s, size_t& at) {
            long v = 0;
            while (at < s.size() && iswdigit(s[at])) v = v * 10 + (s[at++] - L'0');
            if (at < s.size() && s[at] == L'.') ++at;
            else at = s.size();   // a suffix (-beta) or the end
            return v;
        };
        const long x = part(a, i), y = part(b, k);
        if (x != y) return x < y ? -1 : 1;
    }
    return 0;
}

UpdateResult installRelease(const Release& r, const std::wstring& exe, UpdateProgress* p) {
    UpdateResult u;
    const std::wstring next = sibling(exe, L".update.exe"), prev = sibling(exe, L".prev.exe");
    auto stop = [&u, &next](const char* step, const std::string& e) {
        u.step = step;
        u.error = e;
        DeleteFileW(next.c_str());
        return u;
    };
    // 1. download beside the exe (the same volume, so the swap is a rename)
    p->stage = UpdateProgress::Download;
    Stopwatch sw;
    HANDLE f = CreateFileW(next.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        const DWORD e = GetLastError();
        return stop("folder", failed("create " + u8(next), e));
    }
    int status = 0;
    std::string err;
    bool wrote = true;
    const bool got = httpGet(r.assetUrl, L"application/octet-stream", kExeLimit,
                             [&](const char* d, DWORD n) {
                                 DWORD w = 0;
                                 wrote = WriteFile(f, d, n, &w, nullptr) && w == n;
                                 u.bytes += n;
                                 p->got = u.bytes;
                                 return wrote;
                             },
                             &status, &err);
    wrote = FlushFileBuffers(f) && wrote;
    CloseHandle(f);
    u.downloadMs = sw.ms();
    if (!got) return stop("download", err);
    if (!wrote) return stop("download", failed("write"));
    // 2. the same file as the release says
    p->stage = UpdateProgress::Verify;
    sw = Stopwatch();
    if (r.size && u.bytes != r.size)
        return stop("size", fmt("%llu bytes, the release says %llu", static_cast<unsigned long long>(u.bytes),
                                static_cast<unsigned long long>(r.size)));
    if (!r.sha256.empty()) {
        const std::string h = sha256File(next, &err);
        if (h != r.sha256) return stop("sha256", h.empty() ? err : "SHA-256 " + h + ", the release says " + r.sha256);
    }
    u.verifyMs = sw.ms();
    sw = Stopwatch();
    if (!smoke(next, r.version, &err)) return stop("smoke", err);
    u.smokeMs = sw.ms();
    // 3. swap: a running exe can be renamed, not overwritten. The old one stays as <name>.prev.exe
    p->stage = UpdateProgress::Swap;
    sw = Stopwatch();
    if (!MoveFileExW(exe.c_str(), prev.c_str(), MOVEFILE_REPLACE_EXISTING)) return stop("swap", failed("rename to .prev.exe"));
    if (!MoveFileExW(next.c_str(), exe.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        const DWORD e = GetLastError();
        MoveFileExW(prev.c_str(), exe.c_str(), 0);
        return stop("swap", failed("move the new exe in", e));
    }
    u.swapMs = sw.ms();
    p->stage = UpdateProgress::Done;
    u.ok = true;
    return u;
}

std::string updateCheckJson(const UpdateCheck& c) {
    std::string notes = "[";
    for (const auto& n : c.latest.notes) notes += (notes.size() > 1 ? "," : "") + json(n);
    notes += "]";
    return fmt("{\"ok\":%s,\"status\":%d,\"ms\":%.1f,\"latest\":", c.ok ? "true" : "false", c.status, c.ms) +
           json(c.latest.version) + ",\"published\":" + json(c.latest.published) + ",\"page\":" + json(c.latest.page) +
           ",\"asset\":" + json(c.latest.assetUrl) + fmt(",\"size\":%llu,\"sha256\":", static_cast<unsigned long long>(c.latest.size)) +
           json(c.latest.sha256) + ",\"notes\":" + notes + ",\"error\":" + json(c.error) + "}";
}

std::string updateResultJson(const UpdateResult& u) {
    return fmt("{\"ok\":%s,\"step\":", u.ok ? "true" : "false") + json(u.step) + ",\"error\":" + json(u.error) +
           fmt(",\"bytes\":%llu,\"download_ms\":%.1f,\"verify_ms\":%.1f,\"smoke_ms\":%.1f,\"swap_ms\":%.1f}",
               static_cast<unsigned long long>(u.bytes), u.downloadMs, u.verifyMs, u.smokeMs, u.swapMs);
}

bool relaunch(const std::wstring& exe) {
    std::wstring cmd = L"\"" + exe + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, dirOf(exe).c_str(), &si, &pi))
        return false;
    AllowSetForegroundWindow(pi.dwProcessId);   // its window comes up in front, as this one was
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

std::wstring selfPath() {
    wchar_t b[MAX_PATH * 2];
    const DWORD n = GetModuleFileNameW(nullptr, b, MAX_PATH * 2);
    return std::wstring(b, n);
}

}  // namespace rc
