// main.cpp - entry point: single-instance check, CLI dispatch
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
        // Pipe not reachable: background instance not running
        util::ConsoleOut(L"[ERROR] background instance not running (" + resp + L")");
        return static_cast<int>(cfg::ExitCode::NoServer);
    }

    if (!ok)
    {
        util::ConsoleOut(L"[ERROR] " + resp);
        // monitor not found -> 4; capture exclusion -> 3; otherwise -> 1
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

// Start the background server as a detached process when not running.
// Idempotent: returns true if the server is already up.
bool EnsureServerRunning(const cli::Parsed& p)
{
    HANDLE mutex = CreateMutexW(nullptr, TRUE, cfg::kMutexName);
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS)
    {
        CloseHandle(mutex);
        return true; // already running
    }
    if (mutex) CloseHandle(mutex);

    // Re-launch self, detached from this process
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

    // Wait for the pipe (server startup is usually < 200 ms, cap at 5 s)
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
    const cli::Parsed p = cli::Parse(argc > 1 ? const_cast<const wchar_t* const*>(argv + 1)
                                              : nullptr, argc > 1 ? argc - 1 : 0);
    if (p.hasError)
    {
        util::ConsoleOut(p.error);
        return static_cast<int>(cfg::ExitCode::GeneralError);
    }

    // Single instance: an existing server makes this process a client
    HANDLE mutex = CreateMutexW(nullptr, TRUE, cfg::kMutexName);
    const bool serverOwner = mutex && GetLastError() != ERROR_ALREADY_EXISTS;

    if (!p.hasCommand)
    {
        // No command: start the background server
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
        // Hold the mutex for the lifetime of the server
        const int rc = app::RunServer(p.ddaFallback, p.debug);
        CloseHandle(mutex);
        return rc;
    }

    // Command: run as a client; start the server first if needed
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
