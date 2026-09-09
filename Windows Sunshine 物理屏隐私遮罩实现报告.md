# Windows Sunshine 物理屏隐私遮罩实现报告

## 1. 项目目标

开发一个 Windows 本地程序，实现以下效果：

> 当 Sunshine / Moonlight 正在串流时，电脑的**实体显示器显示纯黑遮罩**，使站在电脑旁边的人无法看到桌面、游戏或其他应用内容；与此同时，Sunshine 的串流画面**不能包含这个黑色遮罩**，Moonlight 客户端应继续正常看到真实桌面/游戏内容。

明确约束：

- **不能关闭实体显示器**
- **不能禁用实体显示器**
- 不使用虚拟显示器作为主要方案
- 不切换 Windows 显示拓扑
- 不改变当前桌面分辨率
- 不移动游戏/应用到其他显示器
- 遮罩必须运行在当前登录用户桌面
- Moonlight 的键盘、鼠标、手柄输入必须继续正常作用于底层程序
- 遮罩程序本身不能抢占焦点
- 支持多显示器
- 第一版优先实现稳定性，而不是 UI

推荐实现语言：

```text
C++20 + 原生 Win32 API
```

不需要 Qt、Electron、WebView、WPF 等 GUI 框架。

最终目标是一个小型原生 EXE。

---

# 2. 核心技术方案

核心使用 Windows API：

```cpp
SetWindowDisplayAffinity(
    hwnd,
    WDA_EXCLUDEFROMCAPTURE
);
```

其中：

```cpp
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
```

Microsoft 对该值的定义是：

- 窗口正常显示在本地 monitor 上；
- 在支持该机制的屏幕捕获中，该窗口不会出现；
- Windows 10 Version 2004 开始正式支持。

该函数只能作用于**当前进程拥有的顶级窗口**，并要求 DWM 正常参与桌面合成。

因此程序创建一个覆盖整个实体显示器的纯黑顶级窗口：

```text
Windows Desktop
│
├── 游戏 / 桌面 / 应用
│
└── Privacy Overlay
      ├── 纯黑
      ├── TopMost
      ├── 不获取焦点
      ├── 鼠标穿透
      └── WDA_EXCLUDEFROMCAPTURE
```

预期效果：

```text
实体屏幕：

┌──────────────────────────┐
│                          │
│          BLACK           │
│                          │
└──────────────────────────┘


Sunshine Desktop Duplication：

┌──────────────────────────┐
│                          │
│       REAL DESKTOP       │
│      GAME / WINDOWS      │
│                          │
└──────────────────────────┘
```

---

# 3. 与 Sunshine 的关系

当前 Sunshine Windows 版本支持：

```text
ddx = DirectX Desktop Duplication API
wgc = Windows.Graphics.Capture
```

其中官方配置文档将 `ddx` 描述为 Windows 上支持成熟的 Desktop Duplication 捕获方式，而 `wgc` 当前仍属于 beta，并且不兼容 Sunshine service 模式。

Sunshine Windows Desktop Duplication 实现内部实际调用：

```cpp
IDXGIOutput5::DuplicateOutput1(...)
```

或者：

```cpp
IDXGIOutput1::DuplicateOutput(...)
```

随后通过：

```cpp
AcquireNextFrame(...)
```

获取桌面帧。

因此第一阶段开发和验收时，应明确使用：

```ini
capture = ddx
```

不要一开始依赖 Sunshine 自动选择捕获方式。

第一版测试成功后，再额外测试：

```ini
capture = wgc
```

但 WGC 兼容性不是 MVP 的阻塞条件。

---

# 4. Windows 还有一个 DDA 专用机制

Windows 还有一个与本项目非常相关的 composition attribute：

```cpp
WCA_EXCLUDED_FROM_DDA = 24
```

Microsoft 对它的说明是：

> Prevents a window from being captured by the Desktop Duplication API.

即：

```text
禁止该窗口进入 Desktop Duplication API。
```

该属性至少从 Windows 10 1709 起存在。

但是不要把它作为第一实现。

优先级应该是：

```text
1. WDA_EXCLUDEFROMCAPTURE
2. 可选 WCA_EXCLUDED_FROM_DDA compatibility fallback
```

原因：

