// spike.cpp — Phase 1 Spike: 验证核心技术假设
// 覆盖主显示器的纯黑 TopMost 窗口 + WDA_EXCLUDEFROMCAPTURE。
// 创建 → 设 affinity → 回读校验 → 保持 5 秒 → 退出。
// 退出码: 0=校验通过  3=capture exclusion 失败  1=其他错误

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdio>

static LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;

    case WM_ERASEBKGND:
    {
        RECT rc{};
        GetClientRect(hwnd, &rc);
        FillRect(reinterpret_cast<HDC>(wp), &rc,
                 static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        return 1;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc{};
        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int main()
{
    // DPI：必须在创建任何窗口之前
    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
    {
        printf("[ERROR] SetProcessDpiAwarenessContext failed GLE=%lu\n", GetLastError());
        return 1;
    }

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = OverlayWndProc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.hCursor       = nullptr;
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.lpszClassName = L"PrivacyOverlaySpike";
    if (!RegisterClassExW(&wc))
    {
        printf("[ERROR] RegisterClassEx failed GLE=%lu\n", GetLastError());
        return 1;
    }

    // 主显示器整屏矩形
    RECT rc{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, nullptr, 0); // warm-up no-op
    POINT ptOrigin{ 0, 0 };
    const HMONITOR hmon = MonitorFromPoint(ptOrigin, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(hmon, &mi))
    {
        printf("[ERROR] GetMonitorInfo failed GLE=%lu\n", GetLastError());
        return 1;
    }
    rc = mi.rcMonitor;
    printf("[INFO] Primary monitor: %ls rect=(%ld,%ld,%ld,%ld)\n", mi.szDevice,
           rc.left, rc.top, rc.right, rc.bottom);

    constexpr DWORD exStyle =
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE |
        WS_EX_LAYERED | WS_EX_TRANSPARENT;

    HWND hwnd = CreateWindowExW(
        exStyle, L"PrivacyOverlaySpike", L"", WS_POPUP,
        rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
        nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd)
    {
        printf("[ERROR] CreateWindowEx failed GLE=%lu\n", GetLastError());
        return 1;
    }
    printf("[INFO] HWND=0x%p\n", static_cast<void*>(hwnd));

    SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);

    if (!SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE))
    {
        printf("[ERROR] SetWindowDisplayAffinity failed GLE=%lu\n", GetLastError());
        printf("[ERROR] Capture exclusion failed. Privacy protection NOT ACTIVE.\n");
        DestroyWindow(hwnd);
        return 3;
    }
    printf("[INFO] SetWindowDisplayAffinity: WDA_EXCLUDEFROMCAPTURE SUCCESS\n");

    DWORD affinity = 0;
    if (!GetWindowDisplayAffinity(hwnd, &affinity))
    {
        printf("[ERROR] GetWindowDisplayAffinity failed GLE=%lu\n", GetLastError());
        DestroyWindow(hwnd);
        return 3;
    }
    printf("[INFO] GetWindowDisplayAffinity: 0x%08lX\n", affinity);

    if (affinity != WDA_EXCLUDEFROMCAPTURE)
    {
        printf("[ERROR] Capture exclusion verification FAILED (affinity mismatch)\n");
        DestroyWindow(hwnd);
        return 3;
    }
    printf("[INFO] Capture exclusion VERIFIED\n");

    SetWindowPos(hwnd, HWND_TOPMOST,
                 rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);

    // 消息循环 5 秒后自动退出（方便用户肉眼确认物理屏效果）
    MSG msg{};
    DWORD start = GetTickCount();
    while (GetTickCount() - start < 5000)
    {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(16);
    }

    printf("[INFO] Spike done. Physical screen should have been BLACK for 5s.\n");
    DestroyWindow(hwnd);
    return 0;
}
