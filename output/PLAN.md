# PLAN.md — 实施计划与进度

> 状态标记：`[ ]` 未开始 · `[~]` 进行中 · `[x]` 完成 · `[-]` 放弃/变更

## Phase 0 — 环境确认

- [x] 确认 MSVC 工具链：VS 18 Community，MSVC 14.51，SDK 10.0.26100
- [x] 确认构建方式：build.bat + vcvarsall.bat x64 + cl（无 CMake）

## Phase 1 — Spike（最大技术假设验证）✅

- [x] 1.1 单显示器纯黑 TopMost 窗口（WS_POPUP + 扩展样式五件套）
- [x] 1.2 SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE) + GetWindowDisplayAffinity 回读校验（回读 0x11 一致）
- [x] 1.3 GDI 纯黑渲染（WM_ERASEBKGND / WM_PAINT）
- [x] 1.4 build.bat 编译通过（/W4 /WX 零警告）
- [~] 1.5 肉眼确认物理屏黑 5 秒 —— spike.exe 已运行退出码 0，需用户目视确认（API 层已全部验证）

## Phase 2 — MVP ✅

- [x] 2.1 多显示器枚举（实测双屏 DISPLAY1 rect=(1920,-696,3000,1224) 负坐标 + DISPLAY2 primary）
- [x] 2.2 DPI：PER_MONITOR_AWARE_V2 + WM_DPICHANGED 重建
- [x] 2.3 鼠标穿透 + 不抢焦点（WS_EX_LAYERED|TRANSPARENT|NOACTIVATE + WM_MOUSEACTIVATE）
- [x] 2.4 on/off/toggle CLI
- [x] 2.5 全局热键 Ctrl+Alt+Shift+B / Ctrl+Alt+Shift+F10（紧急关闭，注册于隐藏消息窗口）
- [x] 2.6 WM_DISPLAYCHANGE → 重建 overlay；另有 5s 周期 Reassert（TOPMOST 重申 + 死亡检测）
- [x] 2.7 状态机 OFF/ENABLING/ON/DISABLING/ERROR + 失败回滚（任一屏失败销毁全部）
- [x] 2.8 Win10 2004 版本检查（RtlGetVersion，< 19041 → exit 5）

## Phase 3 — Production ✅

- [x] 3.1 单实例（CreateMutexW Local\SunshinePrivacyScreen.Singleton）
- [x] 3.2 Named Pipe IPC（\\.\pipe\SunshinePrivacyScreen，"ON [spec]" / OFF / TOGGLE / STATUS / EXIT）
- [x] 3.3 控制客户端（发送→ACK→返回；UI 线程 4s 未响应返回 ERR，绝不假成功）
- [x] 3.4 日志（%LOCALAPPDATA%\SunshinePrivacyScreen\privacy-screen.log，Win32 句柄直写 UTF-8）
- [x] 3.5 托盘（Enable/Disable/Status/Exit + 双击菜单）
- [x] 3.6 --dda-fallback（WCA_EXCLUDED_FROM_DDA=24，GetProcAddress 动态加载）
- [x] 3.7 退出码 0/1/2/3/4/5（实测 exit 4 = monitor 未找到）

## Phase 4 — 可选

- [x] 4.1 --debug 控制台输出
- [ ] 4.2 Sunshine capture=wgc 兼容测试（需用户实测）
- [x] 4.3 README：global_prep_cmd 配置范例、Secure Desktop / DRM 限制声明

## Phase 5 — 图片遮罩模式 ✅（2026-09-08 增补）

- [x] 5.1 src/images/image_store.*：文件夹枚举（jpg/jpeg/png/bmp/gif，不递归）+ GDI+ 懒加载
- [x] 5.2 overlay 绘制：cover 方式（等比填满居中裁剪），加载失败兜底纯黑
- [x] 5.3 CLI：`on --images <dir>`、`toggle --images <dir>`、`images [<dir>]`（无参=回纯黑）
- [x] 5.4 IPC：协议扩展 `ON <spec> [IMG <dir>]`、`IMG <dir>`；运行中热切换立即重绘
- [x] 5.5 幻灯片：30s 定时轮播（>1 张才轮播），WM_TIMER 驱动，零常驻 GPU 负载
- [x] 5.6 GDI+ 生命周期：RunServer 启动时 GdiplusStartup，退出时 Shutdown
- [x] 5.7 实测：on --images → ACTIVE；images 热切换 → Image folder set (2 file(s))；错误目录 → exit 1
- 注：图片是 overlay 窗口内容，同样被 capture exclusion 排除，串流画面不含图片


## 已验证（本机实测，2026-09-08）

| 验证项 | 结果 |
|---|---|
| spike 编译运行 | EXIT=0，affinity 设置+回读 0x11 一致 |
| 完整版编译 | /W4 /WX 零警告，/MT 静态链接 |
| server 启动 + 双屏 overlay | 日志 ACTIVE (2 overlay(s))，含负坐标 DISPLAY1 |
| on --monitor DISPLAY2 | 单屏遮罩 ACTIVE (1 overlay(s)) |
| on --monitor 99 | exit 4 + 日志 "monitor not found: 99" |
| off / toggle | 日志 DISABLING → OFF；toggle 再开 → ACTIVE |
| status | 经管道返回状态正文（exit 0） |
| exit | EMERGENCY DISABLE → Server stopped cleanly，进程数归 0 |
| 强杀进程 | overlay 随进程销毁（fail-open，Windows 行为保证） |

## 开发中踩过的坑（重要经验）

1. **bat 文件含中文注释**在 GBK 代码页下解析错乱 → build.bat 必须纯 ASCII。
2. **`_wfopen_s("a, ccs=UTF-8")` + /MT 静态 CRT**：写 UTF-8 字节被二次转换（乱码）且第二次写入触发 0xC0000409 fail-fast（RVA 命中 `__acrt_get_begin_thread_init_policy`）→ 日志改用 `CreateFileW + WriteFile` 直写 UTF-8 字节。
3. **PostMessage 的载荷指针**：pipe server 放在 WPARAM，WndProc 却从 LPARAM 读 → 所有 IPC 请求静默丢失且返回假 OK。修复 + 增加"UI 线程未响应 → ERR"防伪装逻辑。
4. **重复 ON 泄漏 overlay**：Enable 前必须先 DestroyAllLocked。
5. GUI 子系统程序从 bash 管道运行无 stdout；验证靠退出码 + 日志文件；Start-Process 对 GUI 子系统进程返回不可靠的句柄错误。

## 已知风险

| 风险 | 处置 |
|---|---|
| Exclusive fullscreen 游戏可能盖住 overlay | 记录为平台限制，建议 Borderless（不做注入/hook） |
| UAC / 锁屏 Secure Desktop 无法遮盖 | 明示为 Windows 安全模型限制，不承诺 |
| WDA_EXCLUDEFROMCAPTURE 个别 GPU/驱动下异常 | fail-safe：报错 + exit 3，绝不假装成功 |
| 物理屏黑 + 串流不黑的最终目视确认 | 需用户按 README 测试清单 1–9 实测（尤其 Test 4 Sunshine ddx） |