- `WDA_EXCLUDEFROMCAPTURE` 是更标准、更公开的应用接口；
- 不只针对 Desktop Duplication；
- 将来 Sunshine 使用 WGC 时仍更合理；
- `WCA_EXCLUDED_FROM_DDA` 可以作为针对 Sunshine `ddx` 的额外 fallback。

建议增加可选参数：

```text
--dda-fallback
```

开启后同时尝试：

```text
WDA_EXCLUDEFROMCAPTURE
+
WCA_EXCLUDED_FROM_DDA
```

默认不要依赖第二个机制。

---

# 5. Overlay 窗口设计

每个需要遮挡的 monitor 创建**一个独立顶级窗口**。

不要创建一个跨整个 Virtual Desktop 的巨型窗口。

原因：

- 多屏坐标可以出现负数；
- 每个显示器分辨率/DPI 不同；
- Sunshine 可能只捕获其中一个显示器；
- 显示器热插拔处理更简单；
- 可以单独启用/禁用某块屏幕。

窗口基础样式：

```cpp
DWORD style =
    WS_POPUP;

DWORD exStyle =
    WS_EX_TOPMOST |
    WS_EX_TOOLWINDOW |
    WS_EX_NOACTIVATE |
    WS_EX_LAYERED |
    WS_EX_TRANSPARENT;
```

窗口必须：

```text
无标题栏
无边框
无系统菜单
不出现在任务栏
不出现在 Alt+Tab
不激活
始终置顶
视觉上完全不透明
鼠标穿透
```

创建后执行：

```cpp
SetLayeredWindowAttributes(
    hwnd,
    0,
    255,
    LWA_ALPHA
);
```

注意：

```text
alpha = 255
```

即窗口仍然视觉上完全不透明。

`WS_EX_LAYERED + WS_EX_TRANSPARENT` 的目的不是让画面透明，而是让鼠标事件穿过该窗口。

Microsoft 文档明确说明，对于 layered window，如果设置 `WS_EX_TRANSPARENT`，鼠标事件会传递给下面的窗口。

---

# 6. 黑色渲染

不需要 DirectX。

MVP 直接使用 Win32 绘制纯黑即可。

例如注册 window class 时：

```cpp
hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
```

并处理：

```cpp
WM_ERASEBKGND
WM_PAINT
```

确保：

```text
整个 client area = RGB(0,0,0)
```

不要：

- 使用半透明
- 使用 Blur
- 使用 Acrylic
- 使用 DirectComposition
- 使用截图
- 使用 Desktop Duplication 自己抓屏
- 使用 GPU shader

这个程序只需要显示黑色。

---

# 7. Capture Exclusion

窗口创建完成后立即：

```cpp
BOOL result = SetWindowDisplayAffinity(
    hwnd,
    WDA_EXCLUDEFROMCAPTURE
);
```

必须检查返回值。

失败：

```cpp
DWORD error = GetLastError();
```

并记录日志。

随后调用：

```cpp
DWORD affinity = 0;

GetWindowDisplayAffinity(
    hwnd,
    &affinity
);
```

验证：

```cpp
affinity == WDA_EXCLUDEFROMCAPTURE
```

如果不是，视为隐私遮罩初始化失败。

### Fail-safe 原则

非常重要：

如果：

```text
SetWindowDisplayAffinity()
```

失败，则**不要默认显示黑窗并声称功能工作正常**。

应该：

```text
显示明显错误通知
+
日志
+
状态标记 = UNSAFE
```

因为这种情况下 Sunshine 很可能会直接串流黑屏。

程序应能输出例如：

```text
[ERROR] Capture exclusion failed.
GetLastError = 87
Privacy protection NOT ACTIVE.
```

---

# 8. 显示器枚举

使用：

```cpp
EnumDisplayMonitors()
```

获取所有 monitor。

每个 monitor：

```cpp
MONITORINFOEXW info {};
GetMonitorInfoW(...);
```

使用：

```cpp
info.rcMonitor
```

而不是：

```cpp
info.rcWork
```

因为：

```text
rcMonitor = 整块屏幕
rcWork    = 排除 taskbar 的工作区
```

我们必须覆盖 Taskbar。

窗口位置：

```cpp
x = rcMonitor.left;
y = rcMonitor.top;

width =
    rcMonitor.right -
    rcMonitor.left;

height =
    rcMonitor.bottom -
    rcMonitor.top;
```

必须正确支持：

```text
Monitor 1: 0,0
Monitor 2: -2560,0
Monitor 3: 3840,-1080
```

