#include "overlay_manager.h"
#include "../util/logger.h"
#include "overlay_window.h"

namespace overlay {

namespace {
CRITICAL_SECTION& Cs()
{
    static CRITICAL_SECTION cs;
    static const bool inited = [] {
        InitializeCriticalSection(&cs);
        return true;
    }();
    (void)inited;
    return cs;
}
} // namespace

const wchar_t* StateName(State s)
{
    switch (s)
    {
    case State::Off:       return L"OFF";
    case State::Enabling:  return L"ENABLING";
    case State::On:        return L"ON";
    case State::Disabling: return L"DISABLING";
    case State::Error:     return L"ERROR";
    }
    return L"?";
}

Manager& GetManager()
{
    static Manager m;
    return m;
}

void Manager::DestroyAllLocked()
{
    for (const HWND hwnd : overlays_)
    {
        if (hwnd && IsWindow(hwnd))
            DestroyWindow(hwnd);
    }
    overlays_.clear();
}

Manager::EnableReport Manager::Enable(const std::vector<monitor::MonitorInfo>& targets)
{
    EnterCriticalSection(&Cs());
    EnableReport report;

    if (targets.empty())
    {
        report.error = L"no matching monitor";
        lastError_   = report.error;
        state_       = State::Error;
        LeaveCriticalSection(&Cs());
        return report;
    }

    // 幂等：已有 overlay 先销毁，防止重复 ON 泄漏旧窗口
    if (!overlays_.empty())
        DestroyAllLocked();

    state_ = State::Enabling;
    util::Logger::Instance().Info(L"ENABLING: " + std::to_wstring(targets.size()) +
                                  L" monitor(s), dda-fallback=" +
                                  (ddaFallback_ ? L"on" : L"off"));

    std::vector<HWND> created;
    for (const auto& m : targets)
    {
        util::Logger::Instance().Info(L"Creating overlay for " + Describe(m));
        CreateResult r = Create(m.rect, ddaFallback_);
        if (!r.hwnd)
        {
            // 回滚：销毁已创建的全部 overlay —— 不允许“一块黑一块没黑”
            util::Logger::Instance().Error(L"Overlay create FAILED on " + m.device +
                                           L": " + r.error);
            for (const HWND h : created)
                if (h && IsWindow(h)) DestroyWindow(h);
            created.clear();

            report.error = m.device + L": " + r.error;
            lastError_   = report.error;
            state_       = State::Error;
            LeaveCriticalSection(&Cs());
            return report;
        }
        wchar_t hwndStr[32]{};
        swprintf_s(hwndStr, L"0x%p", static_cast<void*>(r.hwnd));
        util::Logger::Instance().Info(L"Overlay HWND=" + std::wstring(hwndStr) +
                                      L" for " + m.device);
        created.push_back(r.hwnd);
        report.coveredDevices.push_back(m.device);
    }

    overlays_     = created;
    lastTargets_  = targets;
    wantOn_       = true;
    state_        = State::On;
    report.success  = true;
    report.covered  = created.size();
    lastError_.clear();

    util::Logger::Instance().Info(L"Privacy mode ACTIVE (" +
                                  std::to_wstring(created.size()) + L" overlay(s))");
    LeaveCriticalSection(&Cs());
    return report;
}

void Manager::Disable(bool emergency)
{
    EnterCriticalSection(&Cs());
    if (!emergency && state_ == State::Off)
    {
        LeaveCriticalSection(&Cs());
        return;
    }
    state_ = State::Disabling;
    util::Logger::Instance().Info(emergency ? L"EMERGENCY DISABLE (panic hotkey)"
                                            : L"DISABLING");
    DestroyAllLocked();
    state_  = State::Off;
    wantOn_ = false;
    lastError_.clear();
    util::Logger::Instance().Info(L"Privacy mode OFF");
    LeaveCriticalSection(&Cs());
}

void Manager::HandleDisplayChange()
{
    EnterCriticalSection(&Cs());
    if (!wantOn_)
    {
        LeaveCriticalSection(&Cs());
        return;
    }
    util::Logger::Instance().Info(L"Display change: rebuilding overlays");
    DestroyAllLocked();
    const auto targets = monitor::Select(spec_);
    if (targets.empty())
    {
        lastError_ = L"display change: no monitors matched";
        state_     = State::Error;
        LeaveCriticalSection(&Cs());
        return;
    }
    std::vector<HWND> created;
    for (const auto& m : targets)
    {
        CreateResult r = Create(m.rect, ddaFallback_);
        if (!r.hwnd)
        {
            for (const HWND h : created)
                if (h && IsWindow(h)) DestroyWindow(h);
            created.clear();
            lastError_ = L"rebuild failed on " + m.device + L": " + r.error;
            state_     = State::Error;
            util::Logger::Instance().Error(lastError_);
            LeaveCriticalSection(&Cs());
            return;
        }
        created.push_back(r.hwnd);
    }
    overlays_ = created;
    state_    = State::On;
    util::Logger::Instance().Info(L"Rebuild OK (" + std::to_wstring(created.size()) +
                                  L" overlay(s))");
    LeaveCriticalSection(&Cs());
}

void Manager::Reassert()
{
    EnterCriticalSection(&Cs());
    if (state_ != State::On)
    {
        LeaveCriticalSection(&Cs());
        return;
    }
    bool allAlive = true;
    for (const HWND hwnd : overlays_)
    {
        if (!hwnd || !IsWindow(hwnd)) { allAlive = false; break; }
    }
    if (allAlive)
    {
        // 重申 TOPMOST，防御其他 topmost 窗口插入
        for (const HWND hwnd : overlays_)
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        LeaveCriticalSection(&Cs());
        return;
    }
    LeaveCriticalSection(&Cs());
    util::Logger::Instance().Warn(L"Reassert: dead overlay detected, rebuilding");
    HandleDisplayChange();
}

void Manager::InvalidateAllOverlays()
{
    EnterCriticalSection(&Cs());
    for (const HWND hwnd : overlays_)
        if (hwnd && IsWindow(hwnd))
            InvalidateRect(hwnd, nullptr, FALSE);
    LeaveCriticalSection(&Cs());
}

void Manager::OnOverlayDestroyed(HWND hwnd)
{
    EnterCriticalSection(&Cs());
    std::erase_if(overlays_, [hwnd](HWND h) { return h == hwnd; });
    // 全部 overlay 意外消失且期望为 ON：视为异常
    if (wantOn_ && overlays_.empty() && state_ == State::On)
    {
        state_     = State::Error;
        lastError_ = L"all overlays vanished unexpectedly";
        util::Logger::Instance().Error(lastError_);
    }
    LeaveCriticalSection(&Cs());
}

std::wstring Manager::StatusText() const
{
    EnterCriticalSection(&Cs());
    std::wstring s = std::wstring(L"state=") + StateName(state_);
    s += L", overlays=" + std::to_wstring(overlays_.size());
    if (wantOn_) s += L", want=ON";
    for (const auto& m : lastTargets_)
        s += L"\n  " + Describe(m);
    if (!lastError_.empty())
        s += L"\n  lastError=" + lastError_;
    LeaveCriticalSection(&Cs());
    return s;
}

} // namespace overlay
