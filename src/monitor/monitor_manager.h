#pragma once
// monitor_manager.h — EnumDisplayMonitors 枚举与目标筛选
#include <windows.h>
#include <string>
#include <vector>

namespace monitor {

struct MonitorInfo {
    HMONITOR    handle  = nullptr;
    std::wstring device;   // 例如 \\.\DISPLAY1
    RECT        rect{};    // rcMonitor（整屏，含任务栏）
    bool        primary = false;
};

// 枚举当前全部显示器（顺序稳定）
std::vector<MonitorInfo> Enumerate();

// 按 spec 筛选：
//   L"" / L"all"          -> 全部
//   L"primary"            -> 仅主屏
//   L"2"                  -> 1-based 枚举序号
//   L"\\.\DISPLAY2" / L"DISPLAY2" -> 按设备名（大小写不敏感，允许省略 \\.\ 前缀）
// 未匹配 -> 返回空 vector
std::vector<MonitorInfo> Select(const std::wstring& spec);

// 单行描述："\\.\DISPLAY1 rect=(0,0,1920,1080) [primary]"
std::wstring Describe(const MonitorInfo& m);

} // namespace monitor
