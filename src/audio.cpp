#include "audio.h"

#include <audioclient.h>
#include <audiopolicy.h>
#include <avrt.h>
#include <mmdeviceapi.h>
#include <propidl.h>

#include <algorithm>
#include <chrono>
#include <map>
#include <set>
#include <cmath>
#include <cstring>

namespace rc {

namespace {

// Defined here to avoid depending on ksmedia.h / functiondiscoverykeys GUID linkage.
const GUID kSubtypeFloat = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
const PROPERTYKEY kFriendlyName = {{0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};

constexpr int64_t kRing = 1 << 17;          // 2.7 s of 48 kHz stereo per source
constexpr uint32_t kChunk = 480;            // 10 ms mixing step
constexpr uint32_t kSinkFrames = 2400;      // 50 ms per AAC input sample
constexpr float kSilent = 0.00178f;         // -55 dBFS

std::wstring devId(IMMDevice* d) {
    LPWSTR id = nullptr;
    std::wstring s;
    if (SUCCEEDED(d->GetId(&id)) && id) s = id;
    CoTaskMemFree(id);
    return s;
}

std::wstring devName(IMMDevice* d) {
    Com<IPropertyStore> ps;
    std::wstring s;
    if (FAILED(d->OpenPropertyStore(STGM_READ, ps.put()))) return s;
    PROPVARIANT pv;
    PropVariantInit(&pv);
    if (SUCCEEDED(ps->GetValue(kFriendlyName, &pv)) && pv.vt == VT_LPWSTR && pv.pwszVal) s = pv.pwszVal;
    PropVariantClear(&pv);
    return s;
}

Com<IMMDeviceEnumerator> enumerator(std::string* err) {
    Com<IMMDeviceEnumerator> en;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                  en.put_void());
    if (FAILED(hr) && err) *err = "MMDeviceEnumerator " + hrHex(hr);
    return en;
}

std::wstring processExe(DWORD pid) {
    std::wstring s;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return wide(fmt("pid %lu", static_cast<unsigned long>(pid)));
    wchar_t b[MAX_PATH * 2];
    DWORD n = MAX_PATH * 2;
    if (QueryFullProcessImageNameW(h, 0, b, &n)) {
        s.assign(b, n);
        size_t k = s.find_last_of(L'\\');
        if (k != std::wstring::npos) s = s.substr(k + 1);
    }
    CloseHandle(h);
    return s;
}

std::vector<AudioDevice::App> sessionApps(IMMDevice* d) {
    std::vector<AudioDevice::App> v;
    Com<IAudioSessionManager2> m;
    Com<IAudioSessionEnumerator> e;
    if (FAILED(d->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, m.put_void()))) return v;
    if (FAILED(m->GetSessionEnumerator(e.put()))) return v;
    int n = 0;
    e->GetCount(&n);
    for (int i = 0; i < n; ++i) {
        Com<IAudioSessionControl> c;
        Com<IAudioSessionControl2> c2;
        if (FAILED(e->GetSession(i, c.put())) || FAILED(c.as(c2))) continue;
        AudioSessionState st = AudioSessionStateInactive;
        c2->GetState(&st);
        if (st == AudioSessionStateExpired) continue;
        AudioDevice::App a;
        a.active = st == AudioSessionStateActive;
        if (c2->IsSystemSoundsSession() == S_OK) {
            a.exe = L"(system sounds)";
        } else {
            DWORD pid = 0;
            c2->GetProcessId(&pid);
            a.exe = processExe(pid);
        }
        auto it = std::find_if(v.begin(), v.end(), [&](const AudioDevice::App& x) { return x.exe == a.exe; });
        if (it == v.end()) v.push_back(a);
        else it->active = it->active || a.active;
    }
    return v;
}

int db10(float lin) {
    if (lin <= 0.0000316f) return -900;  // floor at -90 dB
    return static_cast<int>(std::lround(200.0 * std::log10(static_cast<double>(lin))));
}

std::wstring trimW(const std::wstring& s) {
    size_t a = s.find_first_not_of(L" \t");
    if (a == std::wstring::npos) return {};
    size_t b = s.find_last_not_of(L" \t");
    return s.substr(a, b - a + 1);
}

}  // namespace

std::vector<AudioDevice> listAudioDevices(bool render, std::string* err, bool withApps) {
    std::vector<AudioDevice> v;
    Com<IMMDeviceEnumerator> en = enumerator(err);
    if (!en) return v;
    const EDataFlow flow = render ? eRender : eCapture;
    std::wstring defC, defM;
    {
        Com<IMMDevice> d;
        if (SUCCEEDED(en->GetDefaultAudioEndpoint(flow, eConsole, d.put()))) defC = devId(d.get());
    }
    {
        Com<IMMDevice> d;
        if (SUCCEEDED(en->GetDefaultAudioEndpoint(flow, eCommunications, d.put()))) defM = devId(d.get());
    }
    Com<IMMDeviceCollection> col;
    if (FAILED(en->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, col.put()))) return v;
    UINT n = 0;
    col->GetCount(&n);
    for (UINT i = 0; i < n; ++i) {
        Com<IMMDevice> d;
        if (FAILED(col->Item(i, d.put()))) continue;
        AudioDevice a;
        a.id = devId(d.get());
        a.name = devName(d.get());
        a.render = render;
        a.defConsole = !defC.empty() && a.id == defC;
        a.defComm = !defM.empty() && a.id == defM;
        if (withApps && render) a.apps = sessionApps(d.get());
        v.push_back(a);
    }
    return v;
}

std::wstring normName(const std::wstring& s) {
    std::wstring o = lower(s);
    for (size_t p = o.find(L'('); p != std::wstring::npos; p = o.find(L'(', p + 1)) {
        size_t q = p + 1;
        while (q < o.size() && iswdigit(o[q])) ++q;
        if (q > p + 1 && q + 1 < o.size() && o[q] == L'-' && o[q + 1] == L' ') o.erase(p + 1, q + 2 - (p + 1));
    }
    return o;
}

