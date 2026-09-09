// overlay_window.cpp - per-monitor overlay window: painting and capture exclusion
#include "overlay_window.h"
#include "../config.h"
#include "../images/image_store.h"
#include "../privacy/capture_exclusion.h"
#include "../util/text.h"
#include <objidl.h>
#include <gdiplus.h>

namespace overlay {

namespace {

// Draw the bitmap scaled to cover the whole rect (center crop)
void DrawCover(HDC dst, const RECT& rc, Gdiplus::Bitmap* bmp)
{
    const int dstW = rc.right - rc.left;
    const int dstH = rc.bottom - rc.top;
    if (dstW <= 0 || dstH <= 0 || !bmp) return;

    const UINT srcW = bmp->GetWidth();
    const UINT srcH = bmp->GetHeight();
    if (srcW == 0 || srcH == 0) return;

    Gdiplus::Graphics g(dst);
    if (g.GetLastStatus() != Gdiplus::Ok) return;
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);

    const Gdiplus::REAL scale =
        (static_cast<Gdiplus::REAL>(dstW) / srcW > static_cast<Gdiplus::REAL>(dstH) / srcH)
            ? static_cast<Gdiplus::REAL>(dstW) / srcW
            : static_cast<Gdiplus::REAL>(dstH) / srcH;
    const int drawW = static_cast<int>(srcW * scale + 0.5f);
    const int drawH = static_cast<int>(srcH * scale + 0.5f);
    const int offX  = rc.left + (dstW - drawW) / 2;
    const int offY  = rc.top  + (dstH - drawH) / 2;

    g.DrawImage(bmp, offX, offY, drawW, drawH);
}

} // namespace

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;

    case WM_ERASEBKGND:
    {
        // Always black; image mode paints over it in WM_PAINT
        RECT rc{};
        GetClientRect(hwnd, &rc);
        FillRect(reinterpret_cast<HDC>(wParam), &rc,
                 static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        return 1;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd, &ps);
        if (hdc)
        {
            RECT rc{};
            GetClientRect(hwnd, &rc);
            FillRect(hdc, &rc, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));

            // Image mode: draw through a memory DC to avoid tearing on large images
            images::Store& store = images::GetStore();
            if (store.HasImages())
            {
                Gdiplus::Bitmap* bmp = store.Get(store.Index());
                if (bmp)
                {
                    HDC mem = CreateCompatibleDC(hdc);
                    if (mem)
                    {
                        HBITMAP hbmp = CreateCompatibleBitmap(hdc, rc.right - rc.left,
                                                              rc.bottom - rc.top);
                        if (hbmp)
                        {
                            HGDIOBJ old = SelectObject(mem, hbmp);
                            FillRect(mem, &rc,
                                     static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
                            DrawCover(mem, rc, bmp);
                            BitBlt(hdc, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                                   mem, 0, 0, SRCCOPY);
                            SelectObject(mem, old);
                            DeleteObject(hbmp);
                        }
                        DeleteDC(mem);
                    }
                }
            }
            EndPaint(hwnd, &ps);
        }
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

CreateResult Create(const RECT& monitorRect, bool ddaFallback)
{
    CreateResult result;

    constexpr DWORD exStyle =
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE |
        WS_EX_LAYERED | WS_EX_TRANSPARENT;

    HWND hwnd = CreateWindowExW(
        exStyle,
        cfg::kOverlayClassName,
        L"", // empty title keeps it out of Alt+Tab lists
        WS_POPUP,
        monitorRect.left, monitorRect.top,
        monitorRect.right - monitorRect.left,
        monitorRect.bottom - monitorRect.top,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);

    if (!hwnd)
    {
        const DWORD e = GetLastError();
        result.error = L"CreateWindowExW failed: GLE=" + std::to_wstring(e) + L" (" +
                       util::LastErrorMessage(e) + L")";
        return result;
    }

    // alpha=255: fully opaque; LAYERED+TRANSPARENT only makes mouse events pass through
    SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);

    // Capture exclusion + read-back verification; destroy on failure
    std::wstring err;
    if (!privacy::EnableCaptureExclusion(hwnd, ddaFallback, err))
    {
        DestroyWindow(hwnd);
        result.error = err;
        return result;
    }

    SetWindowPos(hwnd, HWND_TOPMOST,
                 monitorRect.left, monitorRect.top,
                 monitorRect.right - monitorRect.left,
                 monitorRect.bottom - monitorRect.top,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);

    result.hwnd = hwnd;
    return result;
}

} // namespace overlay