不要假设所有坐标都是正数。

---

# 9. DPI

进程启动后的第一件事情之一：

```cpp
SetProcessDpiAwarenessContext(
    DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
);
```

必须在创建任何窗口之前调用。

否则：

```text
150% DPI
200% DPI
不同显示器混合 DPI
```

可能出现遮罩尺寸偏差，例如边缘露出几十或几百像素。

程序还应该处理：

```cpp
WM_DPICHANGED
```

以及：

```cpp
WM_DISPLAYCHANGE
```

收到显示布局变化后：

```text
重新 EnumDisplayMonitors
→ 删除无效 Overlay
→ resize 现存 Overlay
→ 创建新 Overlay
```

---

# 10. Z-Order

显示 Overlay：

```cpp
SetWindowPos(
    hwnd,
    HWND_TOPMOST,
    x,
    y,
    width,
    height,
    SWP_NOACTIVATE |
    SWP_SHOWWINDOW
);
```

必须：

```text
HWND_TOPMOST
SWP_NOACTIVATE
```

不能调用：

```cpp
SetForegroundWindow()
```

不能让 Privacy Overlay 成为 foreground window。

否则 Moonlight 输入可能错误地发送到 Overlay。

---

# 11. 输入穿透

目标：

```text
实体显示器：
看到黑色

Moonlight：
看到真实应用

Moonlight Input：
操作真实应用
```

Overlay 本身不能处理普通鼠标点击。

第一方案：

```text
WS_EX_LAYERED
+
WS_EX_TRANSPARENT
```

同时：

```text
WS_EX_NOACTIVATE
```

不要仅依赖：

```cpp
WM_NCHITTEST -> HTTRANSPARENT
```

因为 Microsoft 对 `HTTRANSPARENT` 的语义包含“同线程下面的窗口”限制，并不适合作为跨进程桌面 click-through 的唯一实现。

可以保留：

```cpp
case WM_MOUSEACTIVATE:
    return MA_NOACTIVATE;
```

作为额外保险。

---

# 12. 多显示器策略

程序必须支持：

```text
privacy-screen.exe on --all
privacy-screen.exe on --primary
privacy-screen.exe on --monitor 1
privacy-screen.exe on --monitor "\\.\DISPLAY2"
```

默认建议：

```text
--all
```

原因：

用户真正想解决的是：

> 旁边的人不能看到实体电脑上的任何内容。

因此最安全默认行为：

```text
全部实体桌面 monitor 显示黑色
```

由于这些 Overlay 都被 capture exclusion 排除，所以 Sunshine 捕获任意指定显示器时，都仍应得到底层真实桌面。

---

# 13. 程序运行模型

推荐使用一个常驻后台实例：

```text
PrivacyScreen.exe
```

启动后：

```text
Hidden Message Window
+
Tray Icon，可选
+
Monitor Manager
+
Overlay Manager
+
Global Hotkey
+
IPC
```

第二次运行 EXE 不创建第二套程序，而是作为控制客户端。

例如：

```powershell
PrivacyScreen.exe on
PrivacyScreen.exe off
PrivacyScreen.exe toggle
PrivacyScreen.exe status
```

控制命令发送给后台实例。

---

# 14. 单实例

使用：

```cpp
CreateMutexW()
```

例如：

```text
Local\SunshinePrivacyScreen.Singleton
```

如果已存在：

```text
不要启动第二个 server
```

IPC 推荐使用：

```text
Named Pipe
```

例如：

```text
\\.\pipe\SunshinePrivacyScreen
```

协议非常简单：

```json
{"command":"on"}
```

```json
{"command":"off"}
```

```json
{"command":"status"}
```

或者甚至直接发送 UTF-8：

```text
ON
OFF
TOGGLE
STATUS
```

不需要复杂 RPC。

---

# 15. CLI

必须实现：

```text
PrivacyScreen.exe
PrivacyScreen.exe on
PrivacyScreen.exe off
PrivacyScreen.exe toggle
PrivacyScreen.exe status
```

可选：

```text
PrivacyScreen.exe on --all
PrivacyScreen.exe on --primary
PrivacyScreen.exe on --monitor DISPLAY2
PrivacyScreen.exe --dda-fallback
PrivacyScreen.exe --debug
```

返回码建议：

```text
0 = success
1 = general error
2 = no server
3 = capture exclusion failed
4 = monitor not found
5 = unsupported OS
```

