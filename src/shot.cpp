// Rec Cue! — screenshot (see shot.h).
#include "shot.h"

#include <dwmapi.h>
#include <wincodec.h>

#include "video.h"

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

namespace rc {

bool captureMonitor(int index, HWND self, ShotImage* out, ShotInfo* info, std::string* err) {
    const auto mons = listMonitors();
    if (mons.empty()) {
        *err = "no monitor is attached to the desktop";
        return false;
    }
    if (index < 0 || index >= static_cast<int>(mons.size())) index = 0;
    const RECT r = mons[static_cast<size_t>(index)].rect;
    const int w = r.right - r.left, h = r.bottom - r.top;
    if (w <= 0 || h <= 0) {
        *err = fmt("monitor %d has no area", index);
        return false;
    }

    // Leave our own window out of the picture. Windows 10 2004+ keeps it on the monitor and drops it from every
    // capture; older systems refuse the flag, so the window is hidden for the moment of the shot instead.
    Stopwatch sw;
    if (self && IsWindowVisible(self) && !IsIconic(self)) {
        if (SetWindowDisplayAffinity(self, WDA_EXCLUDEFROMCAPTURE)) {
            info->excluded = true;
        } else {
            ShowWindow(self, SW_HIDE);
            info->hidden = true;
        }
        DwmFlush();   // the next composed frame is the first one without the window
        DwmFlush();
    }
    info->flushMs = sw.ms();

    Stopwatch bt;
    bool ok = false;
    HDC screen = GetDC(nullptr);
    HDC mem = screen ? CreateCompatibleDC(screen) : nullptr;
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;   // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = mem ? CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
    if (bmp && bits) {
        HGDIOBJ old = SelectObject(mem, bmp);
        ok = BitBlt(mem, 0, 0, w, h, screen, r.left, r.top, SRCCOPY | CAPTUREBLT) != 0;
        if (!ok) *err = fmt("BitBlt failed (%lu)", GetLastError());
        GdiFlush();
        SelectObject(mem, old);
        if (ok) {
            out->w = static_cast<UINT>(w);
            out->h = static_cast<UINT>(h);
            const auto* p = static_cast<const uint32_t*>(bits);
            out->px.assign(p, p + static_cast<size_t>(w) * static_cast<size_t>(h));
        }
    } else {
        *err = fmt("CreateDIBSection failed (%lu)", GetLastError());
    }
    if (bmp) DeleteObject(bmp);
    if (mem) DeleteDC(mem);
    if (screen) ReleaseDC(nullptr, screen);
    info->bltMs = bt.ms();

    if (info->excluded) SetWindowDisplayAffinity(self, WDA_NONE);
    if (info->hidden) ShowWindow(self, SW_SHOWNA);
    return ok;
}

HRESULT savePng(const ShotImage& img, const std::wstring& path) {
    if (img.px.empty()) return E_INVALIDARG;
    const std::wstring part = path + L".part";
    Com<IWICImagingFactory> wic;
    Com<IWICBitmap> bmp;
    Com<IWICStream> st;
    Com<IWICBitmapEncoder> enc;
    Com<IWICBitmapFrameEncode> fr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IWICImagingFactory),
                                  wic.put_void());
    if (SUCCEEDED(hr))
        hr = wic->CreateBitmapFromMemory(img.w, img.h, GUID_WICPixelFormat32bppBGR, img.w * 4,
                                         static_cast<UINT>(img.px.size() * 4),
                                         reinterpret_cast<BYTE*>(const_cast<uint32_t*>(img.px.data())), bmp.put());
    if (SUCCEEDED(hr)) hr = wic->CreateStream(st.put());
    if (SUCCEEDED(hr)) hr = st->InitializeFromFilename(part.c_str(), GENERIC_WRITE);
    if (SUCCEEDED(hr)) hr = wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, enc.put());
    if (SUCCEEDED(hr)) hr = enc->Initialize(st.get(), WICBitmapEncoderNoCache);
    if (SUCCEEDED(hr)) hr = enc->CreateNewFrame(fr.put(), nullptr);
    if (SUCCEEDED(hr)) hr = fr->Initialize(nullptr);
    if (SUCCEEDED(hr)) hr = fr->SetSize(img.w, img.h);
    WICPixelFormatGUID pf = GUID_WICPixelFormat24bppBGR;   // the encoder converts; no alpha channel in the file
    if (SUCCEEDED(hr)) hr = fr->SetPixelFormat(&pf);
    if (SUCCEEDED(hr)) hr = fr->WriteSource(bmp.get(), nullptr);
    if (SUCCEEDED(hr)) hr = fr->Commit();
    if (SUCCEEDED(hr)) hr = enc->Commit();
    fr.reset();
    enc.reset();
    st.reset();   // closes the file
    if (SUCCEEDED(hr) && !MoveFileExW(part.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING))
        hr = HRESULT_FROM_WIN32(GetLastError());
    if (FAILED(hr)) DeleteFileW(part.c_str());
    return hr;
}

