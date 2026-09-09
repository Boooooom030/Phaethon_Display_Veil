#include "logger.h"
#include "text.h"
#include <cstdlib>
#include <cwchar>

namespace util {

namespace {
std::wstring LogFilePath()
{
    std::wstring dir;
    wchar_t* env = nullptr;
    size_t len = 0;
    if (_wdupenv_s(&env, &len, L"LOCALAPPDATA") == 0 && env && len > 0)
    {
        dir = env;
        free(env);
    }
    if (dir.empty())
    {
        wchar_t path[MAX_PATH]{};
        if (GetEnvironmentVariableW(L"USERPROFILE", path, MAX_PATH))
            dir = std::wstring(path) + L"\\AppData\\Local";
    }
    if (dir.empty()) dir = L".";
    dir += L"\\" + std::wstring(L"SunshinePrivacyScreen");
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\privacy-screen.log";
}
} // namespace

Logger& Logger::Instance()
{
    static Logger g;
    return g;
}

void Logger::Init(bool consoleDebug)
{
    if (inited_) return;
    cs_ = new CRITICAL_SECTION{};
    InitializeCriticalSection(static_cast<CRITICAL_SECTION*>(cs_));
    EnterCriticalSection(static_cast<CRITICAL_SECTION*>(cs_));

    // 直接用 Win32 文件句柄追加写（UTF-8 字节由调用侧转换好）。
    // 不用 CRT stdio 的 ccs=UTF-8：静态 CRT 下该模式存在二次转换与
    // FLS fail-fast 问题（实测），且我们的字节本来就是 UTF-8。
    const std::wstring path = LogFilePath();
    file_ = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ,
                        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file_ != INVALID_HANDLE_VALUE && file_ != nullptr)
    {
        // 空文件时写入 UTF-8 BOM，方便编辑器识别
        LARGE_INTEGER size{};
        if (GetFileSizeEx(static_cast<HANDLE>(file_), &size) && size.QuadPart == 0)
        {
            static const unsigned char bom[3] = { 0xEF, 0xBB, 0xBF };
            DWORD w = 0;
            WriteFile(static_cast<HANDLE>(file_), bom, 3, &w, nullptr);
        }
    }

    if (consoleDebug)
        console_ = AttachConsole(ATTACH_PARENT_PROCESS) != FALSE;

    inited_ = true;

    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t ts[64]{};
    swprintf_s(ts, L"%04u-%02u-%02u %02u:%02u:%02u.%03u",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    Write(L"INFO ", std::wstring(L"--- session start ") + ts + L" (" + path + L")");

    LeaveCriticalSection(static_cast<CRITICAL_SECTION*>(cs_));
}

void Logger::Write(const wchar_t* level, const std::wstring& msg)
{
    if (!inited_ || !cs_) return;
    EnterCriticalSection(static_cast<CRITICAL_SECTION*>(cs_));

    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t ts[64]{};
    swprintf_s(ts, L"%04u-%02u-%02u %02u:%02u:%02u.%03u",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

    const std::wstring line = std::wstring(L"[") + ts + L"][" + level + L"] " + msg + L"\n";

    if (file_ && file_ != INVALID_HANDLE_VALUE)
    {
        const std::string utf8 = WideToUtf8(line);
        if (!utf8.empty())
        {
            DWORD written = 0;
            WriteFile(static_cast<HANDLE>(file_), utf8.data(),
                      static_cast<DWORD>(utf8.size()), &written, nullptr);
            FlushFileBuffers(static_cast<HANDLE>(file_));
        }
    }
    if (console_)
    {
        const HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
        if (h && h != INVALID_HANDLE_VALUE)
        {
            DWORD written = 0;
            WriteConsoleW(h, line.c_str(), static_cast<DWORD>(line.size()), &written, nullptr);
        }
    }

    LeaveCriticalSection(static_cast<CRITICAL_SECTION*>(cs_));
}

Logger::~Logger()
{
    if (file_ && file_ != INVALID_HANDLE_VALUE)
        CloseHandle(static_cast<HANDLE>(file_));
    if (cs_)
    {
        DeleteCriticalSection(static_cast<CRITICAL_SECTION*>(cs_));
        delete static_cast<CRITICAL_SECTION*>(cs_);
    }
}

} // namespace util