---

# 16. 全局快捷键

使用：

```cpp
RegisterHotKey()
```

推荐：

```text
Ctrl + Alt + Shift + B
```

功能：

```text
Toggle Black Privacy Overlay
```

再增加紧急恢复快捷键：

```text
Ctrl + Alt + Shift + F10
```

无条件：

```text
OFF
```

即使内部状态认为 overlay 已关闭，也执行：

```text
Destroy/Hide 所有 Overlay
```

这是故障恢复措施。

---

# 17. Sunshine 自动联动

Sunshine 官方支持：

```text
global_prep_cmd
```

以及每个应用自己的：

```text
prep-cmd:
    do
    undo
```

`do` 在应用启动前运行，而 `undo` 会在应用结束后运行。

因此程序完成以后，可以配置 Sunshine：

```json
{
  "do": "\"C:\\Tools\\PrivacyScreen\\PrivacyScreen.exe\" on",
  "undo": "\"C:\\Tools\\PrivacyScreen\\PrivacyScreen.exe\" off",
  "elevated": false
}
```

如果希望所有 Sunshine 应用都启用：

```text
global_prep_cmd
```

优于逐个游戏添加。

注意：

不要让：

```text
PrivacyScreen.exe on
```

一直阻塞。

命令必须：

```text
发送 IPC → 收到 ACK → 返回
```

否则可能阻塞 Sunshine 的 app startup。

推荐：

```text
Sunshine
   │
   ├── prep-command
   │      │
   │      └── PrivacyScreen.exe on
   │              │
   │              ├── IPC
   │              ▼
   │        Background Instance
   │              │
   │              └── create overlay
   │
   └── start streaming application
```

结束：

```text
Sunshine
   │
   └── undo
          │
          └── PrivacyScreen.exe off
                    │
                    └── remove overlay
```

Sunshine 官方也说明，Windows service 安装模式下 prep commands 可以由当前用户上下文运行；本工具原则上不需要管理员权限，因此 `elevated` 保持 `false`。

---

# 18. 建议同时保留手动控制

即使配置 Sunshine 自动启动，也必须保留：

```text
Tray Menu:
    Enable Privacy
    Disable Privacy
    Toggle
    Status
    Exit
```

原因：

如果 Sunshine 崩溃，没有正常执行 `undo`：

```text
Overlay 可能保持开启
```

此时用户必须能通过：

```text
快捷键
Tray
CLI
```

立即关闭。

---

# 19. 状态机

建议内部状态：

```text
OFF
ENABLING
ON
DISABLING
ERROR
```

### OFF → ENABLING

执行：

```text
Enumerate monitors
Create overlay(s)
Set capture affinity
Verify capture affinity
Show topmost
```

全部成功：

```text
ON
```

任何失败：

```text
销毁已经创建的 overlay
→ ERROR
```

避免：

```text
一块屏幕成功
一块屏幕失败
```

导致用户产生错误安全感。

### ON → DISABLING

```text
HideWindow
DestroyWindow
clear overlay list
```

然后：

```text
OFF
```

---

# 20. 推荐关键代码结构

```text
src/
├── main.cpp
├── app.h
├── app.cpp
│
├── ipc/
│   ├── pipe_server.h
│   └── pipe_server.cpp
│
├── monitor/
│   ├── monitor_manager.h
│   └── monitor_manager.cpp
│
├── overlay/
│   ├── overlay_window.h
│   ├── overlay_window.cpp
│   ├── overlay_manager.h
│   └── overlay_manager.cpp
│
├── privacy/
│   ├── capture_exclusion.h
│   └── capture_exclusion.cpp
│
└── util/
    ├── logger.h
    └── logger.cpp
```

不需要过度设计。

---

# 21. Overlay 创建伪代码

