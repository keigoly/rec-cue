// Rec Cue! — settings file (%USERPROFILE%\.rec-cue\config.ini, UTF-8, key=value).
#pragma once

#include "common.h"

namespace rc {

struct Config {
    std::wstring outDir;                 // empty = <Videos>\画面録画
    std::wstring namePrefix = L"Rec Cue";
    int fps = 15;
    int videoKbps = 2000;
    int audioKbps = 128;
    int monitor = 0;                     // 0 = primary
    std::wstring mic = L"auto";          // auto | default | communications | none | part of a device name
    std::wstring system = L"auto";       // auto | default | communications | none | names separated by |
    double micGainDb = 0;
    double systemGainDb = 0;
    bool cursor = true;
    std::wstring container = L"fmp4";    // fmp4 (playable after a crash) | mp4
    int latencyMs = 150;
    std::wstring hotkey = L"Ctrl+Alt+Shift+R";
    bool hwEncode = true;
    bool topmost = false;                // keep the window above other windows (the pin button)
    std::wstring shotDir;                // empty = same folder as the recordings
    bool shotClipboard = true;           // also copy each screenshot to the clipboard
    std::wstring shotHotkey = L"Ctrl+Alt+Shift+S";
    std::wstring audioHotkey = L"Ctrl+Alt+Shift+A";   // start / stop recording audio only
    std::wstring githubUrl = L"https://github.com/keigoly/rec-cue";   // the settings menu links
    std::wstring siteUrl = L"https://official.keigoly.jp/";

    std::wstring resolvedOutDir() const;
    std::wstring resolvedShotDir() const;
};

std::wstring configPath();
// Reads the file, creating it with defaults when missing. Unknown keys / bad values go to warnings.
Config loadConfig(std::vector<std::string>* warnings = nullptr);
// Rewrites one key in place (keeps comments and other keys).
bool saveConfigValue(const std::wstring& key, const std::wstring& value);
std::string configJson(const Config& c);
bool parseHotkey(const std::wstring& s, UINT* mods, UINT* vk);

}  // namespace rc
