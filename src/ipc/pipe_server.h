#pragma once
// pipe_server.h — Named Pipe server（\\.\pipe\SunshinePrivacyScreen，文本协议）
// 协议（UTF-8，一行一命令）：ON / OFF / TOGGLE / STATUS / EXIT（大小写不敏感）
// 响应：第一行 "OK" 或 "ERR ..."；STATUS 时随后为状态文本（UTF-8，\n 分隔）
#include <string>

namespace ipc {

// 请求：pipe server 线程 → UI 线程
struct Request {
    enum class Kind { On, Off, Toggle, Status, Exit, Images } kind = Kind::Status;
    std::wstring monitorSpec;     // 仅 ON 有效（管道协议未携带时为 "all"）
    std::wstring imageDir;        // ON / IMAGES 有效；空 = 纯黑模式
    bool         ddaFallback = false;
};

// 响应：UI 线程填好回传
struct Response {
    bool         ok = true;
    std::wstring text;   // STATUS 正文或错误说明
};

// 同步等待 UI 线程完成的载荷
struct Sync {
    void*    done = nullptr;  // HANDLE（自动复位 Event）
    Response resp;
};

// 经 kMsgIpcRequest 投递的完整载荷（UI 线程reinterpret_cast 使用）
struct PendingRequest {
    Sync*   sync = nullptr;
    Request req;
};

// 在独立线程运行阻塞式 pipe server；每个请求经 kMsgIpcRequest 投递到 hwnd 处理。
// onFatal: 管道创建失败时回调（UI 线程记录日志）。
bool StartServer(void* hwndMessage /*HWND*/, void (*onFatal)(const std::wstring&));

// 停止 server 线程并 join
void StopServer();

// 控制客户端：连接管道发送命令。返回通信是否成功（ok/responseText 承载业务结果）。
bool SendCommand(const Request& req, int timeoutMs, bool& ok, std::wstring& respText);

} // namespace ipc
