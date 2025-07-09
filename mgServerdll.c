#include "mgServerdll.h"
#include "mongoose.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

static LogLevel g_log_level = LOG_LEVEL_INFO;
static int g_log_enabled = 1;
static LogTarget g_log_target = LOG_TARGET_CONSOLE;
static FILE* g_log_file = NULL;

struct Server {
    struct mg_mgr mgr;
    ServerConfig config;
    HttpCallback http_cb;
    WsCallback ws_cb;
    void* user_data;
    struct mg_connection* listener;
};

#define LOG_TIME_BUF 32
static void log_with_time(LogLevel level, const char* file, int line, const char* fmt, ...) {
    if (!g_log_enabled || level > g_log_level) return;
    time_t now = time(NULL);
    struct tm t;
#if defined(_WIN32)
    localtime_s(&t, &now);
#else
    localtime_r(&now, &t);
#endif
    char timebuf[LOG_TIME_BUF];
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", &t);

    static const char* level_str[] = {"NONE", "ERROR", "WARN", "INFO", "DEBUG", "TRACE"};
    FILE* out = (g_log_target == LOG_TARGET_FILE && g_log_file) ? g_log_file : stderr;
    fprintf(out, "[%s][%s][%s:%d] ", timebuf, level_str[level], file, line);

    va_list ap;
    va_start(ap, fmt);
    vfprintf(out, fmt, ap);
    va_end(ap);
    fprintf(out, "\n");
    fflush(out);
}


static void fn(struct mg_connection* c, int ev, void* ev_data) {
    struct Server* server = (struct Server*)c->mgr->userdata;

    if (ev == MG_EV_OPEN) {
        LOG(LOG_LEVEL_DEBUG,"MG_EV_OPEN: %llu", (unsigned long long)c->id);
        if (g_log_level==LOG_LEVEL_DEBUG) c->is_hexdumping = 1;
    } else if (ev == MG_EV_ACCEPT && server->config.use_tls) {
        LOG(LOG_LEVEL_DEBUG,"MG_EV_ACCEPT: %llu", (unsigned long long)c->id);
        struct mg_str cert = mg_file_read(&mg_fs_posix, server->config.cert_file);
        struct mg_str key = mg_file_read(&mg_fs_posix, server->config.key_file);
        if (cert.len == 0 || key.len == 0) {
            LOG(LOG_LEVEL_ERROR,"Failed to read TLS certificate files - cert: %s, key: %s", server->config.cert_file, server->config.key_file);
            c->is_closing = 1;
        }
        struct mg_tls_opts opts = {.cert = cert, .key = key};
        mg_tls_init(c, &opts);
        LOG(LOG_LEVEL_DEBUG,"TLS initialization attempted for connection from %s:%d", c->loc.ip, c->loc.port);
    } else if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message* hm = (struct mg_http_message*)ev_data;
        LOG(LOG_LEVEL_DEBUG,"MG_EV_HTTP_MSG: %llu, URI: %.*s", (unsigned long long)c->id, (int)hm->uri.len, hm->uri.buf);
        // 检查是否为 WebSocket 升级请求
        if (mg_match(hm->uri, mg_str("/ws"), NULL) &&
            server->config.enable_ws && // 仅在启用 WebSocket 时处理
            mg_http_get_header(hm, "Upgrade") != NULL) {
            mg_ws_upgrade(c, hm, NULL);
            LOG(LOG_LEVEL_DEBUG,"Upgraded connection %llu to WebSocket", (unsigned long long)c->id);
            return;
        }
        if (server->http_cb) {
            HttpRequest req = {
                .method = "GET",
                .uri = hm->uri.buf,
                .uri_len = hm->uri.len,
                .headers = NULL,
                .body = NULL,
                .body_len = 0
            };
            HttpResponse res = {0};
            LOG(LOG_LEVEL_DEBUG,"Calling http_cb for conn %llu", (unsigned long long)c->id);
            server->http_cb((ServerHandle*)server, c->id, &req, &res);
            LOG(LOG_LEVEL_DEBUG,"http_cb returned for conn %llu, status_code=%d", (unsigned long long)c->id, res.status_code);
            if (res.body) {
                mg_http_reply(c, res.status_code, res.headers, "%s", res.body);
                LOG(LOG_LEVEL_DEBUG,"Sent HTTP 200 response to conn %llu", (unsigned long long)c->id);
                free((void*)res.body);
            } else {
                struct mg_http_serve_opts opts = {.root_dir = server->config.root_dir};
                mg_http_serve_dir(c, hm, &opts);
                LOG(LOG_LEVEL_DEBUG,"Served static file for conn %llu", (unsigned long long)c->id);
            }
        }
    } else if (ev == MG_EV_TLS_HS) {
        LOG(LOG_LEVEL_DEBUG,"TLS handshake with %s:%d %s", c->loc.ip, c->loc.port, ev_data ? "succeeded" : "failed");
    } else if (ev == MG_EV_CLOSE) {
        LOG(LOG_LEVEL_DEBUG,"Connection closed from %s:%d, reason: %s", c->loc.ip, c->loc.port, ev_data ? (char*)ev_data : "normal");
    } else if (ev == MG_EV_WS_MSG) {
        struct mg_ws_message* wm = (struct mg_ws_message*)ev_data;
        LOG(LOG_LEVEL_DEBUG,"Received WebSocket message from connection %llu (length: %zu)", (unsigned long long)c->id, wm->data.len);
        if (server->ws_cb) {
            WsMessage wm_msg = {
                .data = wm->data.buf,
                .data_len = wm->data.len,
                .binary = (wm->flags & WEBSOCKET_OP_BINARY) ? 1 : 0
            };
            server->ws_cb((ServerHandle*)server, c->id, &wm_msg);
        }
    }
}

