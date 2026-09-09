// minmain.cpp — 判断 /SUBSYSTEM:WINDOWS + /ENTRY:wmainCRTStartup 环境本身是否崩溃
#include <windows.h>

int wmain(int argc, wchar_t* argv[])
{
    HANDLE f = CreateFileW(L"minmain_marker.txt", FILE_APPEND_DATA, 0, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE)
    {
        const char msg[] = "wmain reached\r\n";
        DWORD w = 0;
        WriteFile(f, msg, sizeof(msg) - 1, &w, nullptr);
        CloseHandle(f);
    }
    (void)argc; (void)argv;
    return 42;
}
