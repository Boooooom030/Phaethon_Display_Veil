#include "app.h"
#include "config.h"
#include "images/image_store.h"
#include "ipc/pipe_server.h"
#include "monitor/monitor_manager.h"
#include "overlay/overlay_manager.h"
#include "overlay/overlay_window.h"
#include "util/i18n.h"
#include "util/logger.h"
#include <objidl.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <shlobj.h>
#include <cwchar>

// RtlGetVersion 未随常规 SDK 头文件声明；手动原型（ntdll.dll 导出，文档化）
extern "C" LONG NTAPI RtlGetVersion(PRTL_OSVERSIONINFOW lpVersionInformation);

namespace app {

namespace {

constexpr UINT kTrayUid = 0x5053; // "PS"

// IPC 请求 → UI 线程同步执行的载荷（kMsgIpcRequest 的 lParam）
struct IpcPayload {
    ipc::Sync*     sync;
    ipc::Request   req;
};

// 消息窗口句柄（pipe server 线程需要）
HWND g_serverHwnd = nullptr;

// ---------- IPC 请求处理（UI 线程） ----------

ipc::Response HandleIpcRequest(const ipc::Request& req)
{
    ipc::Response resp;
    auto& mgr  = overlay::GetManager();
    auto& log  = util::Logger::Instance();

    switch (req.kind)
    {
    case ipc::Request::Kind::On:
    {
        // 图片目录（可空 = 纯黑模式）
        if (!req.imageDir.empty())
        {
            const size_t n = images::GetStore().SetDir(req.imageDir);
            if (n == 0)
            {
                resp.ok   = false;
                resp.text = L"image folder not found or empty: " + req.imageDir;
                log.Error(resp.text);
                break;
            }
        }
        else if (req.imageDirSet)
        {
            images::GetStore().Clear();
        }

        const auto targets = monitor::Select(req.monitorSpec);
        if (targets.empty())
        {
            resp.ok   = false;
            resp.text = L"monitor not found: " + req.monitorSpec;
            log.Error(resp.text);
            break;
        }
        for (const auto& m : targets)
            log.Info(L"Target: " + monitor::Describe(m));

        const auto report = mgr.Enable(targets, req.monitorSpec);
        if (!report.success)
        {
            resp.ok   = false;
            resp.text = report.error;
            // fail-safe：状态已置 ERROR，黑窗绝不残留；off/热键仍可恢复
        }
        break;
    }
    case ipc::Request::Kind::Images:
    {
        // 运行中切换图片库；ON 状态下立即重绘
        size_t n = 0;
        if (req.imageDir.empty())
        {
            images::GetStore().Clear();
        }
        else
        {
            n = images::GetStore().SetDir(req.imageDir);
            if (n == 0)
            {
                resp.ok   = false;
                resp.text = L"image folder not found or empty: " + req.imageDir;
                break;
            }
        }
        if (mgr.state() == overlay::State::On)
            mgr.InvalidateAllOverlays();
        resp.text = L"images=" + std::to_wstring(n);
        break;
    }
    case ipc::Request::Kind::Off:
        mgr.Disable(false);
        break;

    case ipc::Request::Kind::Toggle:
    {
        // toggle 可带 --images：若 ON 中则只换图源，否则开关
        if (!req.imageDir.empty() && mgr.state() == overlay::State::On)
        {
            const size_t n = images::GetStore().SetDir(req.imageDir);
            if (n == 0) { resp.ok = false; resp.text = L"image folder not found or empty: " + req.imageDir; break; }
            mgr.InvalidateAllOverlays();
            break;
        }
        if (mgr.state() == overlay::State::On)
        {
            mgr.Disable(false);
        }
        else
        {
            if (!req.imageDir.empty())
            {
                const size_t n = images::GetStore().SetDir(req.imageDir);
                if (n == 0) { resp.ok = false; resp.text = L"image folder not found or empty: " + req.imageDir; break; }
            }
            const auto targets = monitor::Select(L"all");
            if (targets.empty())
            {
                resp.ok   = false;
                resp.text = L"no monitors to cover";
                break;
            }
            const auto report = mgr.Enable(targets, L"all");
            if (!report.success) { resp.ok = false; resp.text = report.error; }
        }
        break;
    }

    case ipc::Request::Kind::Status:
        resp.text = mgr.StatusText();
        if (images::GetStore().HasImages())
            resp.text += L"\n  images=" + std::to_wstring(images::GetStore().Count()) +
                         L" from " + images::GetStore().Dir();
        break;

    case ipc::Request::Kind::Exit:
        PostMessageW(g_serverHwnd, WM_CLOSE, 0, 0);
        break;
    }
    return resp;
}

// ---------- 托盘 ----------

// 菜单命令 ID 布局
namespace menuid {
constexpr int ToggleSwitch   = 10;  // 顶部开关：勾选=ON，点击=开/关
constexpr int AllScreens     = 20;  // 子菜单：全部屏幕
constexpr int FirstMonitor   = 100; // 100 + 枚举序号：单屏
constexpr int PickImages     = 30;  // 选择图片文件夹
constexpr int BackToBlack    = 31;  // 恢复纯黑
constexpr int Status         = 40;
constexpr int Exit           = 41;
} // namespace menuid

// 系统文件夹选择对话框；返回所选目录（取消/失败返回空）
std::wstring PickFolderDialog(HWND owner)
{
    std::wstring result;
    wchar_t path[MAX_PATH]{};

    BROWSEINFOW bi{};
    bi.hwndOwner      = owner;
    bi.lpszTitle      = i18n::Str(i18n::S::FolderDialogTitle);
    bi.ulFlags        = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_USENEWUI;
    bi.lpfn           = nullptr;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (pidl)
    {
        if (SHGetPathFromIDListW(pidl, path))
            result = path;
        CoTaskMemFree(pidl);
    }
    return result;
}

void ShowTrayMenu(HWND hwnd)
{
    auto& mgr = overlay::GetManager();
    const bool on   = mgr.state() == overlay::State::On;
    const bool imgs = images::GetStore().HasImages();

    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    // ---- 顶部：单独的总开关 ----
    AppendMenuW(menu, MF_STRING | (on ? MF_CHECKED : 0), menuid::ToggleSwitch,
                i18n::Str(i18n::S::EnablePrivacy));

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    // ---- 选择屏幕子菜单：全部屏幕 + 每屏一项（勾选显示当前覆盖状态）----
    const auto monitors = monitor::Enumerate();
    HMENU subMon = CreatePopupMenu();
    if (subMon)
    {
        AppendMenuW(subMon, MF_STRING | (mgr.IsAllCovered() ? MF_CHECKED : 0),
                    menuid::AllScreens, i18n::Str(i18n::S::AllScreens));
        AppendMenuW(subMon, MF_SEPARATOR, 0, nullptr);
        for (size_t i = 0; i < monitors.size(); ++i)
        {
            std::wstring label = std::wstring(i18n::Str(i18n::S::MonitorPrefix)) +
                                 std::to_wstring(i + 1) +
                                 L"  " + monitors[i].device;
            if (monitors[i].primary)
                label += i18n::Str(i18n::S::PrimaryTag);
            AppendMenuW(subMon,
                        MF_STRING | (mgr.IsCovered(monitors[i].device) ? MF_CHECKED : 0),
                        menuid::FirstMonitor + static_cast<int>(i), label.c_str());
        }
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(subMon),
                    i18n::Str(i18n::S::SelectScreen));
    }

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, menuid::PickImages,
                imgs ? i18n::Str(i18n::S::ChangeImages)
                     : i18n::Str(i18n::S::ChooseImages));
    AppendMenuW(menu, MF_STRING | (imgs ? MF_CHECKED : 0), menuid::BackToBlack,
                i18n::Str(i18n::S::BackToBlack));

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, menuid::Status, i18n::Str(i18n::S::Status));
    AppendMenuW(menu, MF_STRING, menuid::Exit,   i18n::Str(i18n::S::Exit));

    POINT pt{};
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd); // 托盘菜单显示的 Win32 惯例
    const int cmd = TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_RETURNCMD,
                                   pt.x, pt.y, 0, hwnd, nullptr);
    PostMessageW(hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);

    // ---- 处理选择 ----
    // 单屏选择：直接按设备名启用（保留图片库）
    if (cmd >= menuid::FirstMonitor &&
        cmd < menuid::FirstMonitor + static_cast<int>(monitors.size()))
    {
        const auto& m = monitors[static_cast<size_t>(cmd - menuid::FirstMonitor)];
        if (on && mgr.IsCovered(m.device) && !mgr.IsAllCovered())
        {
            // 已仅遮这块屏 → 再点一次 = 关闭
            mgr.Disable(false);
            return;
        }
        ipc::Request req;
        req.kind        = ipc::Request::Kind::On;
        req.monitorSpec = m.device;
        const ipc::Response r = HandleIpcRequest(req);
        if (!r.ok) util::Logger::Instance().Error(L"tray monitor-select failed: " + r.text);
        return;
    }

    ipc::Request req;
    switch (cmd)
    {
    case menuid::ToggleSwitch:
        if (on) mgr.Disable(false);
        else
        {
            const std::wstring spec = mgr.CurrentSpec().empty() ? L"all" : mgr.CurrentSpec();
            const auto targets = monitor::Select(spec);
            if (!targets.empty())
                mgr.Enable(targets, spec); // 失败时 Enable 内部已置 ERROR 并记日志
        }
        return; // 已处理
    case menuid::AllScreens:
        req.kind        = ipc::Request::Kind::On;
        req.monitorSpec = L"all";
        break;
    case menuid::PickImages:
    {
        const std::wstring dir = PickFolderDialog(hwnd);
        if (dir.empty()) return; // 用户取消
        req.kind     = ipc::Request::Kind::Images;
        req.imageDir = dir;
        break;
    }
    case menuid::BackToBlack:
        req.kind = ipc::Request::Kind::Images;
        req.imageDir.clear();
        break;
    case menuid::Status:
        req.kind = ipc::Request::Kind::Status;
        break;
    case menuid::Exit:
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
        return;
    default:
        return;
    }
    const ipc::Response resp = HandleIpcRequest(req);
    if (!resp.ok)
        util::Logger::Instance().Error(L"tray command failed: " + resp.text);
}