MG_SERVER_API ServerHandle* __stdcall Server_Create(void) {
    struct Server* server = (struct Server*)malloc(sizeof(struct Server));
    if (server) {
        if(g_log_level == LOG_LEVEL_DEBUG){
            mg_log_set(MG_LL_DEBUG);
        }else{
            mg_log_set(MG_LL_NONE); // Disable all Mongoose logs
        }
        memset(server, 0, sizeof(struct Server));
        mg_mgr_init(&server->mgr);
        server->mgr.userdata = server;
    }
    return (ServerHandle*)server;
}

MG_SERVER_API void __stdcall Server_Destroy(ServerHandle* h) {
    if (h) {
        struct Server* server = (struct Server*)h;
        mg_mgr_free(&server->mgr);
        free(server);
    }
}

MG_SERVER_API int __stdcall Server_SetConfig(ServerHandle* h, const ServerConfig* c) {
    if (!h || !c) return -1;
    struct Server* server = (struct Server*)h;
    server->config = *c;
    LOG(LOG_LEVEL_DEBUG,"Server config set - port: %d, TLS: %d", server->config.port, server->config.use_tls);
    return 0;
}

MG_SERVER_API int __stdcall Server_SetCallbacks(ServerHandle* h, HttpCallback http_cb, WsCallback ws_cb, void* user_data) {
    if (!h) return -1;
    struct Server* server = (struct Server*)h;
    server->http_cb = http_cb;
    server->ws_cb = ws_cb;
    server->user_data = user_data;
    return 0;
}

MG_SERVER_API int __stdcall Server_Start(ServerHandle* h) {
    if (!h) return -1;
    struct Server* server = (struct Server*)h;
    char addr[64];
    snprintf(addr, sizeof(addr), "%s://0.0.0.0:%d", server->config.use_tls ? "https" : "http", server->config.port);
    LOG(LOG_LEVEL_DEBUG,"Starting server on port %d, TLS: %s", server->config.port, server->config.use_tls ? "enabled" : "disabled");
    server->listener = mg_http_listen(&server->mgr, addr, fn, server);
    if (server->listener) {
        LOG(LOG_LEVEL_DEBUG,"Listener created successfully on %s", addr);
        return 0;
    } else {
        LOG(LOG_LEVEL_DEBUG,"Failed to create listener on %s - check port availability and TLS certificate files", addr);
        return -1;
    }
}

MG_SERVER_API void __stdcall Server_Stop(ServerHandle* h) {
    if (h) {
        struct Server* server = (struct Server*)h;
        struct mg_connection* c;
        LOG(LOG_LEVEL_DEBUG,"Stopping server on port %d", server->config.port);
        for (c = server->mgr.conns; c; c = c->next)
        {
            char addr[64];
            snprintf(addr, sizeof(addr), "%s:%d", c->loc.ip, c->loc.port);
            c->is_closing = 1; // 立即关闭所有连接
            LOG(LOG_LEVEL_DEBUG,"Closing connection %llu from %s", (unsigned long long)c->id, addr);
        }
        mg_mgr_free(&server->mgr); // 释放所有连接
        LOG(LOG_LEVEL_DEBUG,"All connections closed, server stopped");
        // 清理监听器   
        if (server->listener) {
            server->listener->is_closing = 1;
            server->listener = NULL;
        }
    }
}