```cpp
HWND CreatePrivacyOverlay(const RECT& monitorRect)
{
    DWORD exStyle =
        WS_EX_TOPMOST |
        WS_EX_TOOLWINDOW |
        WS_EX_NOACTIVATE |
        WS_EX_LAYERED |
        WS_EX_TRANSPARENT;

    HWND hwnd = CreateWindowExW(
        exStyle,
        L"PrivacyOverlayClass",
        L"",
        WS_POPUP,
        monitorRect.left,
        monitorRect.top,
        monitorRect.right - monitorRect.left,
        monitorRect.bottom - monitorRect.top,
        nullptr,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr
    );

    if (!hwnd)
        throw Win32Error("CreateWindowEx");

    SetLayeredWindowAttributes(
        hwnd,
        0,
        255,
        LWA_ALPHA
    );

    if (!SetWindowDisplayAffinity(
            hwnd,
            WDA_EXCLUDEFROMCAPTURE))
    {
        DWORD error = GetLastError();
        DestroyWindow(hwnd);
        throw Win32Error(
            "SetWindowDisplayAffinity",
            error
        );
    }

    DWORD affinity = WDA_NONE;

    if (!GetWindowDisplayAffinity(
            hwnd,
            &affinity))
    {
        DestroyWindow(hwnd);
        throw Win32Error(
            "GetWindowDisplayAffinity"
        );
    }

    if (affinity != WDA_EXCLUDEFROMCAPTURE)
    {
        DestroyWindow(hwnd);
        throw std::runtime_error(
            "Capture exclusion verification failed"
        );
    }

    SetWindowPos(
        hwnd,
        HWND_TOPMOST,
        monitorRect.left,
        monitorRect.top,
        monitorRect.right - monitorRect.left,
        monitorRect.bottom - monitorRect.top,
        SWP_NOACTIVATE |
        SWP_SHOWWINDOW
    );

    return hwnd;
}
```

---

# 22. WindowProc

最低需要：

```cpp
LRESULT CALLBACK OverlayWndProc(
    HWND hwnd,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam)
{
    switch (msg)
    {
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;

        case WM_ERASEBKGND:
        {
            RECT rc;
            GetClientRect(hwnd, &rc);

            FillRect(
                reinterpret_cast<HDC>(wParam),
                &rc,
                static_cast<HBRUSH>(
                    GetStockObject(BLACK_BRUSH)
                )
            );

            return 1;
        }

        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            RECT rc;
            GetClientRect(hwnd, &rc);

            FillRect(
                hdc,
                &rc,
                static_cast<HBRUSH>(
                    GetStockObject(BLACK_BRUSH)
                )
            );

            EndPaint(hwnd, &ps);
            return 0;
        }
    }

    return DefWindowProcW(
        hwnd,
        msg,
        wParam,
        lParam
    );
}
```

---

# 23. Capture exclusion fallback

提供一个函数：

```cpp
bool EnableCaptureExclusion(HWND hwnd);
```

逻辑：

```text
SetWindowDisplayAffinity(
    WDA_EXCLUDEFROMCAPTURE
)

↓ success

GetWindowDisplayAffinity()

↓ verify

return true
```

只有打开：

```text
--dda-fallback
```

以后才进一步尝试：

```text
WCA_EXCLUDED_FROM_DDA
```

实现该 fallback 时应动态查找相关 API，不要把整个应用的运行依赖建立在未提供常规 SDK header 声明的 composition API 上。

---

# 24. 日志

输出：

```text
%LOCALAPPDATA%\SunshinePrivacyScreen\privacy-screen.log
```

示例：

```text
[INFO] Windows build: ...
[INFO] DPI awareness: PER_MONITOR_AWARE_V2
[INFO] Monitor count: 2

[INFO] Monitor:
       \\.\DISPLAY1
       rect=0,0,3840,2160

[INFO] Creating overlay for DISPLAY1
[INFO] HWND=0x000A0412

[INFO] SetWindowDisplayAffinity:
       WDA_EXCLUDEFROMCAPTURE SUCCESS

[INFO] GetWindowDisplayAffinity:
       0x00000011

[INFO] Privacy mode ACTIVE
```

禁止把日志写进 overlay 本身。

---

# 25. Startup 检查

启动时检查 Windows 版本。

最低正式目标：

```text
Windows 10 2004+
Windows 11
```

因为 Microsoft 明确指出：

```text
WDA_EXCLUDEFROMCAPTURE
```

从 Windows 10 Version 2004 正式支持；更早系统会表现为兼容模式而不是完整的 exclude-from-capture 行为。

如果系统低于最低版本：

```text
拒绝启用
```

不要静默 fallback 到：

```text
WDA_MONITOR
```

因为 `WDA_MONITOR` 的捕获语义并不是本项目需要的“让底层桌面继续被串流”。

---

# 26. MVP 测试顺序

开发 Agent 必须按以下顺序测试，而不是一上来只测 Sunshine。

## Test 1：普通桌面

开启：

