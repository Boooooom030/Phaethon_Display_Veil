#pragma once
// overlay_manager.h — 状态机（OFF/ENABLING/ON/DISABLING/ERROR）与 overlay 集合管理
#include <windows.h>
#include <string>
#include <vector>

#include "../monitor/monitor_manager.h"

namespace overlay {

enum class State { Off, Enabling, On, Disabling, Error };

const wchar_t* StateName(State s);

class Manager {
public:
    void SetDdaFallback(bool on) { ddaFallback_ = on; }

    State state() const { return state_; }

    struct EnableReport {
        bool             success = false;
        std::wstring     error;      // 首个失败原因
        size_t           covered = 0;
        std::vector<std::wstring> coveredDevices;
    };

    // 开启：枚举 monitors -> 逐个创建 overlay -> 任一失败则全部回滚
    EnableReport Enable(const std::vector<monitor::MonitorInfo>& targets);

    // 关闭全部 overlay（幂等；emergency=true 时即使状态机认为已关闭也强制执行）
    void Disable(bool emergency = false);

    // 显示布局变化：按当前期望状态重建或收缩 overlay
    void HandleDisplayChange();

    // 周期性保险：验证 overlay 存活并重申 TOPMOST；失效则重建
    void Reassert();

    // 广播重绘（图片切换 / 图片库变更后调用）；仅在 ON 状态有意义
    void InvalidateAllOverlays();

    // status 文本（多行）
    std::wstring StatusText() const;

    // 允许某个 overlay 窗口销毁时同步内部列表（外部窗口被强杀等极端情形）
    void OnOverlayDestroyed(HWND hwnd);

private:
    void DestroyAllLocked();

    State                       state_ = State::Off;
    bool                        ddaFallback_ = false;
    bool                        wantOn_ = false;          // 用户期望状态
    std::wstring                spec_ = L"all";           // monitor 选择器
    std::vector<monitor::MonitorInfo> lastTargets_;
    std::vector<HWND>           overlays_;
    std::wstring                lastError_;
};

Manager& GetManager();

} // namespace overlay
