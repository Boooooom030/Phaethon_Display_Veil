#include "i18n.h"
#include "text.h"

namespace i18n {

namespace {

enum class Lang { Zh, En };
Lang g_lang = Lang::En;

struct Entry {
    const wchar_t* zh;
    const wchar_t* en;
};

// Order must match the S enum
constexpr Entry kTable[] = {
    /* TrayTip     */ { L"隐私遮罩",              L"Privacy Screen" },
    /* EnablePrivacy*/{ L"开启遮罩",              L"Enable privacy" },
    /* SelectScreen*/ { L"选择屏幕",              L"Select screen" },
    /* AllScreens  */ { L"全部屏幕",              L"All screens" },
    /* MonitorPrefix*/{ L"显示器 ",               L"Monitor " },
    /* PrimaryTag  */ { L" [主]",                 L" [primary]" },
    /* ChooseImages*/ { L"选择图片文件夹…",       L"Choose image folder…" },
    /* ChangeImages*/ { L"更换图片文件夹…",       L"Change image folder…" },
    /* BackToBlack */ { L"恢复纯黑",              L"Back to black" },
    /* Status      */ { L"状态",                  L"Status" },
    /* Exit        */ { L"退出",                  L"Exit" },
    /* FolderDialogTitle */ { L"选择遮罩图片文件夹", L"Choose privacy image folder" },
};

} // namespace

void Init()
{
    // 环境变量优先：PRIVACY_SCREEN_LANG=zh / en
    wchar_t buf[8]{};
    const DWORD n = GetEnvironmentVariableW(L"PRIVACY_SCREEN_LANG", buf, 8);
    if (n > 0 && n < 8)
    {
        const std::wstring v = util::ToLower(buf);
        if (v.rfind(L"zh", 0) == 0) { g_lang = Lang::Zh; return; }
        if (v.rfind(L"en", 0) == 0) { g_lang = Lang::En; return; }
    }

    // 系统 UI 语言：中文族 → zh，其他 → en
    const LANGID lid = GetUserDefaultUILanguage();
    g_lang = (PRIMARYLANGID(lid) == 0x0004) ? Lang::Zh : Lang::En;
}

const wchar_t* Str(S id)
{
    const size_t i = static_cast<size_t>(id);
    if (i >= sizeof(kTable) / sizeof(kTable[0])) return L"";
    return g_lang == Lang::Zh ? kTable[i].zh : kTable[i].en;
}

} // namespace i18n