// ---------- 隐藏消息窗口的 WindowProc ----------

LRESULT CALLBACK MessageWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    auto& log = util::Logger::Instance();

    switch (msg)
    {
    case WM_CREATE:
    {
        if (!RegisterHotKey(hwnd, cfg::kHotkeyIdToggle,
                            cfg::kHotkeyMods, cfg::kHotkeyToggleVK))
            log.Warn(L"RegisterHotKey(toggle) failed GLE=" + std::to_wstring(GetLastError()));
        if (!RegisterHotKey(hwnd, cfg::kHotkeyIdPanic,
                            cfg::kHotkeyMods, cfg::kHotkeyPanicVK))
            log.Warn(L"RegisterHotKey(panic) failed GLE=" + std::to_wstring(GetLastError()));

        NOTIFYICONDATAW nid{};
        nid.cbSize           = sizeof(nid);
        nid.hWnd             = hwnd;
        nid.uID              = kTrayUid;
        nid.uFlags           = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        nid.uCallbackMessage = cfg::kMsgTrayCallback;
        nid.hIcon            = LoadIconW(nullptr, IDI_APPLICATION);
        wcsncpy_s(nid.szTip, i18n::Str(i18n::S::TrayTip), _TRUNCATE);
        if (!Shell_NotifyIconW(NIM_ADD, &nid))
            log.Warn(L"Shell_NotifyIcon failed GLE=" + std::to_wstring(GetLastError()));

        SetTimer(hwnd, cfg::kTimerReassertId, cfg::kTimerReassertPeriodMs, nullptr);
        SetTimer(hwnd, cfg::kTimerSlideId, cfg::kTimerSlidePeriodMs, nullptr);
        return 0;
    }

    case cfg::kMsgIpcRequest:
    {
        // pipe_server 以 WPARAM 投递 PendingRequest*
        auto* p = reinterpret_cast<ipc::PendingRequest*>(wParam);
        if (p)
        {
            p->sync->resp = HandleIpcRequest(p->req);
            SetEvent(static_cast<HANDLE>(p->sync->done));
        }
        return 0;
    }

    case cfg::kMsgTrayCallback:
        if (LOWORD(lParam) == WM_LBUTTONDBLCLK || LOWORD(lParam) == WM_RBUTTONUP)
            ShowTrayMenu(hwnd);
        return 0;

    case WM_HOTKEY:
        if (wParam == cfg::kHotkeyIdToggle)
        {
            ipc::Request req;
            req.kind = ipc::Request::Kind::Toggle;
            const ipc::Response r = HandleIpcRequest(req);
            if (!r.ok) log.Error(L"hotkey toggle failed: " + r.text);
        }
        else if (wParam == cfg::kHotkeyIdPanic)
        {
            overlay::GetManager().Disable(true); // 紧急无条件关闭
        }
        return 0;

    case WM_TIMER:
        if (wParam == cfg::kTimerReassertId)
            overlay::GetManager().Reassert();
        else if (wParam == cfg::kTimerSlideId)
        {
            // 幻灯片：仅在 ON + 图片模式时切换
            if (overlay::GetManager().state() == overlay::State::On &&
                images::GetStore().HasImages() && images::GetStore().Count() > 1)
            {
                images::GetStore().Next();
                overlay::GetManager().InvalidateAllOverlays();
            }
        }
        return 0;

    case WM_DISPLAYCHANGE:
    case WM_DPICHANGED:
        log.Info(L"display/DPI change received, rebuilding overlays");
        overlay::GetManager().HandleDisplayChange();
        return 0;

    case WM_DESTROY:
    {
        overlay::GetManager().Disable(true);
        KillTimer(hwnd, cfg::kTimerReassertId);
        KillTimer(hwnd, cfg::kTimerSlideId);
        NOTIFYICONDATAW nid{};
        nid.cbSize = sizeof(nid);
        nid.hWnd   = hwnd;
        nid.uID    = kTrayUid;
        Shell_NotifyIconW(NIM_DELETE, &nid);
        UnregisterHotKey(hwnd, cfg::kHotkeyIdToggle);
        UnregisterHotKey(hwnd, cfg::kHotkeyIdPanic);
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

int RunServer(bool ddaFallback, bool debug)
{
    auto& log = util::Logger::Instance();
    log.Init(debug);
    i18n::Init();
    overlay::GetManager().SetDdaFallback(ddaFallback);

    // GDI+：图片模式需要（进程生命周期内一次性初始化）
    Gdiplus::GdiplusStartupInput gdiStartup{};
    ULONG_PTR gdiToken = 0;
    if (Gdiplus::GdiplusStartup(&gdiToken, &gdiStartup, nullptr) != Gdiplus::Ok)
        log.Warn(L"GdiplusStartup failed; image mode unavailable (black overlay still works)");

    // 版本检查：WDA_EXCLUDEFROMCAPTURE 需要 Win10 2004 (build 19041)+
    RTL_OSVERSIONINFOW v{};
    v.dwOSVersionInfoSize = sizeof(v);
    if (RtlGetVersion(&v) != 0 || v.dwBuildNumber < cfg::kMinOsBuild)
    {
        log.Error(L"OS build " + std::to_wstring(v.dwBuildNumber) +
                  L" < " + std::to_wstring(cfg::kMinOsBuild) +
                  L" (Win10 2004): refusing to enable");
        return static_cast<int>(cfg::ExitCode::UnsupportedOs);
    }
    log.Info(L"Windows build " + std::to_wstring(v.dwBuildNumber) + L" OK");

    // DPI：任何窗口创建之前
    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
        log.Warn(L"SetProcessDpiAwarenessContext failed GLE=" +
                 std::to_wstring(GetLastError()));
    else
        log.Info(L"DPI awareness: PER_MONITOR_AWARE_V2");

    // 消息窗口类
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = MessageWndProc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.lpszClassName = cfg::kMessageClassName;
    if (!RegisterClassExW(&wc))
    {
        log.Error(L"RegisterClassEx(message) failed GLE=" + std::to_wstring(GetLastError()));
        return static_cast<int>(cfg::ExitCode::GeneralError);
    }

    // overlay 窗口类（黑色背景 + 自绘 WndProc）
    {
        WNDCLASSEXW owc{};
        owc.cbSize        = sizeof(owc);
        owc.lpfnWndProc   = overlay::WndProc;
        owc.hInstance     = GetModuleHandleW(nullptr);
        owc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        owc.lpszClassName = cfg::kOverlayClassName;
        if (!RegisterClassExW(&owc))
        {
            log.Error(L"RegisterClassEx(overlay) failed GLE=" +
                      std::to_wstring(GetLastError()));
            return static_cast<int>(cfg::ExitCode::GeneralError);
        }
    }

    // 隐藏消息窗口（不显示、不进任务栏）
    const HWND hwnd = CreateWindowExW(
        0, cfg::kMessageClassName, cfg::kAppTitle,
        0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hwnd)
    {
        log.Error(L"CreateWindowEx(message) failed GLE=" + std::to_wstring(GetLastError()));
        return static_cast<int>(cfg::ExitCode::GeneralError);
    }
    g_serverHwnd = hwnd;

    // IPC server
    if (!ipc::StartServer(hwnd,
                          [](const std::wstring& err) {
                              util::Logger::Instance().Error(err);
                          }))
    {
        log.Error(L"pipe server start failed");
        return static_cast<int>(cfg::ExitCode::GeneralError);
    }
    log.Info(L"IPC server ready: " + std::wstring(cfg::kPipeName));
    log.Info(L"Server running. Hotkeys: Ctrl+Alt+Shift+B toggle, Ctrl+Alt+Shift+F10 panic-off");

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    ipc::StopServer();
    g_serverHwnd = nullptr;
    images::GetStore().Clear();
    if (gdiToken)
        Gdiplus::GdiplusShutdown(gdiToken);
    log.Info(L"Server stopped cleanly");
    return static_cast<int>(cfg::ExitCode::Ok);
}

} // namespace app
