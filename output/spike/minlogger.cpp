// minlogger.cpp — 复现: /MT 下 _wfopen_s("a, ccs=UTF-8") + fwrite + fflush 是否 fail-fast
#include <windows.h>
#include <cstdio>

static void Marker(const char* s)
{
    HANDLE f = CreateFileW(L"minlogger_steps.txt", FILE_APPEND_DATA, 0, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE)
    {
        DWORD w = 0;
        WriteFile(f, s, static_cast<DWORD>(lstrlenA(s)), &w, nullptr);
        CloseHandle(f);
    }
}

int wmain()
{
    Marker("step1_open\r\n");
    FILE* f = nullptr;
    _wfopen_s(&f, L"minlogger_test.log", L"a, ccs=UTF-8");
    Marker("step2_opened\r\n");
    if (!f) return 1;

    const char line[] = "[2026-09-08] INFO test line\n";
    Marker("step3_write\r\n");
    fwrite(line, 1, sizeof(line) - 1, f);
    Marker("step4_fflush\r\n");
    fflush(f);
    Marker("step5_close\r\n");
    fclose(f);
    Marker("step6_done\r\n");
    return 7;
}
