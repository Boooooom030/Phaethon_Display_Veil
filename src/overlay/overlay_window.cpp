#include "overlay_window.h"
#include "../config.h"
#include "../images/image_store.h"
#include "../privacy/capture_exclusion.h"
#include "../util/logger.h"
#include "../util/text.h"
#include <objidl.h>
#include <gdiplus.h>

namespace overlay {

namespace {

// 将 src 位图以 cover 方式（等比放大填满、居中裁剪）绘制到 dst DC
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

    // cover: 缩放比取 max
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
        // 绝不激活：foreground 必须留给真实应用 / Moonlight 输入目标
        return MA_NOACTIVATE;

    case WM_ERASEBKGND:
    {
        // 背景一律刷黑：图片模式在 WM_PAINT 覆盖，加载失败也有黑底兜底
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

            // 图片模式：绘制当前幻灯片（内存 DC 缓冲，避免大图直绘撕裂）
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
        L"", // 无标题：不出现在 Alt+Tab 枚举文本中
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

    // alpha=255：视觉上完全不通透（LAYERED+TRANSPARENT 只用于鼠标穿透）
    SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);

    // 核心安全步骤：capture 排除 + 回读校验。失败 → 销毁并报错（fail-safe）。
    std::wstring err;
    if (!privacy::EnableCaptureExclusion(hwnd, ddaFallback, err))
    {
        DestroyWindow(hwnd);
        result.error = err;
        return result;
    }

    // 置顶显示，不激活
    SetWindowPos(hwnd, HWND_TOPMOST,
                 monitorRect.left, monitorRect.top,
                 monitorRect.right - monitorRect.left,
                 monitorRect.bottom - monitorRect.top,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);

    result.hwnd = hwnd;
    return result;
}

} // namespace overlay
