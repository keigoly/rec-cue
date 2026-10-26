#include "config.h"

#include <shlobj.h>

#include <cstdlib>

namespace rc {

static const char* kDefaultIni =
    "; Rec Cue! の設定 (UTF-8)。; で始まる行はコメントです。\r\n"
    "; 変更は次の録画開始から反映されます。マイクと PC の音声は画面のリストからも選べます。\r\n"
    "\r\n"
    "; 保存先。空なら「ビデオ\\画面録画」\r\n"
    "out_dir=\r\n"
    "; ファイル名の先頭 (後ろに 2026-10-20 193001 のような開始時刻が付く)\r\n"
    "name_prefix=Rec Cue\r\n"
    "\r\n"
    "; 1 秒あたりのコマ数 (1〜60)。会議や資料なら 10〜15 で十分です\r\n"
    "fps=15\r\n"
    "; 映像のビットレート (kbps)。1080p で 2000 なら 1 時間でおよそ 0.9 GB\r\n"
    "video_kbps=2000\r\n"
    "; 音声のビットレート (kbps)。96 / 128 / 160 / 192 (録画の AAC と、録音だけの MP3 / M4A)\r\n"
    "audio_kbps=128\r\n"
    "; 録音だけの形式: mp3 (小さく、どこでも再生できる) / wav (無圧縮。1 時間でおよそ 690 MB・5 時間 47 分まで)\r\n"
    ";   / m4a (AAC)。録音の帯の切り替えでも選べます。録画はいつも MP4 です\r\n"
    "audio_format=mp3\r\n"
    "; 録る画面。0 = メインの画面、1 以降 = ほかの画面 (窓の「画面」の欄で選ぶと書き換わる。欄の番号から 1 を引いた数)\r\n"
    "monitor=0\r\n"
    "; マウスカーソルを映す (1 / 0)\r\n"
    "cursor=1\r\n"
    "\r\n"
    "; マイク: auto (ヘッドセット → 通話用の既定 → 既定 の順。仮想ミキサーは音が来ているときだけ) / default (既定のマイク)\r\n"
    ";   / communications (通話用の既定) / none / 機器名の一部\r\n"
    "mic=auto\r\n"
    "; PC の音声 (相手の声): auto (既定のスピーカーと、アプリが音を出している再生機器をすべて。録画中に増えた機器も足す)\r\n"
    ";   / default (既定のスピーカー) / communications / none / 機器名の一部。複数の機器を混ぜるときは | で区切る。\r\n"
    ";   同じ音が 2 つの機器に出る組み合わせ (共有モードで出力する仮想ミキサーとその出力先) は二重になるので避ける\r\n"
    "system=auto\r\n"
    "; 音量の補正 (dB)。-20〜20\r\n"
    "mic_gain_db=0\r\n"
    "system_gain_db=0\r\n"
    "\r\n"
    "; 保存形式: fmp4 (途中で落ちてもそこまで再生できる) / mp4\r\n"
    "container=fmp4\r\n"
    "; GPU のエンコーダーを使う (1 / 0)\r\n"
    "hw_encode=1\r\n"
    "; 音声を揃えるための待ち時間 (ms)。音が途切れるときだけ大きくする\r\n"
    "latency_ms=150\r\n"
    "; 録画の開始と停止のショートカット。none で無効\r\n"
    "hotkey=Ctrl+Alt+Shift+R\r\n"
    "\r\n"
    "; 窓をほかのアプリより手前に固定する (1 / 0)。画面のピンのボタンでも切り替えられます\r\n"
    "topmost=0\r\n"
    "; スクショ (録る画面と同じ画面。Rec Cue! の窓は写りません) の保存先。空なら録画と同じ場所\r\n"
    "shot_dir=\r\n"
    "; スクショをクリップボードにもコピーする (1 / 0)\r\n"
    "shot_clipboard=1\r\n"
    "; スクショのショートカット。none で無効\r\n"
    "shot_hotkey=Ctrl+Alt+Shift+S\r\n"
    "; 録音だけ (画面なし・audio_format の形式) の開始と停止のショートカット。none で無効\r\n"
    "audio_hotkey=Ctrl+Alt+Shift+A\r\n"
    "\r\n"
    "; 設定のメニュー (右上の歯車) から開くページ\r\n"
    "github_url=https://github.com/keigoly/rec-cue\r\n"
    "site_url=https://official.keigoly.jp/\r\n";

std::wstring configPath() { return appDir() + L"\\config.ini"; }

std::wstring Config::resolvedOutDir() const {
    if (!outDir.empty()) {
        wchar_t buf[MAX_PATH * 2];
        DWORD n = ExpandEnvironmentStringsW(outDir.c_str(), buf, MAX_PATH * 2);
        if (n > 0 && n <= MAX_PATH * 2) return buf;
        return outDir;
    }
    PWSTR p = nullptr;
    std::wstring d;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Videos, 0, nullptr, &p))) d = p;
    CoTaskMemFree(p);
    if (d.empty()) d = appDir();
    return d + L"\\画面録画";
}

