#pragma once
// capture_exclusion.h — WDA_EXCLUDEFROMCAPTURE 应用与校验（+ 可选 WCA_EXCLUDED_FROM_DDA fallback）
#include <windows.h>
#include <string>

namespace privacy {

// 成功：true（已验证 affinity == WDA_EXCLUDEFROMCAPTURE）
// 失败：false，err 填充人类可读原因（含 GetLastError 值）
bool EnableCaptureExclusion(HWND hwnd, bool ddaFallback, std::wstring& err);

} // namespace privacy
