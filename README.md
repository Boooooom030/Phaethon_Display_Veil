# SunshinePrivacyScreen

串流隐私遮罩：当 Sunshine / Moonlight 正在串流时，让电脑的**实体显示器显示纯黑**，而 Sunshine 捕获到的串流画面**不含黑窗**，Moonlight 继续正常显示真实桌面并正常接收输入。

原理：创建覆盖每块实体显示器的纯黑 TopMost 窗口，并通过
`SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE)`（Windows 10 2004+）把该窗口排除出屏幕捕获。窗口鼠标穿透、不抢焦点、不出现在任务栏/Alt+Tab。

> 本工具用于在本地串流时隐藏桌面内容。它**不是 DRM，也不是安全边界** —— 不能抵御内核级捕获、恶意特权软件或直接拍摄显示器。

## 构建要求

- Visual Studio 2017+（含 MSVC C++ 工具集，实测 VS 18 / MSVC 14.51）
- Windows SDK 10
- 无第三方依赖，静态链接 CRT（/MT），产物为单个 x64 EXE

```bat
build.bat          # 构建 build\PrivacyScreen.exe（/W4 /WX 零警告）
build.bat spike    # 仅构建核心假设验证程序 build\spike.exe
```

## 使用

```text
PrivacyScreen.exe                      启动后台常驻实例（含托盘图标）
PrivacyScreen.exe on                   开启遮罩（默认全部显示器）
PrivacyScreen.exe on --all             全部显示器
PrivacyScreen.exe on --primary         仅主屏
PrivacyScreen.exe on --monitor 2       按枚举序号
PrivacyScreen.exe on --monitor "\\.\DISPLAY2"
PrivacyScreen.exe off                  关闭全部遮罩
PrivacyScreen.exe toggle               切换
PrivacyScreen.exe status               查看状态
PrivacyScreen.exe exit                 退出后台实例
PrivacyScreen.exe --dda-fallback       附加 WCA_EXCLUDED_FROM_DDA（针对 Sunshine ddx 的额外保险）
PrivacyScreen.exe --debug              日志附加输出到父控制台
```

## 托盘菜单

右键托盘图标：

- **遮罩全部屏幕** —— 开启（当前为图片模式则继续用图片）
- **仅遮罩此屏幕…** —— 子菜单动态列出当前所有显示器（标注主屏），点击单独启用某一块
- **选择图片文件夹…** —— 弹出系统文件夹选择对话框，选中后立即切换为该文件夹的幻灯片（ON 状态实时生效）；已有图片库时菜单项显示"更换图片文件夹…"
- **恢复纯黑** —— 清除图片库回到黑屏模式（勾选状态显示当前是否纯黑）
- **关闭遮罩** / **状态** / **退出**

## 全局热键

- `Ctrl+Alt+Shift+B` —— 开/关遮罩
- `Ctrl+Alt+Shift+F10` —— **紧急无条件关闭**（即使状态异常也强制销毁全部遮罩窗口）

退出码：`0` 成功 · `1` 一般错误 · `2` 后台实例未运行 · `3` capture exclusion 失败 · `4` monitor 未找到 · `5` 系统过旧（需 Win10 2004+）

日志：`%LOCALAPPDATA%\SunshinePrivacyScreen\privacy-screen.log`

## 接入 Sunshine 自动联动

在 Sunshine 的 `global_prep_cmd` 中加入（或按应用配置 prep-cmd）：

```json
{
  "do": "\"C:\\Tools\\PrivacyScreen\\PrivacyScreen.exe\" on",
  "undo": "\"C:\\Tools\\PrivacyScreen\\PrivacyScreen.exe\" off",
  "elevated": false
}
```

- `do` 在串流应用启动前执行，`undo` 在结束后执行；
- 命令通过 Named Pipe（`\\.\pipe\SunshinePrivacyScreen`）发送给后台实例，收到 ACK 后立即返回，不会阻塞 Sunshine 启动；
- 无需管理员权限；
- Sunshine 侧建议显式设置 `capture = ddx`（WGC 为 beta 且与 service 模式不兼容；`--dda-fallback` 可作为 ddx 的额外排除手段）。

## 建议的游戏运行方式

使用 **Borderless Windowed（无边框窗口）** 全屏。Exclusive Fullscreen 模式下游戏可能覆盖遮罩窗口，这是平台限制，本工具不做进程注入 / Present hook / 驱动安装来绕过。

## 行为边界与限制

- 正常交互桌面（interactive desktop session）内遮罩可靠；**无法**遮盖 UAC / Ctrl+Alt+Del / 锁屏等 Secure Desktop 界面（Windows 桌面安全模型所限）。
- `SetWindowDisplayAffinity` 不是绝对安全机制（Microsoft 明确说明），不承诺抵御内核驱动捕获、GPU 厂商私有捕获路径或外置摄像拍摄。
- 进程崩溃/被强杀时，Windows 自动销毁其全部窗口 —— 遮罩 fail-open 消失，绝不会屏幕永久黑掉。
- capture exclusion 设置或校验失败时，程序**不显示黑窗**，明确报错（exit 3），避免"串流出去全是黑屏"的假安全。

## 测试清单（验收用）

1. `on` → 实体屏全黑；`off` → 立即恢复
2. 遮罩开启时本机鼠标/键盘正常作用于底层应用（不抢焦点、点击穿透）
3. `Win+Shift+S` 截图 → 得到真实桌面而非黑图
4. Sunshine `capture=ddx` + Moonlight → 实体屏黑、串流画面正常、输入正常、帧率正常
5. 多屏（含负坐标、混合 DPI）四边无泄漏
6. `--monitor` 单屏遮罩只黑对应屏
7. 显示器热插拔 / DPI 变化 → 自动重建遮罩
8. 强杀进程 → 屏幕自动恢复
9. 热键 `Ctrl+Alt+Shift+F10` 紧急关闭始终可用

## 目录结构

```text
build.bat                 一键 MSVC 构建
spike/                    Phase 1 技术验证（spike.cpp）与最小复现程序
src/
  main.cpp                入口：单实例 + CLI 分发
  app.cpp/.h              后台实例：消息循环、热键、托盘、IPC 装配
  cli.cpp/.h              命令行解析
  config.h                常量（管道名/互斥体/热键/退出码）
  ipc/pipe_server.*       Named Pipe server + 控制客户端
  monitor/monitor_manager.*   显示器枚举与筛选
  overlay/overlay_window.*    遮罩窗口（GDI 纯黑 + capture exclusion）
  overlay/overlay_manager.*   状态机 OFF/ENABLING/ON/DISABLING/ERROR
  privacy/capture_exclusion.* WDA_EXCLUDEFROMCAPTURE + WCA_EXCLUDED_FROM_DDA
  util/logger.*           日志（Win32 文件句柄直写 UTF-8）
  util/text.h             编码/字符串工具
```
