// mincs.cpp — 复现: 静态单例 + new CRITICAL_SECTION + 递归进入 + Write 线路
#include <windows.h>
#include <cstdio>
#include <string>

static void Marker(const char* s)
{
    HANDLE f = CreateFileW(L"mincs_steps.txt", FILE_APPEND_DATA, 0, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE)
    {
        DWORD w = 0;
        WriteFile(f, s, static_cast<DWORD>(lstrlenA(s)), &w, nullptr);
        CloseHandle(f);
    }
}

struct L {
    CRITICAL_SECTION* cs = nullptr;
    FILE* file = nullptr;
    void Init()
    {
        Marker("A_new_cs\r\n");
        cs = new CRITICAL_SECTION{};
        InitializeCriticalSection(cs);
        Marker("B_enter\r\n");
        EnterCriticalSection(cs);
        Marker("C_fopen\r\n");
        FILE* f = nullptr;
        _wfopen_s(&f, L"mincs.log", L"a, ccs=UTF-8");
        file = f;
        Marker("D_write\r\n");
        Write(L"INFO ", std::wstring(L"--- session start test"));
        Marker("E_leave\r\n");
        LeaveCriticalSection(cs);
        Marker("F_initdone\r\n");
    }
    void Write(const wchar_t* level, const std::wstring& msg)
    {
        EnterCriticalSection(cs); // 递归进入（同线程）
        SYSTEMTIME st{};
        GetLocalTime(&st);
        wchar_t ts[64]{};
        swprintf_s(ts, L"%04u-%02u-%02u %02u:%02u:%02u.%03u",
                   st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        const std::wstring line = std::wstring(L"[") + ts + L"][" + level + L"] " + msg + L"\n";
        // WideToUtf8
        std::string out;
        if (!line.empty())
        {
            const int n = WideCharToMultiByte(CP_UTF8, 0, line.data(),
                                              static_cast<int>(line.size()), nullptr, 0,
                                              nullptr, nullptr);
            out.resize(static_cast<size_t>(n));
            if (n > 0)
                WideCharToMultiByte(CP_UTF8, 0, line.data(),
                                    static_cast<int>(line.size()), out.data(), n,
                                    nullptr, nullptr);
        }
        if (file)
        {
            fwrite(out.data(), 1, out.size(), static_cast<FILE*>(file));
            fflush(static_cast<FILE*>(file));
        }
        LeaveCriticalSection(cs);
    }
};

int wmain()
{
    Marker("0_start\r\n");
    static L g;
    g.Init();
    Marker("1_second_write\r\n");
    g.Write(L"INFO ", L"second line");
    Marker("2_done\r\n");
    return 9;
}
