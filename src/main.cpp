// main.cpp — 入口：单实例判定、CLI 分发（后台 server / 控制客户端）
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include "config.h"
#include "app.h"
#include "cli.h"
#include "ipc/pipe_server.h"
#include "util/logger.h"
#include "util/text.h"

namespace {

int RunClient(const cli::Parsed& p)
{
    auto& log = util::Logger::Instance();
    log.Init(p.debug);

    ipc::Request req;
    if      (p.command == L"on")     req.kind = ipc::Request::Kind::On;
    else if (p.command == L"off")    req.kind = ipc::Request::Kind::Off;
    else if (p.command == L"toggle") req.kind = ipc::Request::Kind::Toggle;
    else if (p.command == L"status") req.kind = ipc::Request::Kind::Status;
    else if (p.command == L"exit")   req.kind = ipc::Request::Kind::Exit;
    else if (p.command == L"images") req.kind = ipc::Request::Kind::Images;
    req.monitorSpec = p.monitorSpec;
    req.imageDir    = p.imageDir;
    req.ddaFallback = p.ddaFallback;

    bool ok = false;
    std::wstring resp;
    if (!ipc::SendCommand(req, 3000, ok, resp))
    {
        // 管道不通：后台实例未运行
        util::ConsoleOut(L"[ERROR] background instance not running (" + resp + L")");
        return static_cast<int>(cfg::ExitCode::NoServer);
    }

    if (!ok)
    {
        util::ConsoleOut(L"[ERROR] " + resp);
        // monitor 未找到 → 4；capture exclusion → 3；其余 → 1
        if (resp.find(L"monitor not found") != std::wstring::npos)
            return static_cast<int>(cfg::ExitCode::MonitorNotFound);
        if (resp.find(L"SetWindowDisplayAffinity") != std::wstring::npos ||
            resp.find(L"Capture exclusion") != std::wstring::npos)
            return static_cast<int>(cfg::ExitCode::CaptureExclusionFailed);
        return static_cast<int>(cfg::ExitCode::GeneralError);
    }

    if (p.command == L"status")
        util::ConsoleOut(resp.empty() ? L"state=OFF (server running)" : resp);
    else if (p.command == L"images")
        util::ConsoleOut(L"[OK] image folder set: " + resp);
    else
        util::ConsoleOut(L"[OK] " + p.command);
    return static_cast<int>(cfg::ExitCode::Ok);
}

// 尝试启动后台 server（分离进程，不随本进程退出）。
// 成功返回 true；已有实例在跑返回 true（幂等）；启动失败返回 false。
bool EnsureServerRunning(const cli::Parsed& p)
{
    HANDLE mutex = CreateMutexW(nullptr, TRUE, cfg::kMutexName);
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS)
    {
        CloseHandle(mutex);
        return true; // server 已在跑
    }
    if (mutex) CloseHandle(mutex);

    // 以自身路径分离启动（CREATE_NEW_PROCESS_GROUP | DETACHED，不阻塞本进程）
    wchar_t exePath[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, exePath, MAX_PATH))
        return false;

    std::wstring cmdline = std::wstring(L"\"") + exePath + L"\"";
    if (p.ddaFallback) cmdline += L" --dda-fallback";
    if (p.debug)       cmdline += L" --debug";

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    const BOOL ok = CreateProcessW(nullptr, cmdline.data(), nullptr, nullptr, FALSE,
                                   CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP,
                                   nullptr, nullptr, &si, &pi);
    if (!ok)
        return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    // 等待管道就绪（server 初始化一般 < 200ms，上限 5s）
    for (int i = 0; i < 50; ++i)
    {
        if (WaitNamedPipeW(cfg::kPipeName, 100))
            return true;
        if (GetLastError() == ERROR_FILE_NOT_FOUND)
            Sleep(100);
        else
            Sleep(50);
    }
    return false;
}

} // namespace

int wmain(int argc, wchar_t* argv[])
{
    // 1) 解析命令行
    const cli::Parsed p = cli::Parse(argc > 1 ? const_cast<const wchar_t* const*>(argv + 1)
                                              : nullptr, argc > 1 ? argc - 1 : 0);
    if (p.hasError)
    {
        util::ConsoleOut(p.error);
        return static_cast<int>(cfg::ExitCode::GeneralError);
    }

    // 2) 单实例：已有 server 在跑 → 本进程只能作为控制客户端
    HANDLE mutex = CreateMutexW(nullptr, TRUE, cfg::kMutexName);
    const bool serverOwner = mutex && GetLastError() != ERROR_ALREADY_EXISTS;

    if (!p.hasCommand)
    {
        // 无命令：想启动后台 server
        if (!serverOwner)
        {
            util::ConsoleOut(L"[INFO] background instance already running; nothing to start.");
            if (mutex) CloseHandle(mutex);
            return static_cast<int>(cfg::ExitCode::Ok);
        }
        if (!mutex)
        {
            util::ConsoleOut(L"[ERROR] CreateMutex failed GLE=" + std::to_wstring(GetLastError()));
            return static_cast<int>(cfg::ExitCode::GeneralError);
        }
        // 持有互斥体启动 server（到进程退出为止）
        const int rc = app::RunServer(p.ddaFallback, p.debug);
        CloseHandle(mutex);
        return rc;
    }

    // 3) 有命令：作为控制客户端；server 未运行且命令需要生效时先拉起 server
    if (mutex) CloseHandle(mutex);

    const bool needsServer =
        p.command == L"on" || p.command == L"off" || p.command == L"toggle" ||
        p.command == L"images" || p.command == L"status";

    if (needsServer && !EnsureServerRunning(p))
    {
        util::ConsoleOut(L"[ERROR] failed to start background instance");
        return static_cast<int>(cfg::ExitCode::NoServer);
    }
    return RunClient(p);
}
