// Rec Cue! — しおり's lines (see shiori.h). The file's format is described at its top.
#include "shiori.h"

#include <algorithm>
#include <cmath>
#include <cwchar>

namespace rc {

namespace {

const char* const kSceneNames[] = {
    "start",    "back",  "talk",   "tip",      "check",       "check_mic", "check_sys", "check_quiet", "check_end", "starting",
    "rec",      "rec_on", "audio", "audio_on", "shot",        "shot_idle", "warn_screen", "warn_sys",  "warn_mic",  "warn_nosys",
    "recover",  "saving", "saved", "fail_start", "fail_save", "fail_shot", "pin_on",    "pin_off"};
static_assert(sizeof(kSceneNames) / sizeof(kSceneNames[0]) == static_cast<size_t>(Scene::Count), "a name per scene");

enum : uint16_t {
    kVideo = 1 << 0, kAudio = 1 << 1,                       // the kind of recording
    kShort = 1 << 2, kLong = 1 << 3, kShots = 1 << 4,       // saved: under 1 min / 1 h or more / with screenshots
    kAuto = 1 << 5,                                         // saved: it stopped by itself
    kNewYear = 1 << 6, kYearEnd = 1 << 7,                   // Jan 1-3 / Dec 28-31
    kNeedRec = 1 << 8, kNeedAudio = 1 << 9, kNeedShot = 1 << 10,   // names that shortcut ({rec_key} ...)
    kClip = 1 << 11,                                        // the screenshot went to the clipboard too
};
constexpr uint16_t kPrioFlags = kAuto | kNewYear | kYearEnd;   // with @n=, these go before the ordinary lines
constexpr uint16_t kMoreFlags = kShort | kLong | kShots;       // ... and these are said twice as often

enum class Kind { Band, Day, Month, Flag };
struct Tag {
    const wchar_t* name;
    Kind kind;
    uint16_t bits;
};
constexpr uint16_t M(int a, int b = 0, int c = 0) {
    return static_cast<uint16_t>((1 << a) | (b ? 1 << b : 0) | (c ? 1 << c : 0));
}
const Tag kTags[] = {
    // times of day: early 4-7, morning 7-11, noon 11-13, afternoon 13-17, evening 17-19, night 19-23, late 23-4
    {L"early", Kind::Band, 1 << 0}, {L"morning", Kind::Band, 1 << 1}, {L"noon", Kind::Band, 1 << 2},
    {L"afternoon", Kind::Band, 1 << 3}, {L"evening", Kind::Band, 1 << 4}, {L"night", Kind::Band, 1 << 5},
    {L"late", Kind::Band, 1 << 6},
    {L"sun", Kind::Day, 1 << 0}, {L"mon", Kind::Day, 1 << 1}, {L"tue", Kind::Day, 1 << 2}, {L"wed", Kind::Day, 1 << 3},
    {L"thu", Kind::Day, 1 << 4}, {L"fri", Kind::Day, 1 << 5}, {L"sat", Kind::Day, 1 << 6},
    {L"weekday", Kind::Day, 0x3E}, {L"weekend", Kind::Day, 0x41},
    {L"jan", Kind::Month, M(1)}, {L"feb", Kind::Month, M(2)}, {L"mar", Kind::Month, M(3)}, {L"apr", Kind::Month, M(4)},
    {L"may", Kind::Month, M(5)}, {L"jun", Kind::Month, M(6)}, {L"jul", Kind::Month, M(7)}, {L"aug", Kind::Month, M(8)},
    {L"sep", Kind::Month, M(9)}, {L"oct", Kind::Month, M(10)}, {L"nov", Kind::Month, M(11)}, {L"dec", Kind::Month, M(12)},
    {L"spring", Kind::Month, M(3, 4, 5)}, {L"summer", Kind::Month, M(6, 7, 8)}, {L"autumn", Kind::Month, M(9, 10, 11)},
    {L"winter", Kind::Month, M(12, 1, 2)},
    {L"video", Kind::Flag, kVideo}, {L"audio", Kind::Flag, kAudio}, {L"short", Kind::Flag, kShort},
    {L"long", Kind::Flag, kLong}, {L"shots", Kind::Flag, kShots}, {L"auto", Kind::Flag, kAuto},
    {L"newyear", Kind::Flag, kNewYear}, {L"yearend", Kind::Flag, kYearEnd}, {L"clip", Kind::Flag, kClip},
};

// {names} a line may use, and the shortcut each one needs
struct Field {
    const wchar_t* name;
    uint16_t needs;
};
const Field kFields[] = {{L"n", 0},           {L"t", 0},          {L"mb", 0},          {L"shots", 0},
                         {L"rec_key", kNeedRec}, {L"audio_key", kNeedAudio}, {L"shot_key", kNeedShot}};

int bandOf(int hour) {
    if (hour >= 4 && hour < 7) return 0;
    if (hour >= 7 && hour < 11) return 1;
    if (hour >= 11 && hour < 13) return 2;
    if (hour >= 13 && hour < 17) return 3;
    if (hour >= 17 && hour < 19) return 4;
    if (hour >= 19 && hour < 23) return 5;
    return 6;
}

// 45 秒 / 12 分 / 1 時間 / 1 時間 5 分
std::wstring span(double sec) {
    const long s = std::lround(std::max(0.0, sec));
    if (s < 60) return std::to_wstring(s) + L" 秒";
    const long m = s / 60;
    if (m < 60) return std::to_wstring(m) + L" 分";
    const long h = m / 60, r = m % 60;
    return std::to_wstring(h) + L" 時間" + (r ? L" " + std::to_wstring(r) + L" 分" : std::wstring());
}

bool space(wchar_t c) { return c == L' ' || c == L'\t'; }

}  // namespace

const char* sceneName(Scene s) {
    const int i = static_cast<int>(s);
    return i >= 0 && i < static_cast<int>(Scene::Count) ? kSceneNames[i] : "?";
}

Lines::Lines() {
    rng_ = static_cast<uint64_t>(qpcNow()) ^ (static_cast<uint64_t>(GetCurrentProcessId()) << 32) ^ 0x9E3779B97F4A7C15ull;
    if (!rng_) rng_ = 1;
}

uint64_t Lines::next() {   // xorshift64*
    rng_ ^= rng_ >> 12;
    rng_ ^= rng_ << 25;
    rng_ ^= rng_ >> 27;
    return rng_ * 0x2545F4914F6CDD1Dull;
}

bool Lines::load(HINSTANCE inst, std::vector<std::string>* problems) {
    text_.clear();
    lines_.clear();
    for (auto& v : byScene_) v.clear();
    HRSRC res = FindResourceW(inst, MAKEINTRESOURCEW(kResLines), MAKEINTRESOURCEW(10));   // RT_RCDATA
    HGLOBAL h = res ? LoadResource(inst, res) : nullptr;
    const char* p = h ? static_cast<const char*>(LockResource(h)) : nullptr;
    int bytes = res ? static_cast<int>(SizeofResource(inst, res)) : 0;
    if (!p || bytes <= 0) return false;
    if (bytes >= 3 && static_cast<unsigned char>(p[0]) == 0xEF && static_cast<unsigned char>(p[1]) == 0xBB &&
        static_cast<unsigned char>(p[2]) == 0xBF) {
        p += 3;
        bytes -= 3;
    }
    const int chars = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, p, bytes, nullptr, 0);
    if (chars <= 0) return false;
    text_.resize(static_cast<size_t>(chars));
    MultiByteToWideChar(CP_UTF8, 0, p, bytes, text_.data(), chars);
    auto problem = [&](int no, const std::string& what) {
        if (problems) problems->push_back(fmt("shiori-lines.txt:%d: ", no) + what);
    };
    int scene = -1, no = 0;
    size_t b = 0;
    while (b < text_.size()) {
        size_t e = text_.find(L'\n', b);
        if (e == std::wstring::npos) e = text_.size();
        const size_t after = e + 1;
        ++no;
        while (b < e && space(text_[b])) ++b;
        while (e > b && (space(text_[e - 1]) || text_[e - 1] == L'\r')) --e;
        if (b == e || text_[b] == L'#') {
            b = after;
            continue;
        }
        if (text_[b] == L'[' && text_[e - 1] == L']') {
            const std::string name = u8(text_.substr(b + 1, e - b - 2));
            scene = -1;
            for (int i = 0; i < static_cast<int>(Scene::Count); ++i)
                if (name == kSceneNames[i]) scene = i;
            if (scene < 0) problem(no, "unknown scene [" + name + "] (its lines are skipped)");
            b = after;
            continue;
        }
        Line l;
        l.source = no;
        bool ok = scene >= 0;
        while (ok && b < e && text_[b] == L'@') {
            size_t t = b + 1;
            while (t < e && !space(text_[t])) ++t;
            const std::wstring tag = text_.substr(b + 1, t - b - 1);
            if (tag.rfind(L"n=", 0) == 0 && tag.size() > 2) {
                l.n = static_cast<int16_t>(std::min(9999, _wtoi(tag.c_str() + 2)));
            } else {
                const Tag* found = nullptr;
                for (const Tag& k : kTags)
                    if (tag == k.name) found = &k;
                if (!found) {
                    problem(no, "unknown tag @" + u8(tag));
                    ok = false;
                    break;
                }
                switch (found->kind) {
                    case Kind::Band: l.bands = static_cast<uint8_t>(l.bands | found->bits); break;
                    case Kind::Day: l.days = static_cast<uint8_t>(l.days | found->bits); break;
                    case Kind::Month: l.months = static_cast<uint16_t>(l.months | found->bits); break;
                    case Kind::Flag: l.flags = static_cast<uint16_t>(l.flags | found->bits); break;
                }
            }
            b = t;
            while (b < e && space(text_[b])) ++b;
        }
        for (size_t q = text_.find(L'{', b); ok && q < e; q = text_.find(L'{', q + 1)) {
            const size_t z = text_.find(L'}', q);
            const std::wstring name = z < e ? text_.substr(q + 1, z - q - 1) : std::wstring();
            const Field* found = nullptr;
            for (const Field& f : kFields)
                if (name == f.name) found = &f;
            if (!found) {
                problem(no, "unknown {" + u8(name) + "}");
                ok = false;
                break;
            }
            l.flags = static_cast<uint16_t>(l.flags | found->needs);
        }
        if (ok && b == e) {
            problem(no, "tags without a line");
            ok = false;
        }
        if (ok && lines_.size() < 0xFFFF) {
            l.scene = static_cast<Scene>(scene);
            l.at = static_cast<uint32_t>(b);
            l.len = static_cast<uint32_t>(e - b);
            byScene_[scene].push_back(static_cast<uint16_t>(lines_.size()));
            lines_.push_back(l);
        }
        b = after;
    }
    return !lines_.empty();
}