std::wstring Config::resolvedShotDir() const {
    if (shotDir.empty()) return resolvedOutDir();
    wchar_t buf[MAX_PATH * 2];
    DWORD n = ExpandEnvironmentStringsW(shotDir.c_str(), buf, MAX_PATH * 2);
    return n > 0 && n <= MAX_PATH * 2 ? std::wstring(buf) : shotDir;
}

static std::string readFile(const std::wstring& path, bool* ok) {
    *ok = false;
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return {};
    std::string s;
    LARGE_INTEGER sz{};
    if (GetFileSizeEx(h, &sz) && sz.QuadPart < (1 << 20)) {
        s.resize(static_cast<size_t>(sz.QuadPart));
        DWORD r = 0;
        if (ReadFile(h, s.data(), static_cast<DWORD>(s.size()), &r, nullptr)) {
            s.resize(r);
            *ok = true;
        }
    }
    CloseHandle(h);
    if (s.size() >= 3 && static_cast<unsigned char>(s[0]) == 0xEF && static_cast<unsigned char>(s[1]) == 0xBB &&
        static_cast<unsigned char>(s[2]) == 0xBF)
        s.erase(0, 3);
    return s;
}

static bool writeFile(const std::wstring& path, const std::string& data) {
    std::wstring tmp = path + L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD w = 0;
    BOOL ok = WriteFile(h, data.data(), static_cast<DWORD>(data.size()), &w, nullptr);
    CloseHandle(h);
    if (!ok || w != data.size()) return false;
    return MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}

static std::wstring trim(const std::wstring& s) {
    size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return {};
    size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}

static std::vector<std::wstring> lines(const std::wstring& text) {
    std::vector<std::wstring> out;
    size_t p = 0;
    while (p <= text.size()) {
        size_t q = text.find(L'\n', p);
        if (q == std::wstring::npos) { out.push_back(text.substr(p)); break; }
        out.push_back(text.substr(p, q - p));
        p = q + 1;
    }
    return out;
}

Config loadConfig(std::vector<std::string>* warn) {
    Config c;
    ensureDir(appDir());
    bool ok = false;
    std::string raw = readFile(configPath(), &ok);
    if (!ok) {
        writeFile(configPath(), kDefaultIni);
        raw = kDefaultIni;
    }
    auto bad = [&](const std::wstring& k, const std::wstring& v) {
        if (warn) warn->push_back("config: bad value " + u8(k) + "=" + u8(v));
    };
    for (auto& l : lines(wide(raw))) {
        std::wstring t = trim(l);
        if (t.empty() || t[0] == L';' || t[0] == L'#') continue;
        size_t eq = t.find(L'=');
        if (eq == std::wstring::npos) continue;
        std::wstring k = lower(trim(t.substr(0, eq)));
        std::wstring v = trim(t.substr(eq + 1));
        auto toInt = [&](int lo, int hi, int& dst) {
            wchar_t* e = nullptr;
            long x = wcstol(v.c_str(), &e, 10);
            if (e == v.c_str() || x < lo || x > hi) bad(k, v); else dst = static_cast<int>(x);
        };
        auto toDb = [&](double& dst) {
            wchar_t* e = nullptr;
            double x = wcstod(v.c_str(), &e);
            if (e == v.c_str() || x < -20 || x > 20) bad(k, v); else dst = x;
        };
        auto toBool = [&](bool& dst) {
            std::wstring lv = lower(v);
            if (lv == L"1" || lv == L"true" || lv == L"yes" || lv == L"on") dst = true;
            else if (lv == L"0" || lv == L"false" || lv == L"no" || lv == L"off") dst = false;
            else bad(k, v);
        };
        if (k == L"out_dir") c.outDir = v;
        else if (k == L"name_prefix") { if (!v.empty()) c.namePrefix = v; }
        else if (k == L"fps") toInt(1, 60, c.fps);
        else if (k == L"video_kbps") toInt(200, 50000, c.videoKbps);
        else if (k == L"audio_kbps") {
            int x = c.audioKbps;
            toInt(96, 192, x);
            if (x == 96 || x == 128 || x == 160 || x == 192) c.audioKbps = x; else bad(k, v);
        }
        else if (k == L"audio_format") {
            const std::wstring lv = lower(v);
            if (lv == L"mp3" || lv == L"wav" || lv == L"m4a") c.audioFormat = lv; else bad(k, v);
        }
        else if (k == L"monitor") toInt(0, 16, c.monitor);
        else if (k == L"mic") c.mic = v.empty() ? L"none" : v;
        else if (k == L"system") c.system = v.empty() ? L"none" : v;
        else if (k == L"mic_gain_db") toDb(c.micGainDb);
        else if (k == L"system_gain_db") toDb(c.systemGainDb);
        else if (k == L"cursor") toBool(c.cursor);
        else if (k == L"container") {
            std::wstring lv = lower(v);
            if (lv == L"fmp4" || lv == L"mp4") c.container = lv; else bad(k, v);
        }
        else if (k == L"hw_encode") toBool(c.hwEncode);
        else if (k == L"latency_ms") toInt(40, 1000, c.latencyMs);
        else if (k == L"hotkey") c.hotkey = v;
        else if (k == L"topmost") toBool(c.topmost);
        else if (k == L"shot_dir") c.shotDir = v;
        else if (k == L"shot_clipboard") toBool(c.shotClipboard);
        else if (k == L"shot_hotkey") c.shotHotkey = v;
        else if (k == L"audio_hotkey") c.audioHotkey = v;
        else if (k == L"github_url") {   // the account page was the default before the repository existed (0.3.1-0.3.3)
            if (!v.empty() && v != L"https://github.com/keigoly") c.githubUrl = v;
        }
        else if (k == L"site_url") { if (!v.empty()) c.siteUrl = v; }
        else if (k == L"shiori_voice") {}   // 0.3.1 tried five voices; she speaks one way now (old files stay quiet)
        else if (warn) warn->push_back("config: unknown key " + u8(k));
    }
    return c;
}