static bool isMixerApp(const std::wstring& exe) {
    const std::wstring e = lower(exe);
    return e.find(L"voicemeeter") != std::wstring::npos || e.find(L"vbaudio") != std::wstring::npos ||
           e == L"rec-cue.exe" || e == L"(system sounds)";
}

std::vector<AudioDevice> autoSystemDevices() {
    std::vector<AudioDevice> out;
    for (auto& d : listAudioDevices(true, nullptr, true)) {
        bool used = d.defConsole;
        for (auto& a : d.apps)
            if (!isMixerApp(a.exe)) used = true;
        if (used) out.push_back(d);
    }
    return out;
}

std::vector<AudioDevice> resolveDevices(const std::wstring& spec, bool render, std::vector<std::string>* notes) {
    std::vector<AudioDevice> out;
    std::string err;
    auto all = listAudioDevices(render, &err);
    if (!err.empty() && notes) notes->push_back(err);
    size_t p = 0;
    while (p <= spec.size()) {
        size_t q = spec.find(L'|', p);
        std::wstring tok = trimW(spec.substr(p, q == std::wstring::npos ? std::wstring::npos : q - p));
        std::wstring lt = lower(tok);
        const AudioDevice* hit = nullptr;
        if (lt.empty() || lt == L"none") {
            // nothing
        } else if (lt == L"default") {
            for (auto& d : all) if (d.defConsole) hit = &d;
        } else if (lt == L"communications") {
            for (auto& d : all) if (d.defComm) hit = &d;
        } else {
            const std::wstring nt = normName(tok);
            for (auto& d : all)
                if (normName(d.name) == nt) { hit = &d; break; }
            if (!hit)
                for (auto& d : all)
                    if (normName(d.name).find(nt) != std::wstring::npos) { hit = &d; break; }
        }
        if (!hit && !(lt.empty() || lt == L"none") && notes)
            notes->push_back(std::string(render ? "system" : "mic") + " device not found: " + u8(tok));
        if (hit && std::none_of(out.begin(), out.end(), [&](const AudioDevice& d) { return d.id == hit->id; }))
            out.push_back(*hit);
        if (q == std::wstring::npos) break;
        p = q + 1;
    }
    return out;
}

// ---- one capture stream ---------------------------------------------------

class AudioCapture {
public:
    std::string label;      // mic / sys1 / sys2 ...
    std::wstring name, id;
    bool isMic = false;
    float gain = 1.0f;
    std::atomic<bool> dead{false};
    bool released = false;
    std::string format;     // what WASAPI gave us
    // cumulative stats
    uint64_t packets = 0, frames = 0, disc = 0, silentPk = 0, late = 0, overflow = 0, jumps = 0, nudges = 0;
    // level stats: window (10 s log) and session
    float wPeak = 0, sPeak = 0;
    double wSs = 0, sSs = 0;
    uint64_t wN = 0, sN = 0, wSilent = 0, sSilent = 0, wChunks = 0, sChunks = 0;