bool Lines::fits(const Line& l, const SayContext& c, const SYSTEMTIME& now) const {
    if (l.bands && !(l.bands & (1 << bandOf(now.wHour)))) return false;
    if (l.days && !(l.days & (1 << now.wDayOfWeek))) return false;
    if (l.months && !(l.months & (1 << now.wMonth))) return false;
    if (l.n >= 0 && l.n != c.n) return false;
    const uint16_t f = l.flags;
    if (((f & kVideo) && c.audio) || ((f & kAudio) && !c.audio)) return false;
    if (((f & kShort) && c.sec >= 60) || ((f & kLong) && c.sec < 3600) || ((f & kShots) && c.shots < 1)) return false;
    if (((f & kAuto) && !c.autoStop) || ((f & kClip) && !c.clip)) return false;
    if ((f & kNewYear) && !(now.wMonth == 1 && now.wDay <= 3)) return false;
    if ((f & kYearEnd) && !(now.wMonth == 12 && now.wDay >= 28)) return false;
    if (((f & kNeedRec) && c.recKey.empty()) || ((f & kNeedAudio) && c.audioKey.empty()) ||
        ((f & kNeedShot) && c.shotKey.empty()))
        return false;
    return true;
}

std::wstring Lines::fill(const Line& l, const SayContext& c) const {
    std::wstring s = text_.substr(l.at, l.len);
    for (size_t q = s.find(L'{'); q != std::wstring::npos; q = s.find(L'{', q)) {
        const size_t z = s.find(L'}', q);
        if (z == std::wstring::npos) break;
        const std::wstring name = s.substr(q + 1, z - q - 1);
        std::wstring v;
        if (name == L"n") v = std::to_wstring(c.n);
        else if (name == L"t") v = span(c.sec);
        else if (name == L"shots") v = std::to_wstring(c.shots);
        else if (name == L"rec_key") v = c.recKey;
        else if (name == L"audio_key") v = c.audioKey;
        else if (name == L"shot_key") v = c.shotKey;
        else if (name == L"mb") {
            wchar_t b[32];
            swprintf(b, 32, L"%.1f", c.mb);
            v = b;
        }
        s.replace(q, z - q + 1, v);
        q += v.size();
    }
    return s;
}

