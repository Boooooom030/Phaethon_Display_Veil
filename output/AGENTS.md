# AGENTS.md — SunshinePrivacyScreen 开发规范

本文件面向在本仓库工作的编码 Agent。动手前必读；与需求报告（《Windows Sunshine 物理屏隐私遮罩实现报告.md》）冲突时，以报告为准。

## 1. 项目一句话

C++20 + 纯 Win32 的小型原生 EXE：串流时在实体显示器上显示**纯黑不透明置顶窗口**，同时通过 `SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE)` 保证该黑窗**不进入** Sunshine（DDX / Desktop Duplication）的捕获画面，Moonlight 端继续看到真实桌面。

## 2. 硬性约束（违反即返工）

- 不关闭、不禁用实体显示器；不创建虚拟显示器；不改显示拓扑；不改分辨率。
- 不注入任何进程（Sunshine / 游戏），不 hook DirectX/NVENC，不安装驱动，不自己抓屏。
- Overlay 窗口：`WS_POPUP` + `WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED | WS_EX_TRANSPARENT`，alpha=255 完全不透明，鼠标穿透，永不成为 foreground。
- 禁止调用 `SetForegroundWindow()`；不得出现在任务栏 / Alt+Tab。
- **Fail-safe**：`SetWindowDisplayAffinity` 失败或回读校验失败时，绝不显示黑窗假装成功 —— 必须销毁窗口、置状态 ERROR、日志与返回码明确报告（exit code 3）。
- 每块显示器**一个独立窗口**（不做跨虚拟桌面巨型窗口），位置用 `rcMonitor`（必须盖住任务栏），正确处理负坐标。
- 渲染只用 GDI 纯黑（`BLACK_BRUSH` / `FillRect`），禁止 D3D/半透明/模糊/Acrylic/DirectComposition。
- 不做定时重绘循环；除消息循环、IPC、显示变化外零 CPU/GPU 占用。
- 不需要管理员权限；进程崩溃后窗口必须随进程自动消失（fail-open），不残留任何系统级状态。

## 3. 环境与构建（MSVC）

- 工具链：Visual Studio 18 Community，MSVC v14.51（`cl` 19.5x），C++20，Windows SDK 10.0.26100。
- 构建脚本：`build.bat`（内部调用 `vcvarsall.bat x64` 后用 `cl` 编译，**不依赖 CMake/Ninja**）。
- 手动构建（bash 环境下通过 cmd 调用）：

```bash
cmd //c "build.bat"
```

- 产物：`build\PrivacyScreen.exe`（x64），单文件，静态链接 CRT（`/MT`）以便绿色部署。
- 警告等级 `/W4`；构建必须零警告（C4100 等允许用标准写法消除，不整体关闭）。
- 定义 `UNICODE`、`_UNICODE`、`WIN32_LEAN_AND_MEAN`、`NOMINMAX`、`_WIN32_WINNT=0x0A00`。
- 链接依赖仅系统库：`user32 gdi32 shell32 advapi32`。禁止引入第三方库。

### 关键文件

```text
AGENTS.md                 本文件
PLAN.md                   分阶段计划与验收状态
README.md                 用户文档（Sunshine 配置、限制）
build.bat                 一键 MSVC 构建
src/
  main.cpp                入口：单实例判定、CLI 分发（server / 控制客户端）
  app.h / app.cpp         消息循环、热键、托盘、IPC server 装配
  cli.h / cli.cpp         命令行解析 + 控制客户端（Named Pipe 连接）
  config.h                常量（类名、管道名、互斥体名、热键、日志路径）
  ipc/pipe_server.*       Named Pipe server（文本协议 ON/OFF/TOGGLE/STATUS）
  monitor/monitor_manager.*  EnumDisplayMonitors 枚举 + WM_DISPLAYCHANGE 重建
  overlay/overlay_window.*   Overlay 窗口创建、WindowProc、capture exclusion
  overlay/overlay_manager.*  状态机（OFF/ENABLING/ON/DISABLING/ERROR）与 overlay 集合管理
  util/logger.*           日志（%LOCALAPPDATA%\SunshinePrivacyScreen\privacy-screen.log）
```

## 4. 代码规范

- C++20；UTF-8（`/utf-8`）源文件；字符串字面量用宽字符 `L"..."`（Win32 W 系列 API）。
- 所有 Win32 调用**必须检查返回值**，失败时 `GetLastError()` 记日志。
- `SetWindowDisplayAffinity` 后必须 `GetWindowDisplayAffinity` 回读验证 == `WDA_EXCLUDEFROMCAPTURE`。
- 错误路径必须回滚：任一 monitor 的 overlay 创建失败 → 销毁全部已建 overlay → ERROR。
- 禁止静态全局可变状态裸奔；集中在 `App` 单例内。
- 日志只写文件，绝不绘制到 overlay 上。

## 5. 行为契约

### CLI（第二次运行 = 控制客户端，通过 Named Pipe 发给常驻 server）

| 命令 | 作用 |
|---|---|
| （无参数） | 启动后台常驻实例 |
| `on [--all\|--primary\|--monitor N\|\\.\DISPLAYn]` | 开启遮罩 |
| `off` | 关闭全部遮罩 |
| `toggle` | 切换 |
| `status` | 输出状态（OFF/ON/ERROR + monitor 明细） |
| `--dda-fallback` | 附加 `WCA_EXCLUDED_FROM_DDA`（动态 GetProcAddress） |
| `--debug` | 控制台附加输出 |
| `exit` | 退出后台实例 |

### 退出码

`0` 成功；`1` 一般错误；`2` 后台实例未运行；`3` capture exclusion 失败；`4` monitor 未找到；`5` 系统过旧（< Win10 2004）。

### 热键

- `Ctrl+Alt+Shift+B` toggle；`Ctrl+Alt+Shift+F10` 紧急无条件关闭（即使状态机认为已关闭也强制销毁全部 overlay）。

### Sunshine 集成

`global_prep_cmd`：`do` = `PrivacyScreen.exe on`，`undo` = `PrivacyScreen.exe off`，`elevated=false`。控制客户端必须“发送 IPC → 等 ACK → 立即返回”，绝不阻塞 Sunshine 启动。

## 6. 验收（Definition of Done）

按报告 Test 1–10 顺序验证，重点：

1. `on` 后实体屏纯黑，`off` 立即恢复；
2. Win+Shift+S 截图得到真实桌面而非黑图；
3. 多屏（含负坐标、混合 DPI）四边零泄漏；
4. Sunshine `capture=ddx` 串流画面不含黑窗、Moonlight 输入正常；
5. capture exclusion 失败时明确报错且不留黑屏；
6. 强杀进程后屏幕自动恢复。

每个阶段完成后更新 PLAN.md 中对应条目的状态，再进入下一阶段。