```text
PrivacyScreen.exe on
```

实体显示器：

```text
必须纯黑
```

关闭：

```text
PrivacyScreen.exe off
```

实体屏：

```text
立即恢复
```

---

## Test 2：鼠标

开启 Privacy。

移动本机物理鼠标。

确保：

```text
Overlay 不获取 focus
```

用鼠标点击底层应用位置。

关闭 Privacy 后验证：

```text
底层应用确实接收过输入
```

---

## Test 3：Windows 截图

打开 Privacy。

执行：

```text
Win + Shift + S
```

预期：

```text
实体屏 = black
截图结果 = 底层桌面
```

如果截图直接得到黑色：

```text
FAIL
```

---

## Test 4：Sunshine + Moonlight

Sunshine：

```ini
capture = ddx
```

然后启动 Moonlight。

开启 Privacy。

预期：

```text
Host physical monitor:
BLACK

Moonlight:
NORMAL DESKTOP

Moonlight FPS:
NORMAL

Moonlight input:
NORMAL
```

这是核心验收。

---

## Test 5：游戏窗口 / Borderless

运行游戏：

```text
Borderless Windowed
```

开启 Privacy。

预期：

```text
实体屏 BLACK
Moonlight 正常显示游戏
```

这是推荐游戏运行模式。

---

## Test 6：Exclusive Fullscreen

测试：

```text
Exclusive Fullscreen
```

如果出现：

```text
游戏覆盖 Privacy Overlay
```

则记录为平台限制。

不要为了支持 exclusive fullscreen 注入游戏进程、hook Present、安装驱动。

推荐用户改用：

```text
Borderless Fullscreen
```

---

## Test 7：多屏

至少：

```text
DISPLAY1 3840x2160 @150%
DISPLAY2 2560x1440 @100%
```

开启：

```text
--all
```

所有实体屏：

```text
完全黑
```

Sunshine 捕获任意其中一块：

```text
正常
```

检查屏幕四边不能有：

```text
1px
taskbar
wallpaper
window border
```

泄漏。

---

## Test 8：显示器热插拔

Privacy 开启过程中：

```text
插入/拔掉显示器
```

程序处理：

```text
WM_DISPLAYCHANGE
```

然后自动 rebuild overlays。

---

## Test 9：DPI

分别测试：

```text
100%
125%
150%
175%
200%
```

要求：

```text
完整覆盖屏幕
```

---

## Test 10：异常退出

Privacy 正常开启时强杀：

```text
PrivacyScreen.exe
```

由于 Overlay 属于进程：

```text
进程退出
→ Windows 自动销毁窗口
→ 屏幕恢复
```

这是理想 fail-open 行为。

不要创建任何独立驱动/系统层状态，使崩溃后屏幕仍保持黑色。

---

# 27. Sunshine 自动化验收

配置：

```text
global_prep_cmd
```

启动 Moonlight session：

```text
do → PrivacyScreen.exe on
```

实体显示器必须自动变黑。

关闭 session：

```text
undo → PrivacyScreen.exe off
```

实体显示器必须恢复。

Sunshine 当前官方文档明确支持 global prep command，以及带 `do` / `undo` 的应用 prep command。

---

# 28. 性能要求

程序不应该持续捕获屏幕。

因此 Privacy 开启状态下：

```text
CPU ≈ 0%
GPU ≈ 0%
网络 = 0
磁盘 I/O ≈ 0
```

除：

```text
Windows message loop
IPC
偶发 monitor refresh
```

外，不做定时绘制。

不需要 60 FPS render loop。

纯黑窗口没有动画，因此 Windows/DWM 自己完成 composition 即可。

---

# 29. 安全边界

这个方案属于：

```text
Local Privacy Feature
```

而不是：

```text
DRM
Security Boundary
```

Microsoft 也明确说明 `SetWindowDisplayAffinity` 不是 DRM 或绝对安全机制，不能保证抵御所有捕获方式，例如直接用摄像头拍摄显示器。

因此 README 必须明确写：

> This application is designed to hide the local desktop during Sunshine streaming. It is not a DRM or security boundary.

也不要承诺可以阻止：

```text
kernel driver capture
GPU vendor-specific capture
malicious privileged software
external camera
hardware capture
```

---

# 30. Secure Desktop 限制

Windows：

```text
UAC Secure Desktop
Ctrl+Alt+Del
Windows Lock Screen
```