    bool open(IMMDeviceEnumerator* en, const AudioDevice& d, bool loopback, std::string* err) {
        name = d.name;
        id = d.id;
        loopback_ = loopback;
        HRESULT hr = en->GetDevice(d.id.c_str(), dev_.put());
        if (SUCCEEDED(hr)) hr = dev_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, client_.put_void());
        if (FAILED(hr)) { *err = "activate " + hrHex(hr); return false; }
        WAVEFORMATEXTENSIBLE wf{};
        wf.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
        wf.Format.nChannels = 2;
        wf.Format.nSamplesPerSec = 48000;
        wf.Format.wBitsPerSample = 32;
        wf.Format.nBlockAlign = 8;
        wf.Format.nAvgBytesPerSec = 48000 * 8;
        wf.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
        wf.Samples.wValidBitsPerSample = 32;
        wf.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
        wf.SubFormat = kSubtypeFloat;
        const DWORD lb = loopback ? AUDCLNT_STREAMFLAGS_LOOPBACK : 0;
        hr = client_->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                 lb | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                                 10000000, 0, &wf.Format, nullptr);
        if (SUCCEEDED(hr)) {
            format = "48000 Hz 2ch float (converted by Windows)";
        } else {
            HRESULT first = hr;
            client_.reset();
            hr = dev_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, client_.put_void());
            WAVEFORMATEX* mix = nullptr;
            if (SUCCEEDED(hr)) hr = client_->GetMixFormat(&mix);
            if (FAILED(hr)) { *err = "mix format " + hrHex(hr); return false; }
            ch_ = mix->nChannels;
            rate_ = mix->nSamplesPerSec;
            block_ = mix->nBlockAlign;
            bool isFloat = mix->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
            if (mix->wFormatTag == WAVE_FORMAT_EXTENSIBLE && mix->cbSize >= 22)
                isFloat = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(mix)->SubFormat == kSubtypeFloat;
            if (isFloat) type_ = 0;
            else if (mix->wBitsPerSample == 16) type_ = 1;
            else if (mix->wBitsPerSample == 24) type_ = 2;
            else type_ = 3;
            hr = client_->Initialize(AUDCLNT_SHAREMODE_SHARED, lb, 10000000, 0, mix, nullptr);
            format = fmt("%u Hz %uch %s%u (own conversion; autoconvert %s)", rate_, ch_, isFloat ? "float" : "int",
                         mix->wBitsPerSample, hrHex(first).c_str());
            CoTaskMemFree(mix);
            if (FAILED(hr)) { *err = "initialize " + hrHex(hr); return false; }
            native_ = true;
            step_ = static_cast<double>(rate_) / 48000.0;
        }
        hr = client_->GetService(__uuidof(IAudioCaptureClient), cap_.put_void());
        if (FAILED(hr)) { *err = "capture client " + hrHex(hr); return false; }
        ring_.assign(static_cast<size_t>(kRing) * 2, 0.f);
        return true;
    }

    bool start(std::string* err) {
        HRESULT hr = client_->Start();
        if (FAILED(hr)) { *err = "start " + hrHex(hr); dead = true; return false; }
        return true;
    }

    void stop() {
        if (client_) client_->Stop();
    }

    // Drops the device objects and the ring (stats stay). Called on the engine thread.
    void release() {
        stop();
        cap_.reset();
        client_.reset();
        dev_.reset();
        std::vector<float>().swap(ring_);
        released = true;
    }

    // Moves every pending packet into the ring at its position on the shared timeline.
    void poll(int64_t start100, int64_t readPos, int64_t nowPos) {
        if (dead || !cap_) return;
        for (int guard = 0; guard < 256; ++guard) {
            UINT32 next = 0;
            HRESULT hr = cap_->GetNextPacketSize(&next);
            if (FAILED(hr)) { fail(hr); return; }
            if (next == 0) return;
            BYTE* data = nullptr;
            UINT32 n = 0;
            DWORD fl = 0;
            UINT64 devPos = 0, qpcPos = 0;
            hr = cap_->GetBuffer(&data, &n, &fl, &devPos, &qpcPos);
            if (hr == AUDCLNT_S_BUFFER_EMPTY) return;
            if (FAILED(hr)) { fail(hr); return; }
            packets++;
            frames += n;
            if (fl & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) disc++;
            const bool silent = (fl & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
            if (silent) silentPk++;
            const uint32_t outN = convert(data, n, silent);
            const bool tsOk = qpcPos != 0 && !(fl & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR);
            int64_t p;
            if (tsOk) {
                const int64_t computed = (static_cast<int64_t>(qpcPos) - start100) * 48000 / 10000000;
                if (!haveEnd_) {
                    p = computed;
                } else {
                    const int64_t d = computed - end_;
                    if (d > 1920 || d < -1920) { p = computed; jumps++; }      // gap (loopback idle) or stall
                    else if (d > 96) { p = end_ + 1; nudges++; }             // device clock drift: 1 sample at a time
                    else if (d < -96) { p = end_ - 1; nudges++; }
                    else p = end_;
                }
            } else {
                p = haveEnd_ ? end_ : std::max(readPos, nowPos - 960);
            }
            put(p, conv_.data(), outN, readPos);
            end_ = p + outN;
            haveEnd_ = true;
            cap_->ReleaseBuffer(n);
        }
    }

    // Reads [pos, pos+n) into acc (mixing) and clears it.
    void take(int64_t pos, uint32_t n, float* acc, float* peak, double* ss) {
        *peak = 0;
        *ss = 0;
        if (ring_.empty()) return;  // device never opened
        float pk = 0;
        double s = 0;
        for (uint32_t k = 0; k < n; ++k) {
            const size_t i = static_cast<size_t>((pos + k) & (kRing - 1)) * 2;
            const float l = ring_[i] * gain, r = ring_[i + 1] * gain;
            ring_[i] = ring_[i + 1] = 0.f;
            acc[2 * k] += l;
            acc[2 * k + 1] += r;
            pk = std::max(pk, std::max(std::fabs(l), std::fabs(r)));
            s += static_cast<double>(l) * l + static_cast<double>(r) * r;
        }
        *peak = pk;
        *ss = s;
    }

private:
    void fail(HRESULT hr) {
        dead = true;
        Log::event("audio.lost", false, -1,
                   label + " " + u8(name) +
                       (hr == AUDCLNT_E_DEVICE_INVALIDATED ? " was unplugged or disabled" : " stopped delivering audio"),
                   hr);
    }

    void put(int64_t p, const float* d, uint32_t n, int64_t readPos) {
        const int64_t lo = readPos, hi = readPos + kRing;
        if (haveEnd_ && p == end_ + 1 && end_ >= lo && end_ < hi && n > 0) {
            const size_t i = static_cast<size_t>(end_ & (kRing - 1)) * 2;  // fill the 1-sample gap
            ring_[i] = d[0];
            ring_[i + 1] = d[1];
        }
        for (uint32_t k = 0; k < n; ++k) {
            const int64_t q = p + k;
            if (q < lo) { late++; continue; }
            if (q >= hi) { overflow++; continue; }
            const size_t i = static_cast<size_t>(q & (kRing - 1)) * 2;
            ring_[i] = d[2 * k];
            ring_[i + 1] = d[2 * k + 1];
        }
    }

    float sample(const BYTE* f, uint32_t c) const {
        switch (type_) {
            case 0: return reinterpret_cast<const float*>(f)[c];
            case 1: return reinterpret_cast<const int16_t*>(f)[c] / 32768.f;
            case 2: {
                const BYTE* b = f + c * 3;
                int32_t v = static_cast<int32_t>((static_cast<uint32_t>(b[0]) << 8) | (static_cast<uint32_t>(b[1]) << 16) |
                                                 (static_cast<uint32_t>(b[2]) << 24)) >> 8;
                return static_cast<float>(v) / 8388608.f;
            }
            default: return static_cast<float>(reinterpret_cast<const int32_t*>(f)[c]) / 2147483648.f;
        }
    }

    // To 48 kHz stereo float in conv_. Returns frames.
    uint32_t convert(const BYTE* data, uint32_t n, bool silent) {
        if (!native_) {
            conv_.resize(static_cast<size_t>(n) * 2);
            if (silent || !data) std::fill(conv_.begin(), conv_.end(), 0.f);
            else memcpy(conv_.data(), data, static_cast<size_t>(n) * 8);
            return n;
        }
        tmp_.resize(static_cast<size_t>(n) * 2);
        for (uint32_t i = 0; i < n; ++i) {
            float l = 0, r = 0;
            if (!silent && data) {
                const BYTE* f = data + static_cast<size_t>(i) * block_;
                if (ch_ == 1) {
                    l = r = sample(f, 0);
                } else {
                    l = sample(f, 0);
                    r = sample(f, 1);
                    if (ch_ >= 3) {
                        const float c = sample(f, 2) * 0.7071f;
                        l += c;
                        r += c;
                    }
                }
            }
            tmp_[2 * i] = l;
            tmp_[2 * i + 1] = r;
        }
        if (rate_ == 48000) {
            conv_.swap(tmp_);
            return n;
        }
        // Streaming linear resampler. Source index -1 is the last frame of the previous packet.
        conv_.clear();
        double t = -1.0 + frac_;
        while (t < static_cast<double>(n) - 1.0) {
            const int64_t i0 = static_cast<int64_t>(std::floor(t));
            const float a = static_cast<float>(t - static_cast<double>(i0));
            const float l0 = i0 < 0 ? prevL_ : tmp_[static_cast<size_t>(i0) * 2];
            const float r0 = i0 < 0 ? prevR_ : tmp_[static_cast<size_t>(i0) * 2 + 1];
            const float l1 = tmp_[static_cast<size_t>(i0 + 1) * 2];
            const float r1 = tmp_[static_cast<size_t>(i0 + 1) * 2 + 1];
            conv_.push_back(l0 + (l1 - l0) * a);
            conv_.push_back(r0 + (r1 - r0) * a);
            t += step_;
        }
        frac_ = t - (static_cast<double>(n) - 1.0);
        if (n > 0) {
            prevL_ = tmp_[static_cast<size_t>(n - 1) * 2];
            prevR_ = tmp_[static_cast<size_t>(n - 1) * 2 + 1];
        }
        return static_cast<uint32_t>(conv_.size() / 2);
    }

    Com<IMMDevice> dev_;
    Com<IAudioClient> client_;
    Com<IAudioCaptureClient> cap_;
    bool loopback_ = false;
    bool native_ = false;
    uint32_t ch_ = 2, rate_ = 48000, block_ = 8;
    int type_ = 0;
    double step_ = 1.0, frac_ = 0.0;
    float prevL_ = 0, prevR_ = 0;
    std::vector<float> conv_, tmp_, ring_;
    int64_t end_ = 0;
    bool haveEnd_ = false;
};

