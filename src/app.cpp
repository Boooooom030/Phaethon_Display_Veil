// app.cpp - background server instance: message loop, hotkeys, tray, IPC wiring
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

// RtlGetVersion is not declared in the shipped SDK headers; documented
// ntdll.dll export, prototype declared manually.
extern "C" LONG NTAPI RtlGetVersion(PRTL_OSVERSIONINFOW lpVersionInformation);

namespace app {

namespace {

constexpr UINT kTrayUid = 0x5053; // "PS"

// IPC request carried as kMsgIpcRequest wParam
struct IpcPayload {
    ipc::Sync*     sync;
    ipc::Request   req;
};

// Hidden message window handle; also used by the pipe server thread
HWND g_serverHwnd = nullptr;

// Screen range pre-selected in the tray submenu while OFF (applied by the toggle)
std::wstring pendingSpec_;

// ---------- IPC request handling (UI thread) ----------

ipc::Response HandleIpcRequest(const ipc::Request& req)
{
    ipc::Response resp;
    auto& mgr  = overlay::GetManager();
    auto& log  = util::Logger::Instance();

    switch (req.kind)
    {
    case ipc::Request::Kind::On:
    {
        // Image folder (empty = plain black)
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
        }
        break;
    }
    case ipc::Request::Kind::Images:
    {
        // Hot-swap the image library; redraw immediately while ON
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
        // toggle --images while ON only swaps the image source
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

// ---------- Tray ----------

namespace menuid {
constexpr int ToggleSwitch   = 10;  // top-level switch: checked = ON
constexpr int AllScreens     = 20;  // submenu entry
constexpr int FirstMonitor   = 100; // 100 + enumeration index
constexpr int PickImages     = 30;  // choose image folder
constexpr int BackToBlack    = 31;  // clear image library
constexpr int Exit           = 41;
} // namespace menuid

// Vista+ IFileDialog folder picker (resizable, DPI-aware).
// Returns the selected directory, empty on cancel/failure.
std::wstring PickFolderDialog(HWND owner)
{
    std::wstring result;

    // The dialog requires COM (STA) on this thread
    const HRESULT hrInit = CoInitializeEx(nullptr,
                                          COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool comInited = SUCCEEDED(hrInit);
    if (!comInited && hrInit != RPC_E_CHANGED_MODE)
        return result;

    IFileOpenDialog* dlg = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(FileOpenDialog), nullptr, CLSCTX_INPROC_SERVER,
                                  __uuidof(IFileOpenDialog),
                                  reinterpret_cast<void**>(&dlg));
    if (SUCCEEDED(hr) && dlg)
    {
        DWORD opts = 0;
        dlg->GetOptions(&opts);
        dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        dlg->SetTitle(i18n::Str(i18n::S::FolderDialogTitle));

        if (SUCCEEDED(dlg->Show(owner)))
        {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dlg->GetResult(&item)) && item)
            {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path)
                {
                    result = path;
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }
        dlg->Release();
    }

    if (comInited)
        CoUninitialize();
    return result;
}

void ShowTrayMenu(HWND hwnd)
{
    auto& mgr = overlay::GetManager();
    const bool on   = mgr.state() == overlay::State::On;
    const bool imgs = images::GetStore().HasImages();

    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    // Top-level switch
    AppendMenuW(menu, MF_STRING | (on ? MF_CHECKED : 0), menuid::ToggleSwitch,
                i18n::Str(i18n::S::EnablePrivacy));

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    // Screen submenu: All screens + one entry per monitor, check state
    // reflects the covered range (or the pre-selection while OFF)
    const auto monitors = monitor::Enumerate();
    HMENU subMon = CreatePopupMenu();
    if (subMon)
    {
        AppendMenuW(subMon, MF_STRING | (mgr.IsAllCovered() ||
                                         (!on && pendingSpec_ == L"all") ? MF_CHECKED : 0),
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
                        MF_STRING | ((mgr.IsCovered(monitors[i].device) ||
                                      (!on && pendingSpec_ == monitors[i].device))
                                         ? MF_CHECKED : 0),
                        menuid::FirstMonitor + static_cast<int>(i), label.c_str());
        }
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(subMon),
                    i18n::Str(i18n::S::SelectScreen));
    }

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, menuid::PickImages,
                imgs ? i18n::Str(i18n::S::ChangeImages)
                     : i18n::Str(i18n::S::ChooseImages));
    AppendMenuW(menu, MF_STRING, menuid::BackToBlack,
                i18n::Str(i18n::S::BackToBlack));

    AppendMenuW(menu, MF_STRING, menuid::Exit,   i18n::Str(i18n::S::Exit));

