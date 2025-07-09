#ifndef MGSERVERDLL_H
#define MGSERVERDLL_H

#include <stddef.h>
#include <stdio.h>

// 调试日志宏，包含文件名和行号
#define LOG(level, fmt, ...) log_with_time(level, __FILE__, __LINE__, fmt, ##__VA_ARGS__);

#ifdef MGSERVER_EXPORTS
#define MG_SERVER_API __declspec(dllexport)
#else
#define MG_SERVER_API __declspec(dllimport)
#endif

typedef struct Server ServerHandle;

typedef struct {
    const char* method;   // "GET", "POST" 等
    const char* uri;
    size_t uri_len;
    const char* headers;  // 可选
    const char* body;     // 可选
    size_t body_len;
} HttpRequest;


typedef struct {
    int status_code;         // HTTP状态码，如200、404等
    const char* headers;     // 额外响应头（可为NULL）
    const char* body;        // 响应内容（可为NULL）
    size_t body_len;         // 响应内容长度
} HttpResponse;

typedef struct {
    const char* data;     // 消息内容
    size_t data_len;      // 消息长度
    int binary;           // 1=二进制，0=文本
    // 可扩展更多字段，如opcode、mask等
} WsMessage;

typedef void (__stdcall *HttpCallback)(ServerHandle* server, unsigned long long conn_id, const HttpRequest* request, HttpResponse* response);
typedef void (__stdcall *WsCallback)(ServerHandle* server, unsigned long long conn_id, const WsMessage* message);

typedef struct {
    int port;
    int use_tls;
    int enable_ws; // 是否启用 WebSocket
    const char* cert_file;
    const char* key_file;
    const char* root_dir;
} ServerConfig;

typedef enum {
    LOG_LEVEL_NONE = 0,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_WARN,
    LOG_LEVEL_INFO,
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_TRACE
} LogLevel;

typedef enum {
    LOG_TARGET_CONSOLE = 0,
    LOG_TARGET_FILE = 1
} LogTarget;

MG_SERVER_API ServerHandle* __stdcall Server_Create(void);
MG_SERVER_API void __stdcall Server_Destroy(ServerHandle* h);
MG_SERVER_API int __stdcall Server_SetConfig(ServerHandle* h, const ServerConfig* c);
MG_SERVER_API int __stdcall Server_SetCallbacks(ServerHandle* h, HttpCallback http_cb, WsCallback ws_cb, void* user_data);
MG_SERVER_API int __stdcall Server_Start(ServerHandle* h);
MG_SERVER_API void __stdcall Server_Stop(ServerHandle* h);
MG_SERVER_API void __stdcall Server_Poll(ServerHandle* h, int timeout_ms);
MG_SERVER_API void __stdcall Server_SetLogLevel(int enabled, LogLevel level);
MG_SERVER_API void __stdcall Server_SetLogTarget(LogTarget target, const char* filename); // filename 仅在 LOG_TARGET_FILE 时有效
MG_SERVER_API int __stdcall Server_WsSendToOne(ServerHandle* h, unsigned long long conn_id, const WsMessage* wm);
MG_SERVER_API int __stdcall Server_WsBroadcast(ServerHandle* h, const WsMessage* wm);
MG_SERVER_API int __stdcall Server_HttpReply(ServerHandle* h, unsigned long long conn_id, const HttpResponse* res);
MG_SERVER_API int __stdcall Server_HttpServeFile(ServerHandle* h, unsigned long long conn_id, const char* file_path, const char* extra_headers);

#endif // MGSERVERDLL_H