namespace {

bool isVirtualDevice(const std::wstring& name) {
    const std::wstring n = lower(name);
    return n.find(L"voicemeeter") != std::wstring::npos || n.find(L"vb-audio") != std::wstring::npos ||
           n.find(L"cable output") != std::wstring::npos || n.find(L"virtual") != std::wstring::npos;
}

bool isHeadsetLike(const std::wstring& name) {
    const std::wstring n = lower(name);
    return !isVirtualDevice(name) && (n.find(L"usb") != std::wstring::npos || n.find(L"headset") != std::wstring::npos ||
                                      n.find(L"ヘッドセット") != std::wstring::npos || n.find(L"hands-free") != std::wstring::npos ||
                                      n.find(L"ハンズフリー") != std::wstring::npos);
}

// Opens the device for `ms` and returns its peak (linear). -1 when it cannot be opened.
float shortPeak(IMMDeviceEnumerator* en, const AudioDevice& d, int ms, std::string* err) {
    AudioCapture c;
    if (!c.open(en, d, false, err) || !c.start(err)) return -1.f;
    const int64_t start100 = qpcTo100ns(qpcNow());
    std::vector<float> acc(kChunk * 2);
    int64_t pos = 0;
    float peak = 0;
    const int64_t end = static_cast<int64_t>(ms) * 48;
    const int64_t t0 = qpcNow();
    while (qpcMs(qpcNow() - t0) < ms + 200) {
        const int64_t nowPos = (qpcTo100ns(qpcNow()) - start100) * 48000 / 10000000;
        c.poll(start100, pos, nowPos);
        while (pos + kChunk <= nowPos - 2400 && pos < end) {
            float pk = 0;
            double ss = 0;
            std::fill(acc.begin(), acc.end(), 0.f);
            c.take(pos, kChunk, acc.data(), &pk, &ss);
            peak = std::max(peak, pk);
            pos += kChunk;
        }
        if (pos >= end) break;
        Sleep(10);
    }
    c.stop();
    return peak;
}

}  // namespace

std::string probeLevels(double seconds) {
    std::string err;
    Com<IMMDeviceEnumerator> en = enumerator(&err);
    if (!en) return "{\"ok\":false,\"error\":" + json(err) + "}";
    struct P { std::unique_ptr<AudioCapture> c; std::string error; float peak = 0; double ss = 0; uint64_t n = 0, silent = 0, chunks = 0; std::wstring apps; };
    std::vector<P> ps;
    for (int r = 0; r < 2; ++r) {
        for (auto& d : listAudioDevices(r == 1, nullptr, r == 1)) {
            P p;
            p.c = std::make_unique<AudioCapture>();
            p.c->isMic = r == 0;
            p.c->label = r == 0 ? "mic" : "speaker";
            for (auto& a : d.apps) p.apps += (p.apps.empty() ? L"" : L", ") + a.exe + (a.active ? L"*" : L"");
            if (!p.c->open(en.get(), d, r == 1, &p.error)) p.c->dead = true;
            ps.push_back(std::move(p));
        }
    }
    for (auto& p : ps) if (!p.c->dead && !p.c->start(&p.error)) p.c->dead = true;
    const int64_t start = qpcNow();
    const int64_t start100 = qpcTo100ns(start);
    const int64_t lat = 7200;
    int64_t mixed = 0;
    std::vector<float> acc(kChunk * 2);
    const int64_t endPos = static_cast<int64_t>(seconds * 48000.0);
    while (mixed < endPos) {
        const int64_t nowPos = (qpcTo100ns(qpcNow()) - start100) * 48000 / 10000000;
        for (auto& p : ps) if (!p.c->dead) p.c->poll(start100, mixed, nowPos);
        while (mixed + kChunk <= nowPos - lat && mixed < endPos) {
            for (auto& p : ps) {
                std::fill(acc.begin(), acc.end(), 0.f);
                float pk = 0;
                double ss = 0;
                p.c->take(mixed, kChunk, acc.data(), &pk, &ss);
                p.peak = std::max(p.peak, pk);
                p.ss += ss;
                p.n += kChunk;
                p.chunks++;
                if (pk < kSilent) p.silent++;
            }
            mixed += kChunk;
        }
        Sleep(10);
    }
    for (auto& p : ps) p.c->stop();
    std::string s = fmt("{\"ok\":true,\"seconds\":%.1f,\"devices\":[", seconds);
    for (size_t i = 0; i < ps.size(); ++i) {
        auto& p = ps[i];
        const double rms = p.n ? std::sqrt(p.ss / (2.0 * p.n)) : 0;
        s += fmt("%s{\"kind\":\"%s\",\"name\":%s,\"apps\":%s,\"error\":%s,\"packets\":%llu,\"peak_db\":%.1f,\"rms_db\":%.1f,\"silent_pct\":%.0f}",
                 i ? "," : "", p.c->label.c_str(), json(p.c->name).c_str(), json(p.apps).c_str(), json(p.error).c_str(),
                 static_cast<unsigned long long>(p.c->packets), db10(p.peak) / 10.0, db10(static_cast<float>(rms)) / 10.0,
                 p.chunks ? 100.0 * p.silent / p.chunks : 100.0);
    }
    return s + "]}";
}

