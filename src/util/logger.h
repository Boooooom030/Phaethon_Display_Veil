#pragma once
// logger.h - thread-safe file log (%LOCALAPPDATA%\Phaethon\phaethon.log)
#include <string>

namespace util {

class Logger {
public:
    static Logger& Instance();

    // consoleDebug=true 时尝试 AttachConsole 输出到父控制台
    void Init(bool consoleDebug);
    void Write(const wchar_t* level, const std::wstring& msg);

    void Info(const std::wstring& msg)  { Write(L"INFO ", msg); }
    void Warn(const std::wstring& msg)  { Write(L"WARN ", msg); }
    void Error(const std::wstring& msg) { Write(L"ERROR", msg); }

private:
    Logger() = default;
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void*  cs_      = nullptr; // CRITICAL_SECTION*
    void*  file_    = nullptr; // HANDLE（Win32 文件句柄，追加写 UTF-8）
    bool   console_ = false;
    bool   inited_  = false;
};

} // namespace util
