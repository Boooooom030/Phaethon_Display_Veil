#include "pipe_server.h"
#include "../config.h"
#include "../util/logger.h"
#include "../util/text.h"
#include <atomic>
#include <cstring>
#include <thread>

namespace ipc {

namespace {
std::atomic<bool> g_running{ false };
std::thread       g_thread;
HWND              g_hwndTarget = nullptr;

void HandleOneClient(HANDLE pipe)
{
    // 读一行 UTF-8 命令
    char buf[512]{};
    DWORD read = 0;
    std::string acc;
    while (acc.find('\n') == std::string::npos && acc.size() < sizeof(buf) - 1)
    {
        if (!ReadFile(pipe, buf, sizeof(buf) - 1, &read, nullptr) || read == 0)
            break;
        acc.append(buf, read);
    }
    // 去掉 \r\n
    while (!acc.empty() && (acc.back() == '\n' || acc.back() == '\r'))
        acc.pop_back();

    const std::wstring line = util::Utf8ToWide(acc);
    const std::wstring trimmed = util::Trim(line);

    Request req;
    // 协议：<CMD> [args...]；ON: "ON <spec> [IMG <dir>]"；IMG: "IMG <dir>"
    const size_t sp = trimmed.find(L' ');
    const std::wstring cmd = util::ToUpper(sp == std::wstring::npos
                                               ? trimmed
                                               : trimmed.substr(0, sp));
    const std::wstring rest = sp == std::wstring::npos
                                  ? L""
                                  : util::Trim(trimmed.substr(sp + 1));

    if (cmd == L"ON")
    {
        req.kind = Request::Kind::On;
        // 解析 "ON <spec> [IMG <dir>]"
        const size_t imgPos = util::ToUpper(rest).find(L"IMG ");
        if (imgPos != std::wstring::npos)
        {
            req.monitorSpec = util::Trim(rest.substr(0, imgPos));
            req.imageDir    = util::Trim(rest.substr(imgPos + 4));
            if (req.monitorSpec.empty())
                req.monitorSpec = L"all";
        }
        else
        {
            req.monitorSpec = rest.empty() ? L"all" : rest;
        }
    }
    else if (cmd == L"OFF")    req.kind = Request::Kind::Off;
    else if (cmd == L"TOGGLE") req.kind = Request::Kind::Toggle;
    else if (cmd == L"STATUS") req.kind = Request::Kind::Status;
    else if (cmd == L"EXIT")   req.kind = Request::Kind::Exit;
    else if (cmd == L"IMG")    { req.kind = Request::Kind::Images; req.imageDir = rest; }
    else
    {
        const std::string err = "ERR unknown command\n";
        DWORD written = 0;
        WriteFile(pipe, err.data(), static_cast<DWORD>(err.size()), &written, nullptr);
        FlushFileBuffers(pipe);
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
        return;
    }

    // 投递到 UI 线程同步处理（PostMessage 异步 + Event 等待，保证 ACK 语义）
    auto* sync = new Sync{};
    sync->done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    auto* payload = new PendingRequest{ sync, req };

    PostMessageW(g_hwndTarget, cfg::kMsgIpcRequest,
                 reinterpret_cast<WPARAM>(payload), 0);
    const DWORD wait = WaitForSingleObject(sync->done, 4000);

    std::string out;
    if (wait != WAIT_OBJECT_0)
    {
        // UI 线程未响应：绝不能伪装成功
        out = "ERR ui thread did not respond (timeout)\n";
    }
    else if (!sync->resp.ok)
    {
        out = "ERR " + util::WideToUtf8(sync->resp.text) + "\n";
    }
    else
    {
        out = "OK\n";
        if (!sync->resp.text.empty())
            out += util::WideToUtf8(sync->resp.text) + "\n";
    }

    DWORD written = 0;
    WriteFile(pipe, out.data(), static_cast<DWORD>(out.size()), &written, nullptr);
    FlushFileBuffers(pipe);
    DisconnectNamedPipe(pipe);
    CloseHandle(pipe);
    CloseHandle(sync->done);
    delete sync;
    delete payload;
}

void ServerLoop(void (*onFatal)(const std::wstring&))
{
    for (;;)
    {
        HANDLE pipe = CreateNamedPipeW(
            cfg::kPipeName,
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1, // 单实例：串行处理即可，避免并发状态问题
            512, 512, 2000, nullptr);

        if (pipe == INVALID_HANDLE_VALUE)
        {
            const DWORD e = GetLastError();
            if (e == ERROR_PIPE_BUSY) { Sleep(50); continue; }
            if (onFatal) onFatal(L"CreateNamedPipe failed GLE=" + std::to_wstring(e));
            return;
        }

        const BOOL connected = ConnectNamedPipe(pipe, nullptr)
                                   ? TRUE
                                   : (GetLastError() == ERROR_PIPE_CONNECTED ? TRUE : FALSE);
        if (!connected)
        {
            // StopServer 触发的正常退出路径
            CloseHandle(pipe);
            if (!g_running) return;
            continue;
        }

        HandleOneClient(pipe);

        if (!g_running) return;
    }
}

} // namespace

bool StartServer(void* hwndMessage, void (*onFatal)(const std::wstring&))
{
    g_hwndTarget = static_cast<HWND>(hwndMessage);
    g_running    = true;
    g_thread     = std::thread(ServerLoop, onFatal);
    return true;
}

void StopServer()
{
    if (!g_running) return;
    g_running = false;
    // 唤醒阻塞在 ConnectNamedPipe 的线程：创建一次自连接
    HANDLE h = CreateFileW(cfg::kPipeName, GENERIC_READ | GENERIC_WRITE,
                           0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h != INVALID_HANDLE_VALUE)
    {
        // 发送一个无效命令让循环退出
        const char junk[] = "QUIT\n";
        DWORD written = 0;
        WriteFile(h, junk, sizeof(junk) - 1, &written, nullptr);
        // 不等待响应，server 会因 unknown command 返回 ERR 后回到循环发现 g_running=false
        CloseHandle(h);
    }
    if (g_thread.joinable())
        g_thread.join();
}

bool SendCommand(const Request& req, int timeoutMs, bool& ok, std::wstring& respText)
{
    ok = false;
    respText.clear();

    // 等待管道可用
    if (!WaitNamedPipeW(cfg::kPipeName, timeoutMs))
    {
        const DWORD e = GetLastError();
        respText = L"pipe not available (GLE=" + std::to_wstring(e) + L")";
        return false;
    }

    HANDLE h = CreateFileW(cfg::kPipeName, GENERIC_READ | GENERIC_WRITE,
                           0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE)
    {
        const DWORD e = GetLastError();
        respText = L"connect pipe failed (GLE=" + std::to_wstring(e) + L")";
        return false;
    }

    const char* cmd = "STATUS";
    char cmdBuf[1024];
    switch (req.kind)
    {
    case Request::Kind::On:
    {
        // ON 携带 monitor spec + 可选图片目录："ON <spec> [IMG <dir>]"
        std::string line = "ON ";
        line += util::WideToUtf8(req.monitorSpec);
        if (!req.imageDir.empty())
        {
            line += " IMG ";
            line += util::WideToUtf8(req.imageDir);
        }
        if (line.size() >= sizeof(cmdBuf))
            line.resize(sizeof(cmdBuf) - 1);
        strcpy_s(cmdBuf, line.c_str());
        cmd = cmdBuf;
        break;
    }
    case Request::Kind::Images:
    {
        std::string line = "IMG ";
        line += util::WideToUtf8(req.imageDir);
        if (line.size() >= sizeof(cmdBuf))
            line.resize(sizeof(cmdBuf) - 1);
        strcpy_s(cmdBuf, line.c_str());
        cmd = cmdBuf;
        break;
    }
    case Request::Kind::Off:    cmd = "OFF";    break;
    case Request::Kind::Toggle: cmd = "TOGGLE"; break;
    case Request::Kind::Status: cmd = "STATUS"; break;
    case Request::Kind::Exit:   cmd = "EXIT";   break;
    }

    std::string out = cmd;
    out += "\n";
    DWORD written = 0;
    if (!WriteFile(h, out.data(), static_cast<DWORD>(out.size()), &written, nullptr))
    {
        respText = L"write pipe failed GLE=" + std::to_wstring(GetLastError());
        CloseHandle(h);
        return false;
    }

    // 读全部响应（server 会 FlushFileBuffers 后断开，读到 EOF）
    std::string acc;
    char buf[1024]{};
    for (;;)
    {
        DWORD read = 0;
        if (!ReadFile(h, buf, sizeof(buf), &read, nullptr) || read == 0)
            break;
        acc.append(buf, read);
    }
    CloseHandle(h);

    // 解析
    size_t nl = acc.find('\n');
    const std::string first = nl == std::string::npos ? acc : acc.substr(0, nl);
    const std::string rest  = nl == std::string::npos ? "" : acc.substr(nl + 1);

    std::string status = first;
    while (!status.empty() && (status.back() == '\r' || status.back() == ' '))
        status.pop_back();

    if (status == "OK")
    {
        ok = true;
        std::string body = rest;
        while (!body.empty() && (body.back() == '\n' || body.back() == '\r'))
            body.pop_back();
        if (!body.empty())
            respText = util::Utf8ToWide(body);
        return true;
    }
    if (status == "ERR")
    {
        std::string body = rest;
        while (!body.empty() && (body.back() == '\n' || body.back() == '\r'))
            body.pop_back();
        respText = body.empty() ? L"unknown error" : util::Utf8ToWide(body);
        return true; // 通信本身成功
    }
    if (status.rfind("ERR ", 0) == 0)
    {
        respText = util::Utf8ToWide(status.substr(4));
        return true;
    }
    respText = L"malformed pipe response";
    return false;
}

} // namespace ipc