// ---- engine -----------------------------------------------------------------

AudioEngine::AudioEngine() = default;

AudioEngine::~AudioEngine() { stopNow(); }

void AudioEngine::open(const Config& c, std::vector<std::string>* notes) {
    cfg_ = c;
    latencyFrames_ = static_cast<int64_t>(c.latencyMs) * 48;
    {
        std::lock_guard<std::mutex> lk(mu_);
        opened_ = goSignal_ = false;
        openNotes_.clear();
    }
    stopFrames_ = -1;
    stopNow_ = false;
    mixed_ = 0;
    clipped_ = 0;
    for (int k = 0; k < 2; ++k) {
        mPk_[k] = 0;
        mSs_[k] = 0;
        mN_[k] = 0;
        silentRun_[k] = 0;
    }
    for (Meter* m : {&mic, &sys}) {
        m->peakDb10 = -900;
        m->rmsDb10 = -900;
        m->silentMs = 0;
    }
    helperStop_ = false;
    micDead_ = false;
    pendingSys_.clear();
    pendingMic_.clear();
    running_ = true;
    th_ = std::thread([this] { run(); });
    std::unique_lock<std::mutex> lk(mu_);
    cv_.wait_for(lk, std::chrono::seconds(8), [this] { return opened_; });
    if (notes) notes->insert(notes->end(), openNotes_.begin(), openNotes_.end());
}

void AudioEngine::go(int64_t qpcStart, Sink sink) {
    std::lock_guard<std::mutex> lk(mu_);
    start100_ = qpcTo100ns(qpcStart);
    sink_ = std::move(sink);
    goSignal_ = true;
    cv_.notify_all();
}

void AudioEngine::stopAt(int64_t qpcStop) {
    if (!th_.joinable()) return;
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (!goSignal_) { stopNow_ = true; goSignal_ = true; cv_.notify_all(); }
    }
    stopFrames_ = std::max<int64_t>(0, (qpcTo100ns(qpcStop) - start100_) * 48000 / 10000000);
    th_.join();
}

void AudioEngine::stopNow() {
    if (!th_.joinable()) return;
    {
        std::lock_guard<std::mutex> lk(mu_);
        stopNow_ = true;
        goSignal_ = true;
        cv_.notify_all();
    }
    th_.join();
}

std::wstring AudioEngine::micName() const { std::lock_guard<std::mutex> lk(statsMu_); return micName_; }
std::wstring AudioEngine::sysName() const { std::lock_guard<std::mutex> lk(statsMu_); return sysName_; }

void AudioEngine::run() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    DWORD task = 0;
    HANDLE mm = AvSetMmThreadCharacteristicsW(L"Audio", &task);
    Stopwatch sw;
    std::vector<std::string> notes;
    std::string err;
    Com<IMMDeviceEnumerator> en = enumerator(&err);
    if (!err.empty()) notes.push_back(err);
    src_.clear();
    sysCount_ = 0;
    {
        std::lock_guard<std::mutex> lk(statsMu_);
        micName_.clear();
        sysName_.clear();
    }
    autoSys_ = lower(cfg_.system) == L"auto";
    if (en) {
        if (lower(cfg_.mic) == L"auto") {
            AudioDevice d;
            if (chooseAutoMic(en.get(), &d, &notes)) addSource(en.get(), d, true, &notes);
        } else {
            auto mics = resolveDevices(cfg_.mic, false, &notes);
            if (mics.size() > 1) mics.resize(1);
            for (auto& d : mics) addSource(en.get(), d, true, &notes);
        }
        auto syss = autoSys_ ? autoSystemDevices() : resolveDevices(cfg_.system, true, &notes);
        for (auto& d : syss) addSource(en.get(), d, false, &notes);
    }
    std::wstring micN = micName(), sysN = sysName();
    mic.active = !micN.empty();
    sys.active = !sysN.empty() || autoSys_;
    micDead_ = micN.empty() && lower(cfg_.mic) != L"none";   // keep looking for a microphone while recording
    Log::event("audio.ready", true, sw.ms(),
               fmt("mic=%s system=%s latency=%dms", micN.empty() ? "none" : u8(micN).c_str(),
                   sysN.empty() ? "none" : u8(sysN).c_str(), cfg_.latencyMs));
    {
        std::unique_lock<std::mutex> lk(mu_);
        openNotes_ = notes;
        opened_ = true;
        cv_.notify_all();
        cv_.wait(lk, [this] { return goSignal_; });
    }
    if (!stopNow_) {
        for (auto& s : src_) {
            std::string e;
            if (!s->start(&e)) Log::event("audio.start", false, -1, s->label + " " + e);
        }
        HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        if (!timer) timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
        outStart_ = 0;
        out_.clear();
        helperStop_ = false;
        helper_ = std::thread([this] { helperLoop(); });
        for (;;) {
            if (en) takePending(en.get());
            reapDead();
            const int64_t now100 = qpcTo100ns(qpcNow()) - start100_;
            const int64_t nowPos = now100 * 48000 / 10000000;
            int64_t avail = nowPos - latencyFrames_;
            const int64_t stopF = stopFrames_.load();
            if (stopF >= 0 && avail > stopF) avail = stopF;
            const int64_t m = mixed_.load();
            for (auto& s : src_) s->poll(start100_, m, nowPos);
            while (mixed_.load() + kChunk <= avail) mix(kChunk);
            if (stopF >= 0 && avail >= stopF) {
                if (mixed_.load() < stopF) mix(static_cast<uint32_t>(stopF - mixed_.load()));
                break;
            }
            if (stopNow_) break;
            if (timer) {
                LARGE_INTEGER due;
                due.QuadPart = -100000;  // 10 ms
                SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE);
                WaitForSingleObject(timer, 50);
            } else {
                Sleep(10);
            }
        }
        if (!out_.empty() && sink_) sink_(out_.data(), static_cast<uint32_t>(out_.size() / 2), outStart_ * 10000000 / 48000);
        out_.clear();
        if (timer) CloseHandle(timer);
        {
            std::lock_guard<std::mutex> lk(pendMu_);
            helperStop_ = true;
        }
        helperCv_.notify_all();
        if (helper_.joinable()) helper_.join();
    }
    {
        // Release the device objects here, on this MTA thread, before COM and Media Foundation shut down.
        std::lock_guard<std::mutex> lk(statsMu_);
        for (auto& s : src_) s->release();
    }
    Log::event("audio.end", true, -1, fmt("mixed=%.1fs clipped=%llu", mixed_.load() / 48000.0,
                                           static_cast<unsigned long long>(clipped_.load())));
    mic.active = false;
    sys.active = false;
    if (mm) AvRevertMmThreadCharacteristics(mm);
    CoUninitialize();
    running_ = false;
}

