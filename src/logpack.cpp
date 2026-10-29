#include "logpack.h"

#include <shlobj.h>

#include <algorithm>
#include <array>

#include "config.h"

namespace rc {

namespace {

constexpr uint64_t kFileLimit = 64ull << 20;    // a single file larger than this is left out
constexpr uint64_t kTotalLimit = 256ull << 20;  // ... and so is everything past this (the ZIP stays well under 4 GB)

uint32_t crc32(const std::string& s) {
    static const auto table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    uint32_t c = 0xFFFFFFFFu;
    for (unsigned char b : s) c = table[(c ^ b) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

void put16(std::string& o, uint32_t v) {
    o += static_cast<char>(v & 0xFF);
    o += static_cast<char>((v >> 8) & 0xFF);
}

void put32(std::string& o, uint32_t v) {
    put16(o, v & 0xFFFF);
    put16(o, v >> 16);
}

// The whole file, shared with the writer of today's log (which goes on appending).
bool readAll(const std::wstring& path, std::string* out, FILETIME* written) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(h, &size) && static_cast<uint64_t>(size.QuadPart) <= kFileLimit &&
              GetFileTime(h, nullptr, nullptr, written);
    if (ok) {
        out->resize(static_cast<size_t>(size.QuadPart));
        DWORD n = 0;
        ok = out->empty() || (ReadFile(h, out->data(), static_cast<DWORD>(out->size()), &n, nullptr) && n == out->size());
    }
    CloseHandle(h);
    return ok;
}

// The files in `dir` matching `pattern` that changed since `since`, by name.
std::vector<std::wstring> changedSince(const std::wstring& dir, const wchar_t* pattern, const FILETIME& since) {
    std::vector<std::wstring> names;
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileExW((dir + L"\\" + pattern).c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    if (f == INVALID_HANDLE_VALUE) return names;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && CompareFileTime(&fd.ftLastWriteTime, &since) >= 0)
            names.push_back(fd.cFileName);
    } while (FindNextFileW(f, &fd));
    FindClose(f);
    std::sort(names.begin(), names.end());
    return names;
}

}  // namespace

std::wstring downloadsDir() {
    PWSTR p = nullptr;
    std::wstring d;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &p))) d = p;
    CoTaskMemFree(p);
    return d.empty() ? appDir() : d;
}

// A plain ZIP: each file stored (method 0) with its CRC, its UTF-8 name (flag bit 11) and its time in DOS form, then
// the central directory. One file at a time is held in memory.
bool packLogs(const std::wstring& zip, int days, const std::string& about, LogPack* out) {
    Stopwatch sw;
    *out = LogPack{};
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER t{};
    t.LowPart = now.dwLowDateTime;
    t.HighPart = now.dwHighDateTime;
    t.QuadPart -= static_cast<uint64_t>(days) * 24 * 3600 * 10000000ull;
    const FILETIME since{t.LowPart, t.HighPart};

    const std::wstring tmp = zip + L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        out->error = "cannot create the ZIP (" + hrHex(HRESULT_FROM_WIN32(GetLastError())) + ")";
        out->ms = sw.ms();
        return false;
    }
    bool ok = true;
    DWORD writeErr = 0;   // of the first write that failed
    uint64_t offset = 0, total = 0;
    std::string central;
    auto write = [&](const std::string& s) {
        DWORD n = 0;
        if (ok && !(WriteFile(h, s.data(), static_cast<DWORD>(s.size()), &n, nullptr) && n == s.size())) {
            ok = false;
            writeErr = GetLastError() ? GetLastError() : ERROR_WRITE_FAULT;
        }
        offset += s.size();
    };
    auto add = [&](const std::string& name, const std::string& data, const FILETIME& written) {
        WORD date = 0x21, time = 0;   // 1980-01-01 when the time cannot be told
        FILETIME local;
        if (FileTimeToLocalFileTime(&written, &local)) FileTimeToDosDateTime(&local, &date, &time);
        const uint32_t crc = crc32(data), size = static_cast<uint32_t>(data.size()), at = static_cast<uint32_t>(offset);
        std::string head;
        put32(head, 0x04034B50);   // local file header
        put16(head, 20);           // version needed: 2.0
        put16(head, 0x0800);       // the name is UTF-8
        put16(head, 0);            // stored
        put16(head, time);
        put16(head, date);
        put32(head, crc);
        put32(head, size);
        put32(head, size);
        put16(head, static_cast<uint32_t>(name.size()));
        put16(head, 0);
        head += name;
        put32(central, 0x02014B50);   // its entry in the central directory
        put16(central, 20);           // made by: MS-DOS / 2.0
        put16(central, 20);
        put16(central, 0x0800);
        put16(central, 0);
        put16(central, time);
        put16(central, date);
        put32(central, crc);
        put32(central, size);
        put32(central, size);
        put16(central, static_cast<uint32_t>(name.size()));
        put16(central, 0);   // extra
        put16(central, 0);   // comment
        put16(central, 0);   // disk
        put16(central, 0);   // internal attributes
        put32(central, 0);   // external attributes
        put32(central, at);
        central += name;
        write(head);
        write(data);
        ++out->files;
    };
    auto addFile = [&](const std::wstring& path, const std::string& name) {
        std::string data;
        FILETIME written{};
        if (!readAll(path, &data, &written) || total + data.size() > kTotalLimit) {
            ++out->skipped;
            return;
        }
        total += data.size();
        add(name, data, written);
    };
    const std::wstring base = appDir();
    for (const auto& n : changedSince(base + L"\\logs", L"rec-cue-*.log", since))
        addFile(base + L"\\logs\\" + n, "logs/" + u8(n));
    for (const auto& n : changedSince(base + L"\\sessions", L"*.json", since))
        addFile(base + L"\\sessions\\" + n, "sessions/" + u8(n));
    addFile(configPath(), "config.ini");
    add("about.json", about, now);

    std::string end;
    const uint32_t entries = static_cast<uint32_t>(std::min(out->files, 0xFFFF));
    const uint32_t start = static_cast<uint32_t>(offset);
    put32(end, 0x06054B50);   // end of the central directory
    put16(end, 0);
    put16(end, 0);
    put16(end, entries);
    put16(end, entries);
    put32(end, static_cast<uint32_t>(central.size()));
    put32(end, start);
    put16(end, 0);
    write(central);
    write(end);
    CloseHandle(h);
    if (!ok) {
        out->error = "cannot write the ZIP (" + hrHex(HRESULT_FROM_WIN32(writeErr)) + ")";
    } else if (!MoveFileExW(tmp.c_str(), zip.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        out->error = "cannot rename the ZIP (" + hrHex(HRESULT_FROM_WIN32(GetLastError())) + ")";
        ok = false;
    }
    if (!ok) DeleteFileW(tmp.c_str());
    out->bytes = ok ? offset : 0;
    out->ms = sw.ms();
    return ok;
}

}  // namespace rc