std::wstring Lines::pick(Scene s, const SayContext& c) {
    const int k = static_cast<int>(s);
    if (k < 0 || k >= static_cast<int>(Scene::Count)) return L"";
    SYSTEMTIME now;
    if (fixedClock_) now = clock_;
    else GetLocalTime(&now);
    // the lines that fit now; one meant for this very moment (a milestone, a special day, an automatic stop) goes first
    std::vector<uint16_t> fit;
    fit.reserve(byScene_[k].size());
    bool prio = false;
    for (uint16_t i : byScene_[k]) {
        const Line& l = lines_[i];
        if (!fits(l, c, now)) continue;
        const bool p = l.n >= 0 || (l.flags & kPrioFlags);
        if (p && !prio) {
            fit.clear();
            prio = true;
        }
        if (p == prio) fit.push_back(i);
    }
    if (fit.empty()) return L"";
    // not one of the lines said in the last (half as many as fit) times in this scene
    const uint32_t count = said_[k], window = static_cast<uint32_t>(fit.size() / 2);
    std::vector<uint16_t> fresh;
    fresh.reserve(fit.size());
    for (uint16_t i : fit)
        if (lines_[i].said == 0 || count - lines_[i].said >= window) fresh.push_back(i);
    if (fresh.empty()) fresh = fit;
    // a line for this time of day, day, season or kind of result is twice as likely as a general one
    auto weight = [&](const Line& l) { return l.bands || l.days || l.months || (l.flags & kMoreFlags) ? 2u : 1u; };
    uint32_t total = 0;
    for (uint16_t i : fresh) total += weight(lines_[i]);
    uint32_t r = static_cast<uint32_t>(next() % total);
    uint16_t chosen = fresh.back();
    for (uint16_t i : fresh) {
        const uint32_t w = weight(lines_[i]);
        if (r < w) {
            chosen = i;
            break;
        }
        r -= w;
    }
    Line& l = lines_[chosen];
    l.said = ++said_[k];
    lastSource_ = l.source;
    return fill(l, c);
}

bool Lines::milestone(Scene s, int n) const {
    const int k = static_cast<int>(s);
    if (k < 0 || k >= static_cast<int>(Scene::Count)) return false;
    for (uint16_t i : byScene_[k])
        if (lines_[i].n == n) return true;
    return false;
}

std::wstring Lines::longestAt(size_t i) const {
    SayContext c;
    c.n = 100;
    c.shots = 100;
    c.sec = 12 * 3600 + 59 * 60;   // 12 時間 59 分
    c.mb = 9999.9;
    c.recKey = c.audioKey = c.shotKey = L"Ctrl+Alt+Shift+F12";
    return fill(lines_[i], c);
}

}  // namespace rc
