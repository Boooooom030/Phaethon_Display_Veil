#pragma once
// overlay_window.h — 单个遮罩窗口：创建、GDI 纯黑/图片渲染、capture exclusion
#include <windows.h>
#include <string>

namespace overlay {

struct CreateResult {
    HWND         hwnd = nullptr;
    std::wstring error;   // 失败原因（人类可读）
};

// 在给定 monitor 矩形上创建完全不透明遮罩（纯黑或图片幻灯片）。
// ddaFallback: 附加 WCA_EXCLUDED_FROM_DDA。
// 失败时保证不留半成品窗口。
CreateResult Create(const RECT& monitorRect, bool ddaFallback);

// WindowProc（内部使用，导出供类注册）
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

} // namespace overlay
