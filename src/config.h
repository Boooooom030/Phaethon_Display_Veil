#pragma once
// config.h - project constants and shared enums
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace cfg {

constexpr wchar_t kMutexName[]         = L"Local\\Phaethon.Singleton";
constexpr wchar_t kPipeName[]          = L"\\\\.\\pipe\\Phaethon";
constexpr wchar_t kOverlayClassName[]  = L"Phaethon.Overlay";
constexpr wchar_t kMessageClassName[]  = L"Phaethon.Message";
constexpr wchar_t kLogDirName[]        = L"Phaethon";
constexpr wchar_t kLogFileName[]       = L"phaethon.log";
constexpr wchar_t kAppTitle[]          = L"Phaethon";

// UI 线程自定义消息
constexpr UINT kMsgIpcRequest   = WM_APP + 1;  // wParam = ipc::Request*
constexpr UINT kMsgTrayCallback = WM_APP + 2;  // 托盘图标回调

// 热键
constexpr int  kHotkeyIdToggle = 1;
constexpr int  kHotkeyIdPanic  = 2;
constexpr UINT kHotkeyMods     = MOD_CONTROL | MOD_ALT | MOD_SHIFT;
constexpr UINT kHotkeyToggleVK = 'B';
constexpr UINT kHotkeyPanicVK  = VK_F10;

// 顶层重断言定时器
constexpr UINT kTimerReassertId      = 1;
constexpr UINT kTimerReassertPeriodMs = 5000;

// 图片幻灯片切换定时器
constexpr UINT kTimerSlideId       = 2;
constexpr UINT kTimerSlidePeriodMs = 30000;

// 最低系统：Windows 10 2004 (build 19041) —— WDA_EXCLUDEFROMCAPTURE 正式支持起点
constexpr ULONG kMinOsBuild = 19041;

// 退出码（AGENTS.md 行为契约）
enum class ExitCode : int {
    Ok                    = 0,
    GeneralError          = 1,
    NoServer              = 2,
    CaptureExclusionFailed= 3,
    MonitorNotFound       = 4,
    UnsupportedOs         = 5,
};

} // namespace cfg