bool copyToClipboard(HWND owner, const ShotImage& img) {
    if (img.px.empty()) return false;
    const size_t n = static_cast<size_t>(img.w) * img.h;
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, sizeof(BITMAPINFOHEADER) + n * 4);
    if (!mem) return false;
    auto* bih = static_cast<BITMAPINFOHEADER*>(GlobalLock(mem));
    if (!bih) {
        GlobalFree(mem);
        return false;
    }
    *bih = {};
    bih->biSize = sizeof(BITMAPINFOHEADER);
    bih->biWidth = static_cast<LONG>(img.w);
    bih->biHeight = static_cast<LONG>(img.h);   // bottom-up: what every app reads from CF_DIB
    bih->biPlanes = 1;
    bih->biBitCount = 32;
    bih->biCompression = BI_RGB;
    bih->biSizeImage = static_cast<DWORD>(n * 4);
    auto* dst = reinterpret_cast<uint32_t*>(bih + 1);
    for (UINT y = 0; y < img.h; ++y) {
        const uint32_t* src = img.px.data() + static_cast<size_t>(img.h - 1 - y) * img.w;
        uint32_t* row = dst + static_cast<size_t>(y) * img.w;
        for (UINT x = 0; x < img.w; ++x) row[x] = src[x] | 0xFF000000u;   // opaque (some apps read the 4th byte)
    }
    GlobalUnlock(mem);
    // Clipboard history, remote desktop and clipboard tools often hold the clipboard for a moment after a change.
    bool opened = false;
    for (int i = 0; i < 10 && !opened; ++i) {
        opened = OpenClipboard(owner) != 0;
        if (!opened) Sleep(15);
    }
    bool ok = false;
    if (opened) {
        if (EmptyClipboard()) {
            ok = SetClipboardData(CF_DIB, mem) != nullptr;   // the clipboard owns the memory from here
            // A work screen stays on this PC: keep the shot out of the clipboard history (Win+V) and the cloud clipboard.
            for (const wchar_t* name : {L"CanIncludeInClipboardHistory", L"CanUploadToCloudClipboard"}) {
                const UINT f = ok ? RegisterClipboardFormatW(name) : 0;
                HGLOBAL no = f ? GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD)) : nullptr;
                if (!no) continue;
                if (auto* p = static_cast<DWORD*>(GlobalLock(no))) {
                    *p = 0;
                    GlobalUnlock(no);
                    if (SetClipboardData(f, no)) continue;
                }
                GlobalFree(no);
            }
        }
        CloseClipboard();
    }
    if (!ok) GlobalFree(mem);
    return ok;
}

std::wstring uniquePath(const std::wstring& dir, const std::wstring& base, const wchar_t* ext) {
    std::wstring p = dir + L"\\" + base + ext;
    for (int i = 2; i < 100 && GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; ++i)
        p = dir + L"\\" + base + L" (" + std::to_wstring(i) + L")" + ext;
    return p;
}

}  // namespace rc