bool AudioEngine::chooseAutoMic(IMMDeviceEnumerator* en, AudioDevice* out, std::vector<std::string>* notes) {
    Stopwatch sw;
    auto all = listAudioDevices(false);
    std::string trail;
    auto tryOpen = [&](const AudioDevice& d, const char* why) {
        std::string e;
        const float pk = shortPeak(en, d, 150, &e);
        trail += fmt("%s%s:%s=%s", trail.empty() ? "" : " | ", why, u8(d.name).c_str(),
                     pk < 0 ? ("open " + e).c_str() : fmt("%.0fdB", db10(pk) / 10.0).c_str());
        return pk >= 0;
    };
    auto pick = [&](const AudioDevice& d, const char* why) {
        *out = d;
        Log::event("audio.auto_mic", true, sw.ms(), std::string(why) + " -> " + u8(d.name) + " [" + trail + "]");
        return true;
    };
    bool headsetBusy = false;
    for (auto& d : all)
        if (isHeadsetLike(d.name)) {
            if (tryOpen(d, "headset")) return pick(d, "headset");
            headsetBusy = true;
        }
    std::vector<const AudioDevice*> virt;
    for (auto& d : all) if (isVirtualDevice(d.name)) virt.push_back(&d);
    // Mixer bus outputs first (B = virtual buses in Voicemeeter), then the rest.
    std::stable_sort(virt.begin(), virt.end(), [](const AudioDevice* a, const AudioDevice* b) {
        const bool ab = lower(a->name).find(L"out b") != std::wstring::npos;
        const bool bb = lower(b->name).find(L"out b") != std::wstring::npos;
        return ab && !bb;
    });
    auto signalIn = [&](const char* why) -> const AudioDevice* {
        for (auto* d : virt) {
            std::string e;
            const float pk = shortPeak(en, *d, 300, &e);
            trail += fmt("%s%s:%s=%s", trail.empty() ? "" : " | ", why, u8(d->name).c_str(),
                         pk < 0 ? ("open " + e).c_str() : fmt("%.0fdB", db10(pk) / 10.0).c_str());
            if (pk > 0.0000562f) return d;  // above -85 dBFS: something is routed there
        }
        return nullptr;
    };
    if (headsetBusy)
        if (const AudioDevice* d = signalIn("mixer")) return pick(*d, "headset held by a mixer, mixer output");
    for (auto& d : all)
        if (d.defComm && !isVirtualDevice(d.name) && tryOpen(d, "communications")) return pick(d, "communications default");
    for (auto& d : all)
        if (d.defConsole && !isVirtualDevice(d.name) && tryOpen(d, "default")) return pick(d, "default");
    for (auto& d : all)
        if (!isVirtualDevice(d.name) && !d.defComm && !d.defConsole && tryOpen(d, "physical")) return pick(d, "physical");
    if (!headsetBusy)
        if (const AudioDevice* d = signalIn("virtual")) return pick(*d, "virtual with sound");
    for (auto& d : all)
        if (d.defConsole) return pick(d, "fallback default (may be silent)");
    Log::event("audio.auto_mic", false, sw.ms(), "no microphone [" + trail + "]");
    if (notes) notes->push_back("mic auto: no microphone found");
    return false;
}

AudioCapture* AudioEngine::addSource(IMMDeviceEnumerator* en, const AudioDevice& d, bool isMic, std::vector<std::string>* notes) {
    auto s = std::make_unique<AudioCapture>();
    s->isMic = isMic;
    s->label = isMic ? "mic" : fmt("sys%d", ++sysCount_);
    s->gain = static_cast<float>(std::pow(10.0, (isMic ? cfg_.micGainDb : cfg_.systemGainDb) / 20.0));
    std::string e;
    Stopwatch t;
    std::string apps;
    for (auto& a : d.apps) apps += (apps.empty() ? "" : ", ") + u8(a.exe) + (a.active ? "*" : "");
    if (!s->open(en, d, !isMic, &e)) {
        Log::event("audio.open", false, t.ms(), s->label + " " + u8(d.name) + ": " + e);
        if (notes) notes->push_back(s->label + " " + u8(d.name) + ": " + e);
        return nullptr;
    }
    Log::event("audio.open", true, t.ms(), s->label + " " + u8(d.name) + " / " + s->format + (apps.empty() ? "" : " / apps: " + apps));
    std::lock_guard<std::mutex> lk(statsMu_);
    if (isMic) {
        micName_ = d.name;   // one microphone at a time (a replacement takes over the name)
    } else {
        sysName_ += (sysName_.empty() ? L"" : L" + ") + d.name;
    }
    src_.push_back(std::move(s));
    return src_.back().get();
}