bool saveConfigValue(const std::wstring& key, const std::wstring& value) {
    bool ok = false;
    std::string raw = readFile(configPath(), &ok);
    if (!ok) raw = kDefaultIni;
    std::wstring text = wide(raw);
    std::vector<std::wstring> ls = lines(text);
    bool done = false;
    for (auto& l : ls) {
        std::wstring t = trim(l);
        if (t.empty() || t[0] == L';' || t[0] == L'#') continue;
        size_t eq = t.find(L'=');
        if (eq == std::wstring::npos) continue;
        if (lower(trim(t.substr(0, eq))) == lower(key)) {
            bool cr = !l.empty() && l.back() == L'\r';
            l = key + L"=" + value + (cr ? L"\r" : L"");
            done = true;
            break;
        }
    }
    std::wstring out;
    for (size_t i = 0; i < ls.size(); ++i) {
        out += ls[i];
        if (i + 1 < ls.size()) out += L"\n";
    }
    if (!done) {
        if (!out.empty() && out.back() != L'\n') out += L"\r\n";
        out += key + L"=" + value + L"\r\n";
    }
    return writeFile(configPath(), u8(out));
}

std::string configJson(const Config& c) {
    return fmt("{\"out_dir\":%s,\"name_prefix\":%s,\"fps\":%d,\"video_kbps\":%d,\"audio_kbps\":%d,\"audio_format\":%s,"
               "\"monitor\":%d,\"mic\":%s,\"system\":%s,\"mic_gain_db\":%.1f,\"system_gain_db\":%.1f,\"cursor\":%s,"
               "\"container\":%s,\"hw_encode\":%s,\"latency_ms\":%d,\"hotkey\":%s,\"topmost\":%s,\"shot_dir\":%s,"
               "\"shot_clipboard\":%s,\"shot_hotkey\":%s,\"audio_hotkey\":%s}",
               json(c.resolvedOutDir()).c_str(), json(c.namePrefix).c_str(), c.fps, c.videoKbps, c.audioKbps,
               json(c.audioFormat).c_str(), c.monitor, json(c.mic).c_str(), json(c.system).c_str(), c.micGainDb,
               c.systemGainDb, c.cursor ? "true" : "false", json(c.container).c_str(), c.hwEncode ? "true" : "false",
               c.latencyMs, json(c.hotkey).c_str(), c.topmost ? "true" : "false", json(c.resolvedShotDir()).c_str(),
               c.shotClipboard ? "true" : "false", json(c.shotHotkey).c_str(), json(c.audioHotkey).c_str());
}

bool parseHotkey(const std::wstring& s, UINT* mods, UINT* vk) {
    *mods = 0;
    *vk = 0;
    std::wstring t = lower(trim(s));
    if (t.empty() || t == L"none" || t == L"off") return false;
    size_t p = 0;
    while (p <= t.size()) {
        size_t q = t.find(L'+', p);
        std::wstring part = trim(t.substr(p, q == std::wstring::npos ? std::wstring::npos : q - p));
        if (part == L"ctrl" || part == L"control") *mods |= MOD_CONTROL;
        else if (part == L"alt") *mods |= MOD_ALT;
        else if (part == L"shift") *mods |= MOD_SHIFT;
        else if (part == L"win") *mods |= MOD_WIN;
        else if (part.size() == 1 && ((part[0] >= L'a' && part[0] <= L'z') || (part[0] >= L'0' && part[0] <= L'9')))
            *vk = static_cast<UINT>(towupper(part[0]));
        else if (part.size() >= 2 && part[0] == L'f') {
            int n = _wtoi(part.c_str() + 1);
            if (n >= 1 && n <= 24) *vk = VK_F1 + static_cast<UINT>(n - 1);
        }
        if (q == std::wstring::npos) break;
        p = q + 1;
    }
    return *vk != 0;
}

}  // namespace rc