MG_SERVER_API void __stdcall Server_Poll(ServerHandle* h, int timeout_ms) {
    if (h) {
        struct Server* server = (struct Server*)h;
        mg_mgr_poll(&server->mgr, timeout_ms);
    }
}

MG_SERVER_API int __stdcall Server_WsSendToOne(ServerHandle* h, unsigned long long conn_id, const WsMessage* wm) {
    LOG(LOG_LEVEL_DEBUG, "Server_WsSendToOne called - conn_id: %llu, message: %.*s len: %d, binary: %d",
        conn_id, (int)wm->data_len, wm->data, wm->data_len, wm->binary);
    if (!h || !wm || wm->data_len <= 0) return -1;
    struct Server* server = (struct Server*)h;
    struct mg_connection* c;
    for (c = server->mgr.conns; c; c = c->next) {
        if (c->id == conn_id && c->is_websocket) {
            mg_ws_send(c, wm->data, wm->data_len, wm->binary ? WEBSOCKET_OP_BINARY : WEBSOCKET_OP_TEXT);
            return 0;
        }
    }
    return -1;
}

MG_SERVER_API int __stdcall Server_WsBroadcast(ServerHandle* h, const WsMessage* wm) {
    if (!h || !wm || wm->data_len <= 0) return -1;
    struct Server* server = (struct Server*)h;
    struct mg_connection* c;
    for (c = server->mgr.conns; c; c = c->next) {
        if (c->is_websocket) {
            mg_ws_send(c, wm->data, wm->data_len, wm->binary ? WEBSOCKET_OP_BINARY : WEBSOCKET_OP_TEXT);
        }
    }
    return 0;
}

MG_SERVER_API int __stdcall Server_HttpReply(ServerHandle* h, unsigned long long conn_id, const HttpResponse* res) {
    if (!h || !res) return -1;
    struct Server* server = (struct Server*)h;
    struct mg_connection* c;
    for (c = server->mgr.conns; c; c = c->next) {
        if (c->id == conn_id && !c->is_websocket) {
            mg_http_reply(c, res->status_code, res->headers ? res->headers : "", res->body);
            return 0;
        }
    }
    return -1;
}

MG_SERVER_API int __stdcall Server_HttpServeFile(ServerHandle* h, unsigned long long conn_id, const char* file_path, const char* extra_headers) {
    if (!h || !file_path) return -1; 
    struct Server* server = (struct Server*)h;
    struct mg_connection* c;

    struct mg_http_serve_opts opts = {0};
    opts.extra_headers = extra_headers;
    opts.root_dir = server->config.root_dir ? server->config.root_dir : "."; // 默认根目录为当前目录

    LOG(LOG_LEVEL_DEBUG, 
        "Server_HttpServeFile called - conn_id: %llu, file_path: %s, root_dir: %s, opts.extra_headers: %s", 
        conn_id, file_path, opts.root_dir, opts.extra_headers ? opts.extra_headers : "");
    
    for (c = server->mgr.conns; c; c = c->next)  {
        if (c->id == conn_id && !c->is_websocket) {
            LOG(LOG_LEVEL_DEBUG, 
                "Connection ID: %llu, is_websocket: %d Memory status before serve: %s  c->send.buf: %p (size: %zu)", 
                c->id, c->is_websocket, c->send.buf, c->send.len);
            mg_http_serve_file(c, (struct mg_http_message*)&c->recv, file_path, &opts);
            return 0;
        }
    }
    return -1;
}

MG_SERVER_API void __stdcall Server_SetLogLevel(int enabled, LogLevel level) {
    g_log_enabled = enabled;
    g_log_level = level;
}

MG_SERVER_API void __stdcall Server_SetLogTarget(LogTarget target, const char* filename) {
    if (g_log_file) {
        fclose(g_log_file);
        g_log_file = NULL;
    }
    g_log_target = target;
    if (target == LOG_TARGET_FILE && filename) {
        g_log_file = fopen(filename, "a");
        if (!g_log_file) g_log_target = LOG_TARGET_CONSOLE; // 回退到控制台
    }
}