void AudioEngine::helperLoop() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    std::string err;
    Com<IMMDeviceEnumerator> en = enumerator(&err);
    std::map<std::wstring, int64_t> retryAt;   // playback device id -> earliest next attempt (QPC)
    const bool micWanted = lower(cfg_.mic) != L"none";
    int64_t nextMic = 0;
    for (;;) {
        {
            std::unique_lock<std::mutex> lk(pendMu_);
            helperCv_.wait_for(lk, std::chrono::seconds(3), [this] { return helperStop_.load(); });
            if (helperStop_) break;
        }
        const int64_t now = qpcNow();
        if (autoSys_) {
            std::set<std::wstring> have;
            {
                std::lock_guard<std::mutex> lk(statsMu_);
                for (auto& s : src_)
                    if (!s->isMic && !s->dead) have.insert(s->id);
            }
            std::vector<AudioDevice> add;
            for (auto& d : autoSystemDevices()) {
                if (have.count(d.id)) continue;
                auto it = retryAt.find(d.id);
                if (it != retryAt.end() && now < it->second) continue;   // failed or died recently
                retryAt[d.id] = now + 30 * qpcFreq();
                add.push_back(d);
            }
            if (!add.empty()) {
                std::lock_guard<std::mutex> lk(pendMu_);
                pendingSys_.insert(pendingSys_.end(), add.begin(), add.end());
            }
        }
        if (micWanted && micDead_ && en && now >= nextMic) {
            nextMic = now + 5 * qpcFreq();
            AudioDevice d;
            bool found = false;
            if (lower(cfg_.mic) == L"auto") {
                found = chooseAutoMic(en.get(), &d, nullptr);
            } else {
                auto v = resolveDevices(cfg_.mic, false, nullptr);
                if (!v.empty()) { d = v[0]; found = true; }
            }
            if (found) {
                std::lock_guard<std::mutex> lk(pendMu_);
                pendingMic_.push_back(d);
                micDead_ = false;   // the engine sets it again if the device does not open
            }
        }
    }
    CoUninitialize();
}

void AudioEngine::takePending(IMMDeviceEnumerator* en) {
    std::vector<AudioDevice> ps, pm;
    {
        std::unique_lock<std::mutex> lk(pendMu_, std::try_to_lock);
        if (!lk.owns_lock()) return;
        ps.swap(pendingSys_);
        pm.swap(pendingMic_);
    }
    for (auto& d : pm) {
        AudioCapture* s = addSource(en, d, true, nullptr);
        std::string e;
        if (s && s->start(&e)) {
            mic.active = true;
            Log::event("audio.mic_replaced", true, -1, u8(d.name));
        } else {
            if (s) Log::event("audio.start", false, -1, "mic " + e);
            micDead_ = true;
        }
    }
    for (auto& d : ps) {
        AudioCapture* s = addSource(en, d, false, nullptr);
        std::string e;
        if (s && !s->start(&e)) Log::event("audio.start", false, -1, s->label + " " + e);
        if (s && !s->dead) sys.active = true;
    }
}

void AudioEngine::reapDead() {
    bool changed = false;
    std::lock_guard<std::mutex> lk(statsMu_);
    for (auto& s : src_)
        if (s->dead && !s->released) {
            s->release();
            changed = true;
        }
    if (!changed) return;
    bool micAlive = false, sysAlive = false;
    for (auto& s : src_) {
        if (s->dead) continue;
        (s->isMic ? micAlive : sysAlive) = true;
    }
    if (!micAlive && mic.active) {
        mic.active = false;            // the window then says "マイクなし"
        micDead_ = lower(cfg_.mic) != L"none";
    }
    if (!sysAlive && !autoSys_) sys.active = false;
}

void AudioEngine::mix(uint32_t n) {
    acc_.assign(static_cast<size_t>(n) * 2, 0.f);
    const int64_t pos = mixed_.load();
    float pk[2] = {0, 0};
    double ss[2] = {0, 0};
    {
        std::lock_guard<std::mutex> lk(statsMu_);
        for (auto& s : src_) {
            float p = 0;
            double q = 0;
            s->take(pos, n, acc_.data(), &p, &q);
            const int k = s->isMic ? 0 : 1;
            pk[k] = std::max(pk[k], p);
            ss[k] += q;
            s->wPeak = std::max(s->wPeak, p);
            s->sPeak = std::max(s->sPeak, p);
            s->wSs += q;
            s->sSs += q;
            s->wN += n;
            s->sN += n;
            s->wChunks++;
            s->sChunks++;
            if (p < kSilent) { s->wSilent++; s->sSilent++; }
        }
    }
    finishMeters(true, pk[0], ss[0], n);
    finishMeters(false, pk[1], ss[1], n);
    if (sink_) {
        if (out_.empty()) outStart_ = pos;
        for (size_t i = 0; i < acc_.size(); ++i) {
            float v = acc_[i];
            if (v > 1.f) { v = 1.f; clipped_++; }
            else if (v < -1.f) { v = -1.f; clipped_++; }
            out_.push_back(static_cast<int16_t>(std::lround(v * 32767.f)));
        }
        if (out_.size() / 2 >= kSinkFrames) {
            sink_(out_.data(), static_cast<uint32_t>(out_.size() / 2), outStart_ * 10000000 / 48000);
            out_.clear();
        }
    }
    mixed_ = pos + n;
}

void AudioEngine::finishMeters(bool isMic, float peak, double ss, uint32_t n) {
    const int k = isMic ? 0 : 1;
    Meter& m = isMic ? mic : sys;
    mPk_[k] = std::max(mPk_[k], peak);
    mSs_[k] += ss;
    mN_[k] += n;
    silentRun_[k] = peak < kSilent ? silentRun_[k] + static_cast<int64_t>(n) : 0;
    if (mN_[k] >= 4800) {
        m.peakDb10 = db10(mPk_[k]);
        m.rmsDb10 = db10(static_cast<float>(std::sqrt(mSs_[k] / (2.0 * mN_[k]))));
        m.silentMs = static_cast<int>(std::min<int64_t>(silentRun_[k] / 48, INT32_MAX));
        mPk_[k] = 0;
        mSs_[k] = 0;
        mN_[k] = 0;
    }
}

