#pragma once
// app.h — 后台常驻实例装配：消息循环、热键、托盘、IPC、定时保险
#include <windows.h>

namespace app {

// 运行后台实例；返回进程退出码（cfg::ExitCode）
int RunServer(bool ddaFallback, bool debug);

} // namespace app
