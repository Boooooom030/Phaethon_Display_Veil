#include "cli.h"
#include "util/text.h"

namespace cli {

Parsed Parse(const wchar_t* const* argv, int argc)
{
    Parsed p;
    p.monitorSpec = L"all";
    bool monitorSpecified = false;

    for (int i = 0; i < argc; ++i)
    {
        const std::wstring a = util::Trim(argv[i]);
        const std::wstring la = util::ToLower(a);

        if (la == L"--dda-fallback") { p.ddaFallback = true; continue; }
        if (la == L"--debug")        { p.debug = true; continue; }
        if (la == L"--all")          { p.monitorSpec = L"all"; monitorSpecified = true; continue; }
        if (la == L"--primary")      { p.monitorSpec = L"primary"; monitorSpecified = true; continue; }
        if (la == L"--monitor")
        {
            if (i + 1 >= argc)
            {
                p.hasError = true;
                p.error = L"--monitor requires a value (index or \\\\.\\DISPLAYn)";
                return p;
            }
            p.monitorSpec = util::Trim(argv[++i]);
            monitorSpecified = true;
            continue;
        }
        if (la == L"--images")
        {
            if (i + 1 >= argc)
            {
                p.hasError = true;
                p.error = L"--images requires a folder path";
                return p;
            }
            p.imageDir = util::Trim(argv[++i]);
            continue;
        }
        if (la == L"--help" || la == L"-h" || la == L"/?")
        {
            p.hasError = true;
            p.error =
                L"usage:\n"
                L"  PrivacyScreen.exe              start background service instance\n"
                L"  PrivacyScreen.exe on [--all|--primary|--monitor N|\\\\.\\DISPLAYn]\n"
                L"                    [--images <folder>]\n"
                L"  PrivacyScreen.exe off\n"
                L"  PrivacyScreen.exe toggle [--images <folder>]\n"
                L"  PrivacyScreen.exe images [<folder>]  set slideshow folder (no arg = back to black)\n"
                L"  PrivacyScreen.exe status\n"
                L"  PrivacyScreen.exe exit\n"
                L"  options: --dda-fallback, --debug";
            return p;
        }

        // 命令词
        if (!p.hasCommand &&
            (la == L"on" || la == L"off" || la == L"toggle" ||
             la == L"status" || la == L"exit" || la == L"images"))
        {
            p.hasCommand = true;
            p.command    = la;
            // images 命令允许位置参数目录：images <folder>
            if (la == L"images" && i + 1 < argc)
            {
                const std::wstring next = util::Trim(argv[i + 1]);
                if (!next.empty() && next[0] != L'-' && next[0] != L'/')
                    p.imageDir = next, ++i;
            }
            continue;
        }

        p.hasError = true;
        p.error = L"unknown argument: " + a;
        return p;
    }

    // images 命令不带目录 = 清除图片库（回到纯黑），不报错
    // ON 未显式指定 monitor 时默认全部
    if (p.hasCommand && p.command == L"on" && !monitorSpecified)
        p.monitorSpec = L"all";
    return p;
}

} // namespace cli