static std::string levelJson(float peak, double ss, uint64_t n, uint64_t silent, uint64_t chunks) {
    const double rms = n ? std::sqrt(ss / (2.0 * static_cast<double>(n))) : 0.0;
    return fmt("\"peak_db\":%.1f,\"rms_db\":%.1f,\"silent_pct\":%.0f", db10(peak) / 10.0, db10(static_cast<float>(rms)) / 10.0,
               chunks ? 100.0 * static_cast<double>(silent) / static_cast<double>(chunks) : 100.0);
}

std::string AudioEngine::windowJson() {
    std::lock_guard<std::mutex> lk(statsMu_);
    std::string s = "{";
    bool first = true;
    for (auto& c : src_) {
        s += (first ? "" : ",") + json(c->label) + ":{" + levelJson(c->wPeak, c->wSs, c->wN, c->wSilent, c->wChunks) +
             fmt(",\"pkts\":%llu,\"disc\":%llu,\"late\":%llu,\"jumps\":%llu,\"nudges\":%llu,\"dead\":%s}",
                 static_cast<unsigned long long>(c->packets), static_cast<unsigned long long>(c->disc),
                 static_cast<unsigned long long>(c->late), static_cast<unsigned long long>(c->jumps),
                 static_cast<unsigned long long>(c->nudges), c->dead ? "true" : "false");
        c->wPeak = 0;
        c->wSs = 0;
        c->wN = c->wSilent = c->wChunks = 0;
        first = false;
    }
    return s + "}";
}

std::string AudioEngine::summaryJson() {
    std::lock_guard<std::mutex> lk(statsMu_);
    std::string s = "[";
    bool first = true;
    for (auto& c : src_) {
        s += std::string(first ? "" : ",") + "{\"label\":" + json(c->label) + ",\"device\":" + json(c->name) +
             ",\"format\":" + json(c->format) + "," + levelJson(c->sPeak, c->sSs, c->sN, c->sSilent, c->sChunks) +
             fmt(",\"packets\":%llu,\"frames\":%llu,\"discontinuities\":%llu,\"silent_packets\":%llu,\"late_frames\":%llu,"
                 "\"overflow_frames\":%llu,\"jumps\":%llu,\"nudges\":%llu,\"dead\":%s}",
                 static_cast<unsigned long long>(c->packets), static_cast<unsigned long long>(c->frames),
                 static_cast<unsigned long long>(c->disc), static_cast<unsigned long long>(c->silentPk),
                 static_cast<unsigned long long>(c->late), static_cast<unsigned long long>(c->overflow),
                 static_cast<unsigned long long>(c->jumps), static_cast<unsigned long long>(c->nudges),
                 c->dead ? "true" : "false");
        first = false;
    }
    return s + "]";
}

TonePlayer::~TonePlayer() { stop(); }

bool TonePlayer::start(const std::wstring& spec, int64_t qpcStart, std::vector<double> times, double burstS, std::string* err) {
    std::vector<std::string> notes;
    auto devs = resolveDevices(spec, true, &notes);
    if (devs.empty()) { *err = "tone device not found: " + u8(spec); return false; }
    device = devs[0].name;
    const std::wstring id = devs[0].id;
    stop_ = false;
    th_ = std::thread([this, id, qpcStart, times, burstS] {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        std::string e;
        Com<IMMDeviceEnumerator> en = enumerator(&e);
        Com<IMMDevice> dev;
        Com<IAudioClient> client;
        Com<IAudioRenderClient> rc;
        HRESULT hr = en ? en->GetDevice(id.c_str(), dev.put()) : E_FAIL;
        if (SUCCEEDED(hr)) hr = dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, client.put_void());
        WAVEFORMATEXTENSIBLE wf{};
        wf.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
        wf.Format.nChannels = 2;
        wf.Format.nSamplesPerSec = 48000;
        wf.Format.wBitsPerSample = 32;
        wf.Format.nBlockAlign = 8;
        wf.Format.nAvgBytesPerSec = 48000 * 8;
        wf.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
        wf.Samples.wValidBitsPerSample = 32;
        wf.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
        wf.SubFormat = kSubtypeFloat;
        if (SUCCEEDED(hr))
            hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                                    500000, 0, &wf.Format, nullptr);
        UINT32 bufFrames = 0;
        if (SUCCEEDED(hr)) hr = client->GetBufferSize(&bufFrames);
        if (SUCCEEDED(hr)) hr = client->GetService(__uuidof(IAudioRenderClient), rc.put_void());
        if (SUCCEEDED(hr)) hr = client->Start();
        Log::event("tone.start", SUCCEEDED(hr), -1, u8(device), hr);
        if (SUCCEEDED(hr)) {
            // Frame k of this stream is written now and heard after the queued padding; schedule by wall clock.
            const double f = 1000.0;
            uint64_t phase = 0;
            while (!stop_) {
                UINT32 pad = 0;
                if (FAILED(client->GetCurrentPadding(&pad))) break;
                const UINT32 n = bufFrames - pad;
                if (n > 0) {
                    BYTE* d = nullptr;
                    if (FAILED(rc->GetBuffer(n, &d))) break;
                    float* s = reinterpret_cast<float*>(d);
                    const double nowRel = qpcMs(qpcNow() - qpcStart) / 1000.0 + pad / 48000.0;
                    for (UINT32 i = 0; i < n; ++i) {
                        const double tt = nowRel + i / 48000.0;
                        bool on = false;
                        for (double x : times) if (tt >= x && tt < x + burstS) on = true;
                        const float v = on ? 0.5f * static_cast<float>(std::sin(6.283185307179586 * f * (phase / 48000.0))) : 0.f;
                        s[2 * i] = v;
                        s[2 * i + 1] = v;
                        ++phase;
                    }
                    rc->ReleaseBuffer(n, 0);
                }
                Sleep(5);
            }
            client->Stop();
        }
        CoUninitialize();
    });
    return true;
}

void TonePlayer::stop() {
    stop_ = true;
    if (th_.joinable()) th_.join();
}

}  // namespace rc
