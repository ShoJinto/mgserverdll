#include <stdio.h>
#include <windows.h>
#include <conio.h>   // 加这一行
#include "mgServerdll.h"


// HTTP回调函数示例
void __stdcall http_callback(ServerHandle* server, unsigned long long conn_id, const HttpRequest* request, HttpResponse* response) {
    static const char* html = "<html><body><h1>Hello from DLL Server</h1></body></html>";
    response->status_code = 200;
    response->headers = "Content-Type: text/html\r\n";
    response->body = html;
    response->body_len = strlen(html);
    printf("HTTP request received: %.*s", (int)request->uri_len, request->uri);

    Server_HttpServeFile(server, conn_id, "static/mgserver.dll", "Content-Type: application/octet-stream\r\n");
}

// WebSocket回调函数示例
void __stdcall ws_callback(ServerHandle* server, unsigned long long conn_id, const WsMessage* message) {
    printf("Received WebSocket message: %.*s\n", (int)message->data_len, message->data);
    // 回声响应
    Server_WsSendToOne(server, conn_id, message);
}

int main() {
    // 加载DLL
    HMODULE hDll = LoadLibraryA("mgServer.dll");
    if (!hDll) {
        printf("Failed to load DLL\n");
        return 1;
    }

    // 创建服务器实例
    ServerHandle* server = Server_Create();
    if (!server) {
        printf("Failed to create server\n");
        FreeLibrary(hDll);
        return 1;
    }

    // 配置服务器
    ServerConfig config = {
        .port = 8000,
        .use_tls = 1, // 0=HTTP, 1=HTTPS
        .enable_ws = 1, // 启用 WebSocket
        .cert_file = "tfxing.com.crt",
        .key_file = "tfxing.com.key",
        .root_dir = "."
    };
    Server_SetConfig(server, &config);

    Server_SetLogLevel(1, LOG_LEVEL_DEBUG); // 启用调试日志
    Server_SetLogTarget(LOG_TARGET_FILE, "server.log"); // 输出到文件

    // 设置回调函数
    Server_SetCallbacks(server, http_callback, ws_callback, NULL);

    // 启动服务器
    if (Server_Start(server) != 0) {
        printf("Failed to start server\n");
        Server_Destroy(server);
        FreeLibrary(hDll);
        return 1;
    }

    printf("Server started on port %d\n", config.port);
    printf("Press Enter to stop...\n");
    for (;;) {
        Server_Poll(server, 100);
        if (_kbhit()) break;
        Sleep(10);
    }
    getchar();

    // 停止服务器
    Server_Stop(server);
    Server_Destroy(server);
    FreeLibrary(hDll);
    return 0;
}