    POINT pt{};
    GetCursorPos(&pt);
    // Foreground transfer is required for TrackPopupMenu to dismiss correctly
    SetForegroundWindow(hwnd);
    const int cmd = TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_RETURNCMD,
                                   pt.x, pt.y, 0, hwnd, nullptr);
    PostMessageW(hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);

    // Per-monitor entry: remember the choice; apply immediately while ON
    if (cmd >= menuid::FirstMonitor &&
        cmd < menuid::FirstMonitor + static_cast<int>(monitors.size()))
    {
        const auto& m = monitors[static_cast<size_t>(cmd - menuid::FirstMonitor)];
        if (on)
        {
            if (mgr.IsCovered(m.device) && !mgr.IsAllCovered())
            {
                // Clicking the only covered monitor again turns it off
                mgr.Disable(false);
            }
            else
            {
                ipc::Request req;
                req.kind        = ipc::Request::Kind::On;
                req.monitorSpec = m.device;
                const ipc::Response r = HandleIpcRequest(req);
                if (!r.ok) util::Logger::Instance().Error(L"tray monitor-select failed: " + r.text);
            }
        }
        else
        {
            // OFF: remember the selection, applied by the top-level switch
            pendingSpec_ = m.device;
        }
        return;
    }

    ipc::Request req;
    switch (cmd)
    {
    case menuid::ToggleSwitch:
        if (on) mgr.Disable(false);
        else
        {
            const std::wstring spec = mgr.CurrentSpec().empty()
                                          ? (pendingSpec_.empty() ? L"all" : pendingSpec_)
                                          : mgr.CurrentSpec();
            const auto targets = monitor::Select(spec);
            if (!targets.empty())
                mgr.Enable(targets, spec); // on failure state becomes ERROR, logged inside
        }
        return;
    case menuid::AllScreens:
        // Remember the selection; apply immediately while ON
        pendingSpec_ = L"all";
        if (on)
        {
            req.kind        = ipc::Request::Kind::On;
            req.monitorSpec = L"all";
            break;
        }
        return;
    case menuid::PickImages:
    {
        const std::wstring dir = PickFolderDialog(hwnd);
        if (dir.empty()) return; // canceled
        req.kind     = ipc::Request::Kind::Images;
        req.imageDir = dir;
        break;
    }
    case menuid::BackToBlack:
        req.kind = ipc::Request::Kind::Images;
        req.imageDir.clear();
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

// ---------- Hidden message window ----------

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
        nid.hIcon            = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1));
        if (!nid.hIcon)
            nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        wcsncpy_s(nid.szTip, cfg::kAppTitle, _TRUNCATE);
        if (!Shell_NotifyIconW(NIM_ADD, &nid))
            log.Warn(L"Shell_NotifyIcon failed GLE=" + std::to_wstring(GetLastError()));

        SetTimer(hwnd, cfg::kTimerReassertId, cfg::kTimerReassertPeriodMs, nullptr);
        SetTimer(hwnd, cfg::kTimerSlideId, cfg::kTimerSlidePeriodMs, nullptr);
        return 0;
    }

    case cfg::kMsgIpcRequest:
    {
        // pipe_server posts PendingRequest* in wParam
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
            overlay::GetManager().Disable(true); // unconditional shutdown
        }
        return 0;

    case WM_TIMER:
        if (wParam == cfg::kTimerReassertId)
            overlay::GetManager().Reassert();
        else if (wParam == cfg::kTimerSlideId)
        {
            // Slideshow: only advance while ON with more than one image
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

    // GDI+ for image mode; black-only mode still works if this fails
    Gdiplus::GdiplusStartupInput gdiStartup{};
    ULONG_PTR gdiToken = 0;
    if (Gdiplus::GdiplusStartup(&gdiToken, &gdiStartup, nullptr) != Gdiplus::Ok)
        log.Warn(L"GdiplusStartup failed; image mode unavailable (black overlay still works)");

    // WDA_EXCLUDEFROMCAPTURE requires Windows 10 2004 (build 19041)+
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

    // Must precede any window creation
    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
        log.Warn(L"SetProcessDpiAwarenessContext failed GLE=" +
                 std::to_wstring(GetLastError()));
    else
        log.Info(L"DPI awareness: PER_MONITOR_AWARE_V2");

    // Message-only window class
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = MessageWndProc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.hIcon         = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1));
    wc.hIconSm       = wc.hIcon;
    wc.lpszClassName = cfg::kMessageClassName;
    if (!RegisterClassExW(&wc))
    {
        log.Error(L"RegisterClassEx(message) failed GLE=" + std::to_wstring(GetLastError()));
        return static_cast<int>(cfg::ExitCode::GeneralError);
    }

    // Overlay window class (black background + custom WndProc)
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

    const HWND hwnd = CreateWindowExW(
        0, cfg::kMessageClassName, cfg::kAppTitle,
        0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hwnd)
    {
        log.Error(L"CreateWindowEx(message) failed GLE=" + std::to_wstring(GetLastError()));
        return static_cast<int>(cfg::ExitCode::GeneralError);
    }
    g_serverHwnd = hwnd;

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
