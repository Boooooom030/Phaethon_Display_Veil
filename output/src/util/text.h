#pragma once
// text.h — 字符串/控制台小工具（header-only）
#include <windows.h>
#include <string>

namespace util {

inline std::wstring Utf8ToWide(const std::string& s)
{
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    if (n > 0)
        MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

inline std::string WideToUtf8(const std::wstring& w)
{
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
                                      nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    if (n > 0)
        WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
                            s.data(), n, nullptr, nullptr);
    return s;
}

inline std::wstring ToLower(std::wstring s)
{
    if (!s.empty()) CharLowerBuffW(s.data(), static_cast<DWORD>(s.size()));
    return s;
}

inline std::wstring ToUpper(std::wstring s)
{
    if (!s.empty()) CharUpperBuffW(s.data(), static_cast<DWORD>(s.size()));
    return s;
}

inline std::wstring Trim(const std::wstring& s)
{
    size_t b = 0, e = s.size();
    while (b < e && iswspace(s[b])) ++b;
    while (e > b && iswspace(s[e - 1])) --e;
    return s.substr(b, e - b);
}

inline std::wstring RectToString(const RECT& rc)
{
    return L"(" + std::to_wstring(rc.left) + L"," + std::to_wstring(rc.top) + L"," +
           std::to_wstring(rc.right) + L"," + std::to_wstring(rc.bottom) + L")";
}

inline std::wstring LastErrorMessage(DWORD err)
{
    LPWSTR buf = nullptr;
    const DWORD n = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, err, 0, reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
    std::wstring msg = buf ? std::wstring(buf, n) : L"unknown error";
    if (buf) LocalFree(buf);
    while (!msg.empty() && (msg.back() == L'\r' || msg.back() == L'\n' || msg.back() == L' '))
        msg.pop_back();
    return msg;
}

// 输出到父进程控制台（WINDOWS 子系统程序 AttachConsole 后用 WriteConsoleW）。
// 无父控制台时静默忽略。
inline void ConsoleOut(const std::wstring& text)
{
    if (!AttachConsole(ATTACH_PARENT_PROCESS))
        return;
    const HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!h || h == INVALID_HANDLE_VALUE)
        return;
    std::wstring line = text + L"\r\n";
    DWORD written = 0;
    WriteConsoleW(h, line.c_str(), static_cast<DWORD>(line.size()), &written, nullptr);
}

} // namespace util