不是普通用户桌面的 DWM window stack。

普通 Overlay 不应该被认为能够可靠遮住这些界面。

因此不要宣称：

```text
“实体显示器在任何情况下永远不会出现内容”
```

实际保证范围是：

```text
正常 interactive desktop session
```

这是 Windows 本身的桌面安全模型导致的限制。

---

# 31. 不要做的事情

本项目不要：

```text
关闭显示器
SendMessage WM_SYSCOMMAND SC_MONITORPOWER
DisplayConfig 禁用输出
创建虚拟显示器
安装 IDD
改变显示拓扑
注入 Sunshine
修改 Sunshine 源码
注入游戏
hook DirectX Present
hook NVENC
抓屏后再重新编码
使用 OBS
安装 kernel driver
使用 Desktop Duplication 自己抓桌面
```

全部不必要。

核心只有：

```text
Opaque Black Window
+
TopMost
+
NoActivate
+
Mouse Passthrough
+
WDA_EXCLUDEFROMCAPTURE
```

---

# 32. 完成标准 / Definition of Done

只有全部满足以下条件才能认为项目完成：

- Windows 11 正常运行；
- Windows 10 2004+ 正常运行；
- 单文件或少量依赖部署；
- 不要求管理员权限；
- `PrivacyScreen.exe on` 能遮黑所有目标实体屏幕；
- `PrivacyScreen.exe off` 能立即恢复；
- 遮罩不进入 Alt+Tab；
- 遮罩不出现在任务栏；
- 遮罩不抢 foreground focus；
- Moonlight 鼠标操作正常；
- Moonlight 键盘操作正常；
- Sunshine `capture=ddx` 时串流不包含黑色 Overlay；
- 实体显示器确实显示纯黑；
- 支持多显示器；
- 支持负 monitor coordinates；
- 支持 HiDPI；
- 显示配置变化后自动修复 overlay；
- Capture Exclusion API 失败时明确报错；
- 程序崩溃后 Overlay 自动消失；
- 支持 `on/off/toggle/status`；
- 支持全局紧急关闭快捷键；
- 可以接入 Sunshine `global_prep_cmd`；
- README 包含 Sunshine 配置范例；
- README 明确说明 Secure Desktop 和 DRM 限制。

---

# 33. 推荐开发阶段

## Phase 1 — Spike

只写：

```text
一个 monitor
一个黑窗
WDA_EXCLUDEFROMCAPTURE
```

验证：

```text
实体屏黑
+
Sunshine ddx 不黑
```

**在这个实验通过以前，不开发 tray、IPC、Sunshine integration。**

这是整个项目最大的技术假设。

---

## Phase 2 — MVP

增加：

```text
multi-monitor
DPI
click-through
no-activate
on/off
hotkey
```

---

## Phase 3 — Production

增加：

```text
single-instance
named-pipe IPC
tray
logging
Sunshine prep-cmd
display hotplug
error handling
```

---

## Phase 4 — Optional

测试：

```text
Sunshine capture=wgc
```

以及实验：

```text
WCA_EXCLUDED_FROM_DDA
```

作为兼容增强。

---

# 34. 给 Coding Agent 的最终任务

请实现一个名为：

```text
SunshinePrivacyScreen
```

的 C++20 Win32 程序。

最重要的技术验证是：

```text
创建一个覆盖实体显示器的完全不透明黑色 TopMost 窗口，
通过 SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE)
让该窗口在本地 monitor 上可见，但不进入 Sunshine 的
DXGI Desktop Duplication 捕获结果。
```

首先只完成技术 Spike。

**不要在确认这一点实际工作以前实现复杂架构。**

Spike 验收必须同时满足：

```text
Host Monitor = BLACK
Moonlight Stream = ORIGINAL DESKTOP
```

如果这两个条件成立，再按照本文继续完成：

```text
multi-monitor
input passthrough
IPC
hotkey
tray
Sunshine automation
```

如果 Spike 失败：

1. 输出 Windows build；
2. 输出 GPU 与驱动版本；
3. 确认 Sunshine：

```ini
capture = ddx
```

4. 验证：

```cpp
SetWindowDisplayAffinity
GetWindowDisplayAffinity
```

5. 再实验：

```text
WCA_EXCLUDED_FROM_DDA
```

不要改成虚拟显示器方案，也不要关闭实体显示器，除非项目需求以后明确改变。