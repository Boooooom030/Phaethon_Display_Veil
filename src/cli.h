#pragma once
// cli.h — 命令行解析与控制客户端入口
#include <string>

namespace cli {

// 去掉 argv[0] 后的参数解析结果
struct Parsed {
    // server 模式标志（无命令参数时启动后台实例）
    bool         runServer   = true;
    bool         ddaFallback = false;
    bool         debug       = false;

    // 图片模式：非空 = 遮罩显示该文件夹图片（幻灯片），空 = 纯黑
    std::wstring imageDir;

    // 控制命令
    bool         hasCommand  = false;
    std::wstring command;      // on/off/toggle/status/exit/images（小写）
    std::wstring monitorSpec;  // on 的 --all/--primary/--monitor 值，默认 "all"

    // 解析失败时的用户可读消息
    bool         hasError    = false;
    std::wstring error;
};

Parsed Parse(const wchar_t* const* argv, int argc);

} // namespace cli
