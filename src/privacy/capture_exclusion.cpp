#include "capture_exclusion.h"
#include "../util/logger.h"
#include "../util/text.h"

namespace privacy {

namespace {

// WCA_EXCLUDED_FROM_DDA = 24：阻止窗口进入 Desktop Duplication API。
// 未公开 API，需动态查找；仅作为 ddx 的额外 fallback，绝不作为主依赖。
constexpr DWORD kWcaExcludedFromDda = 24;

struct WINDOWCOMPOSITIONATTRIBDATA {
    DWORD  Attrib;
    void*  pvData;
    SIZE_T cbData;
};

using FnSetWindowCompositionAttribute =
    BOOL(WINAPI*)(HWND, const WINDOWCOMPOSITIONATTRIBDATA*);

void ApplyDdaFallback(HWND hwnd)
{
    const HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) return;
    const auto fn = reinterpret_cast<FnSetWindowCompositionAttribute>(
        GetProcAddress(user32, "SetWindowCompositionAttribute"));
    if (!fn)
    {
        util::Logger::Instance().Warn(
            L"SetWindowCompositionAttribute not found; --dda-fallback unavailable");
        return;
    }
    BOOL enable = TRUE;
    WINDOWCOMPOSITIONATTRIBDATA data{};
    data.Attrib = kWcaExcludedFromDda;
    data.pvData = &enable;
    data.cbData = sizeof(enable);
    if (fn(hwnd, &data))
        util::Logger::Instance().Info(L"WCA_EXCLUDED_FROM_DDA applied (fallback)");
    else
        util::Logger::Instance().Warn(L"WCA_EXCLUDED_FROM_DDA apply failed GLE=" +
                                      std::to_wstring(GetLastError()));
}

} // namespace

bool EnableCaptureExclusion(HWND hwnd, bool ddaFallback, std::wstring& err)
{
    // 1) 设置 WDA_EXCLUDEFROMCAPTURE
    if (!SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE))
    {
        const DWORD e = GetLastError();
        err = L"SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE) failed: GLE=" +
              std::to_wstring(e) + L" (" + util::LastErrorMessage(e) + L")";
        return false;
    }

    // 2) 回读校验 —— 不一致视为失败（fail-safe：绝不假装成功）
    DWORD affinity = 0;
    if (!GetWindowDisplayAffinity(hwnd, &affinity))
    {
        const DWORD e = GetLastError();
        err = L"GetWindowDisplayAffinity failed: GLE=" + std::to_wstring(e) + L" (" +
              util::LastErrorMessage(e) + L")";
        return false;
    }
    if (affinity != WDA_EXCLUDEFROMCAPTURE)
    {
        err = L"Capture exclusion verification failed: affinity=0x" +
              std::to_wstring(affinity) + L", expected 0x11 (WDA_EXCLUDEFROMCAPTURE)";
        return false;
    }

    // 3) 可选 ddx fallback
    if (ddaFallback)
        ApplyDdaFallback(hwnd);

    return true;
}

} // namespace privacy
