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
    // spec: 本次启用的显示器选择器（"all"/"primary"/序号/设备名），记录用于
    //       显示变化重建与托盘勾选状态
    EnableReport Enable(const std::vector<monitor::MonitorInfo>& targets,
                        const std::wstring& spec);

    // 关闭全部 overlay（幂等；emergency=true 时即使状态机认为已关闭也强制执行）
    void Disable(bool emergency = false);

    // 显示布局变化：按当前期望状态重建或收缩 overlay
    void HandleDisplayChange();

    // 周期性保险：验证 overlay 存活并重申 TOPMOST；失效则重建
    void Reassert();

    // 广播重绘（图片切换 / 图片库变更后调用）；仅在 ON 状态有意义
    void InvalidateAllOverlays();

    // 当前生效的显示器选择器（"all"/设备名…），供托盘菜单勾选与开关复用
    std::wstring CurrentSpec() const;

    // 当前是否处于 ON 且覆盖全部屏幕
    bool IsAllCovered() const;

    // 当前是否处于 ON 且覆盖指定设备（\\.\DISPLAYn，大小写不敏感）
    bool IsCovered(const std::wstring& device) const;

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
