#include "monitor_manager.h"
#include "../util/text.h"

namespace monitor {

std::vector<MonitorInfo> Enumerate()
{
    std::vector<MonitorInfo> out;
    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR hmon, HDC, LPRECT, LPARAM lparam) -> BOOL {
            auto* list = reinterpret_cast<std::vector<MonitorInfo>*>(lparam);
            MONITORINFOEXW mi{};
            mi.cbSize = sizeof(mi);
            if (!GetMonitorInfoW(hmon, &mi))
                return TRUE; // 跳过该显示器，继续枚举
            MonitorInfo m;
            m.handle  = hmon;
            m.device  = mi.szDevice;
            m.rect    = mi.rcMonitor;
            m.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
            list->push_back(m);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&out));
    return out;
}

std::vector<MonitorInfo> Select(const std::wstring& specIn)
{
    const std::vector<MonitorInfo> all = Enumerate();
    const std::wstring spec = util::ToLower(util::Trim(specIn));

    if (spec.empty() || spec == L"all")
        return all;

    if (spec == L"primary")
    {
        std::vector<MonitorInfo> out;
        for (const auto& m : all)
            if (m.primary) out.push_back(m);
        return out;
    }

    // 1-based 序号
    if (!spec.empty() && spec.find_first_not_of(L"0123456789") == std::wstring::npos)
    {
        const int idx = _wtoi(spec.c_str());
        if (idx >= 1 && idx <= static_cast<int>(all.size()))
            return { all[static_cast<size_t>(idx - 1)] };
        return {};
    }

    // 设备名匹配（"\\.\DISPLAY2" 或 "DISPLAY2"，后缀匹配）
    std::vector<MonitorInfo> out;
    for (const auto& m : all)
    {
        const std::wstring dev = util::ToLower(m.device);
        if (dev == spec ||
            (dev.size() >= spec.size() &&
             dev.compare(dev.size() - spec.size(), spec.size(), spec) == 0))
        {
            out.push_back(m);
            break;
        }
    }
    return out;
}

std::wstring Describe(const MonitorInfo& m)
{
    std::wstring s = m.device + L" rect=" + util::RectToString(m.rect);
    if (m.primary) s += L" [primary]";
    return s;
}

} // namespace monitor
