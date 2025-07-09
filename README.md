# mgServer DLL 使用说明

本项目基于 [Mongoose](https://github.com/cesanta/mongoose) 封装，提供了简单易用的 HTTP/HTTPS + WebSocket 服务能力，支持 TLS、日志控制、回调等功能，适合 C/C++、Lazarus/Delphi 等多语言集成。

---

## 目录结构

```
mgServer.dll
mgServer.def
mgServerdll.h
mgServerdll.c
mgServer.c
mongoose.c
mongoose.h
server.com.crt   // 证书（可选，启用TLS时需要）
server.com.key   // 私钥（可选，启用TLS时需要）
test.c           // 示例测试程序
```

---

## 编译

Windows 下（MinGW）示例：

```sh
gcc -shared -o mgServer.dll mgServerdll.c mgServer.c mongoose.c mgServer.def \
    -Wl,--out-implib=libmgServer.a \
    -Wl,--subsystem,windows \
    -DMGSERVER_EXPORTS -DMG_ENABLE_OPENSSL=1 -DMG_ENABLE_IPV6=1 -DMG_TLS=MG_TLS_OPENSSL -I. \
    -lssl -lcrypto -lws2_32 -lpthread
```

> **注意**：务必在编译时指定 `.def` 文件，确保 DLL 导出未修饰名，便于 Pascal/Lazarus 直接调用。

---

## 导出接口

详见 `mgServerdll.h`，主要接口如下：

- `ServerHandle* Server_Create(void);`
- `void Server_Destroy(ServerHandle* h);`
- `int Server_SetConfig(ServerHandle* h, const ServerConfig* config);`
- `int Server_SetCallbacks(ServerHandle* h, HttpCallback http_cb, WsCallback ws_cb, void* user_data);`
- `int Server_Start(ServerHandle* h);`
- `void Server_Stop(ServerHandle* h);`
- `void Server_Poll(ServerHandle* h, int timeout_ms);`
- `int Server_WsSendToOne(ServerHandle* h, unsigned long long conn_id, const char* data, int len, int binary);`
- `int Server_WsBroadcast(ServerHandle* h, const char* data, int len, int binary);`
- `int Server_HttpReply(ServerHandle* h, unsigned long long conn_id, int status_code, const char* headers, const char* body, int body_len);`
- `void Server_SetLogLevel(int enabled, LogLevel level);`
- `void Server_SetLogTarget(LogTarget target, const char* filename);`
- `int Server_HttpServeFile(ServerHandle* h, unsigned long long conn_id, const char* file_path, const char* extra_headers);`  
  通过指定连接ID和文件路径发送文件内容，extra_headers可设置Content-Type等HTTP头

---

## 日志控制

- `Server_SetLogLevel(int enabled, LogLevel level);`  
  控制日志开关和级别（如 `LOG_LEVEL_INFO`, `LOG_LEVEL_DEBUG` 等）。
- `Server_SetLogTarget(LogTarget target, const char* filename);`  
  控制日志输出到控制台或文件（`LOG_TARGET_CONSOLE` 或 `LOG_TARGET_FILE`）。

---

## TLS 支持

- 配置 `ServerConfig` 结构体时，填写 `cert_file` 和 `key_file` 字段即可启用 HTTPS/WSS。
- 证书和私钥文件需放在可访问目录下。

---

## C 与 Lazarus/Delphi 对接关键点

**1. 调用约定统一为 `stdcall`**  
所有 DLL 导出函数和回调类型都必须使用 `__stdcall`，Pascal 侧声明用 `stdcall`，确保参数压栈方式一致。

**2. 64位参数兼容性**  
- 如果 DLL 接口有 `unsigned long long`（如 conn_id），Pascal 侧用 `Int64` 对应。
- 32位下，`stdcall` 比 `cdecl` 更安全，但仍建议尽量避免回调参数中混用多个 64 位类型。
- 若遇到参数错位，建议将 conn_id 等参数统一为 32 位（如 `Cardinal`/`unsigned int`），DLL 和 Pascal 同步修改。

**3. .def 文件导出未修饰名**  
- Windows 下 `stdcall` 会导致导出名被修饰（如 `_Server_Create@0`），
- 必须用 `.def` 文件导出未修饰名（如 `Server_Create`），否则 Pascal/Delphi 找不到入口点。

**4. Pascal 单元声明示例**

```pascal
type
  TServerHandle = Pointer;
  THttpCallback = procedure(server: TServerHandle; conn_id: Int64; request: PAnsiChar; request_len: SizeUInt; response: PPAnsiChar; response_len: PSizeUInt); stdcall;
  TWsCallback = procedure(server: TServerHandle; conn_id: Int64; data: PAnsiChar; len: SizeUInt; binary: Integer); stdcall;

function Server_Create: TServerHandle; stdcall; external 'mgServer.dll';
function Server_WsSendToOne(handle: TServerHandle; conn_id: Int64; data: PAnsiChar; len: Integer; binary: Integer): Integer; stdcall; external 'mgServer.dll';
```
具体操作可以查看另一个项目的[示例代码](https://github.com/shojinto/mgserverdlldemo)

**5. 结构体对齐与类型一致**  
- Pascal 侧结构体需加 `{$ALIGN 8}`、`{$PACKRECORDS C}`，字段顺序、类型与 C 完全一致。

**6. 日志格式化注意**  
- C 侧日志打印 `%llu` 用于 64 位无符号整数，`%.*s` 用于定长字符串，避免二进制或未结尾字符串导致日志异常。

**7. DLL/EXE 位数必须一致**  
- 32位 DLL 只能被 32位程序调用，64位 DLL 只能被 64位程序调用。

---

## 使用示例（C 伪代码）

```c
#include "mgServerdll.h"

void __stdcall http_callback(ServerHandle* server, unsigned long long conn_id, const char* request, size_t request_len, char** response, size_t* response_len) {
    // 处理HTTP请求
}

void __stdcall ws_callback(ServerHandle* server, unsigned long long conn_id, const char* data, size_t len, int binary) {
    // 处理WebSocket消息
}

int main() {
    ServerHandle* server = Server_Create();
    // ...配置、回调、启动、事件循环...
}
```

---

## 注意事项

- 必须定期调用 `Server_Poll`，否则不会响应任何请求。
- 回调函数内不要阻塞太久，避免影响事件循环。
- 日志文件如需切换，需先调用 `Server_SetLogTarget(LOG_TARGET_CONSOLE, NULL)` 再切换到新文件。
- 若遇到参数错位、找不到入口点等问题，优先检查调用约定、参数类型、.def 文件和 DLL/EXE 位数。
- 使用Server_HttpServeFile时需确保文件路径有读取权限，建议使用绝对路径
- extra_headers需以`\r\n\r\n`结尾，例如："Content-Type: application/octet-stream\r\n"
- 文件路径建议使用UTF-8编码，Pascal侧调用时需用`Utf8ToAnsi(UTF8Encode(filepath))`转换路径

---

## 许可证

本项目基于 Mongoose，遵循其开源协议。

---

如有问题欢迎提 Issue 或联系作者。
