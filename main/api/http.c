#include <lua.h>
#include <lauxlib.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_tls_errors.h>
#include <esp_transport.h>
#include "../event.h"

ESP_EVENT_DEFINE_BASE(HTTP_PROCESS_EVENT);

typedef struct http_header {
    char* key;
    char* value;
    struct http_header* next;
} http_header_t;

typedef struct {
    esp_http_client_handle_t handle;
    char* url;
    char* data;
    size_t dataSize;
    size_t dataLength;
    size_t readPos;
    http_header_t* headers;
    http_header_t* headers_tail;
} http_handle_t;

static bool installed = false;

static int http_handle_read(lua_State *L) {
    http_handle_t** handle = lua_touserdata(L, lua_upvalueindex(1));
    if (*handle == NULL) luaL_error(L, "attempt to use a closed file");
    if ((*handle)->readPos >= (*handle)->dataLength) return 0;
    if (lua_isnumber(L, 1)) {
        int n = lua_tointeger(L, 1);
        if (n < 0) luaL_error(L, "bad argument #1 (value out of range)");
        else if (n == 0) {
            lua_pushliteral(L, "");
            return 1;
        }
        if (n > (*handle)->dataLength - (*handle)->readPos) n = (*handle)->dataLength - (*handle)->readPos;
        lua_pushlstring(L, (*handle)->data + (*handle)->readPos, n);
        (*handle)->readPos += n;
        return 1;
    } else {
        lua_pushinteger(L, (*handle)->data[(*handle)->readPos++]);
        return 1;
    }
}

static int http_handle_readLine(lua_State *L) {
    http_handle_t** handle = lua_touserdata(L, lua_upvalueindex(1));
    if (*handle == NULL) luaL_error(L, "attempt to use a closed file");
    if ((*handle)->readPos >= (*handle)->dataLength) return 0;
    size_t end = (*handle)->readPos;
    while (end < (*handle)->dataLength && (*handle)->data[end] != '\n') end++;
    lua_pushlstring(L, (*handle)->data + (*handle)->readPos, end - (*handle)->readPos);
    (*handle)->readPos = end + 1;
    return 1;
}

static int http_handle_readAll(lua_State *L) {
    http_handle_t** handle = lua_touserdata(L, lua_upvalueindex(1));
    if (*handle == NULL) luaL_error(L, "attempt to use a closed file");
    if ((*handle)->readPos >= (*handle)->dataLength) return 0;
    lua_pushlstring(L, (*handle)->data + (*handle)->readPos, (*handle)->dataLength - (*handle)->readPos);
    (*handle)->readPos = (*handle)->dataLength;
    return 1;
}

static int http_handle_seek(lua_State *L) {
    http_handle_t** handle = lua_touserdata(L, lua_upvalueindex(1));
    if (*handle == NULL) luaL_error(L, "attempt to use a closed file");
    const char* whence = luaL_optstring(L, 1, "cur");
    int offset = luaL_optinteger(L, 2, 0);
    if (strcmp(whence, "set") == 0) {
        if (offset < 0) offset = 0;
        (*handle)->readPos = offset;
    } else if (strcmp(whence, "cur") == 0) {
        if (-offset > (*handle)->readPos) (*handle)->readPos = 0;
        else (*handle)->readPos += offset;
    } else if (strcmp(whence, "end") == 0) {
        if (offset > (*handle)->dataLength) offset = (*handle)->dataLength;
        (*handle)->readPos = (*handle)->dataLength - offset;
    } else luaL_error(L, "Invalid whence");
    lua_pushinteger(L, (*handle)->readPos);
    return 1;
}

static int http_handle_getResponseCode(lua_State *L) {
    http_handle_t** handle = lua_touserdata(L, lua_upvalueindex(1));
    if (*handle == NULL) luaL_error(L, "attempt to use a closed file");
    lua_pushinteger(L, esp_http_client_get_status_code((*handle)->handle));
    return 1;
}

static int http_handle_getResponseHeaders(lua_State *L) {
    http_handle_t** handle = lua_touserdata(L, lua_upvalueindex(1));
    if (*handle == NULL) luaL_error(L, "attempt to use a closed file");
    lua_newtable(L);
    http_header_t* header = (*handle)->headers;
    while (header) {
        lua_pushstring(L, header->key);
        lua_pushstring(L, header->value);
        lua_settable(L, -3);
        header = header->next;
    }
    return 1;
}

static void http_handle_free(http_handle_t* handle) {
    esp_http_client_cleanup(handle->handle);
    if (handle->data != NULL) free(handle->data);
    while (handle->headers) {
        http_handle_t* next = handle->headers->next;
        free(handle->headers);
        handle->headers = next;
    }
    free(handle);
}

static int http_handle_close(lua_State *L) {
    http_handle_t** handle = lua_touserdata(L, lua_upvalueindex(1));
    if (*handle != NULL) http_handle_free(*handle);
    *handle = NULL;
    return 0;
}

static int http_handle_gc(lua_State *L) {
    http_handle_t** handle = lua_touserdata(L, 1);
    if (*handle != NULL) http_handle_free(*handle);
    *handle = NULL;
    return 0;
}

static const luaL_Reg http_handle[] = {
    {"read", http_handle_read},
    {"readLine", http_handle_readLine},
    {"readAll", http_handle_readAll},
    {"seek", http_handle_seek},
    {"getResponseCode", http_handle_getResponseCode},
    {"getResponseHeaders", http_handle_getResponseHeaders},
    {"close", http_handle_close},
    {NULL, NULL}
};

static void http_handle_push(lua_State *L, void* arg) {
    lua_createtable(L, 0, 7);
    http_handle_t** handle_ptr = lua_newuserdata(L, sizeof(http_handle_t*));
    *handle_ptr = arg;
    lua_createtable(L, 0, 1);
    lua_pushcfunction(L, http_handle_gc);
    lua_setfield(L, -2, "__gc");
    lua_setmetatable(L, -2);
    luaL_setfuncs(L, http_handle, 1);
}

static esp_err_t http_event(esp_http_client_event_t* event) {
    http_handle_t* handle = event->user_data;
    switch (event->event_id) {
        case HTTP_EVENT_ERROR: {
            esp_tls_error_handle_t err = event->data;
            event_t ev;
            ev.type = EVENT_TYPE_HTTP_FAILURE;
            ev.http.url = handle->url;
            ev.http.err = esp_err_to_name(err->last_error);
            ev.http.handle_fn = NULL;
            event_push(&ev);
            http_handle_free(handle);
            break;
        } case HTTP_EVENT_ON_HEADER: {
            http_header_t* header = malloc(sizeof(http_header_t));
            header->key = malloc(strlen(event->header_key)+1);
            header->value = malloc(strlen(event->header_value)+1);
            strcpy(header->key, event->header_key);
            strcpy(header->value, event->header_value);
            header->next = NULL;
            if (handle->headers == NULL) handle->headers = header;
            else handle->headers_tail->next = header;
            handle->headers_tail = header;
            break;
        } case HTTP_EVENT_ON_DATA: {
            if (handle->data == NULL) {
                int64_t sz = esp_http_client_get_content_length(handle->handle);
                if (sz < 0) sz = event->data_len + 1024;
                handle->data = heap_caps_malloc(sz, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
                handle->dataLength = 0;
                handle->dataSize = sz;
            } else if (handle->dataLength + event->data_len > handle->dataSize) {
                handle->data = heap_caps_realloc(handle->data, handle->dataSize + event->data_len + 1024, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
                handle->dataSize += event->data_len + 1024;
            }
            memcpy(handle->data + handle->dataLength, event->data, event->data_len);
            handle->dataLength += event->data_len;
            break;
        } default: break;
    }
    return ESP_OK;
}

static void http_process(void* handler_arg, esp_event_base_t base, int32_t id, void* data) {
    http_handle_t* handle = *(http_handle_t**)data;
    esp_err_t err;
    do {err = esp_http_client_perform(handle->handle);}
    while (err == ESP_ERR_HTTP_EAGAIN);
    handle->data = heap_caps_realloc(handle->data, handle->dataLength, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    handle->dataSize = handle->dataLength;
    event_t ev;
    ev.type = EVENT_TYPE_HTTP_SUCCESS;
    ev.http.url = handle->url;
    ev.http.err = NULL;
    ev.http.handle_fn = http_handle_push;
    ev.http.handle_arg = handle;
    event_push(&ev);
    handle->url = NULL;
}

static int http_request(lua_State *L) {
    if (!lua_istable(L, 1)) {
        lua_settop(L, 4);
        lua_createtable(L, 0, 4);
        lua_pushvalue(L, 1);
        lua_setfield(L, -2, "url");
        lua_pushvalue(L, 2);
        lua_setfield(L, -2, "body");
        lua_pushvalue(L, 3);
        lua_setfield(L, -2, "headers");
        lua_pushvalue(L, 4);
        lua_setfield(L, -2, "binary");
        lua_insert(L, 1);
        lua_settop(L, 1);
    }
    esp_http_client_config_t config = {};
    lua_getfield(L, 1, "url");
    size_t sz;
    const char* url = luaL_checklstring(L, -1, &sz);
    config.url = malloc(sz+1);
    memcpy((char*)config.url, url, sz);
    ((char*)config.url)[sz] = 0;
    lua_pop(L, 1);
    config.method = HTTP_METHOD_GET;
    lua_getfield(L, 1, "method");
    if (lua_isstring(L, -1)) {
        const char* method = lua_tostring(L, -1);
        if (strcasecmp(method, "GET") == 0) config.method = HTTP_METHOD_GET;
        else if (strcasecmp(method, "POST") == 0) config.method = HTTP_METHOD_POST;
        else if (strcasecmp(method, "PUT") == 0) config.method = HTTP_METHOD_PUT;
        else if (strcasecmp(method, "DELETE") == 0) config.method = HTTP_METHOD_DELETE;
        else if (strcasecmp(method, "HEAD") == 0) config.method = HTTP_METHOD_HEAD;
        else if (strcasecmp(method, "OPTIONS") == 0) config.method = HTTP_METHOD_OPTIONS;
        else if (strcasecmp(method, "PATCH") == 0) config.method = HTTP_METHOD_PATCH;
    }
    lua_pop(L, 1);
    config.disable_auto_redirect = false;
    config.max_redirection_count = 16;
    lua_getfield(L, 1, "redirect");
    if (lua_isboolean(L, -1) && !lua_toboolean(L, -1)) config.disable_auto_redirect = true;
    lua_pop(L, 1);
    config.timeout_ms = 10000;
    lua_getfield(L, 1, "timeout");
    if (lua_isnumber(L, -1)) config.timeout_ms = lua_tointeger(L, -1) * 1000;
    lua_pop(L, 1);
    config.buffer_size = 4096;
    config.buffer_size_tx = 2048;
    config.is_async = false;
    config.auth_type = HTTP_AUTH_TYPE_NONE;
    config.use_global_ca_store = true;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.common_name = NULL;
    config.keep_alive_enable = false;
    config.if_name = NULL;
    config.ds_data = NULL;
    config.event_handler = http_event;
    config.user_agent = "computercraft/1.109.2 CraftOS-ESP/1.0";

    http_handle_t* handle = malloc(sizeof(http_handle_t));
    config.user_data = handle;
    handle->handle = esp_http_client_init(&config);
    handle->url = (char*)config.url;
    handle->dataSize = handle->dataLength = handle->readPos = 0;
    handle->data = handle->headers = handle->headers_tail = NULL;
    lua_getfield(L, 1, "headers");
    if (lua_istable(L, -1)) {
        lua_pushnil(L);
        while (lua_next(L, -2)) {
            const char* key = lua_tostring(L, -2);
            const char* value = lua_tostring(L, -1);
            esp_http_client_set_header(handle->handle, key, value);
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    lua_getfield(L, 1, "body");
    if (lua_isstring(L, -1)) {
        size_t sz;
        const char* data = lua_tolstring(L, -1, &sz); // I sure hope this doesn't get GC'd!!!
        esp_http_client_set_post_field(handle->handle, data, sz);
    }
    lua_pop(L, 1);
    if (!installed) {
        esp_event_handler_register_with(common_event_loop, HTTP_PROCESS_EVENT, 0, http_process, NULL);
        installed = true;
    }
    esp_event_post_to(common_event_loop, HTTP_PROCESS_EVENT, 0, &handle, sizeof(handle), portMAX_DELAY);
    lua_pushboolean(L, true);
    return 1;
}

static void http_checkURL_cb(lua_State *L, void* arg) {
    lua_pushboolean(L, arg);
}

static int http_checkURL(lua_State *L) {
    size_t sz;
    const char* str = luaL_checklstring(L, 1, &sz);
    ptrdiff_t ok = PTRDIFF_MAX;
    if (strncmp(str, "http://", 7) != 0 && strncmp(str, "https://", 8) != 0) ok = 0;
    event_t event;
    event.type = EVENT_TYPE_HTTP_CHECK;
    event.http.url = malloc(sz + 1);
    memcpy(event.http.url, str, sz);
    event.http.url[sz] = 0;
    event.http.err = NULL;
    event.http.handle_fn = http_checkURL_cb;
    event.http.handle_arg = (void*)ok;
    event_push(&event);
    lua_pushboolean(L, true);
    return 1;
}

static int http_websocket(lua_State *L) {
    return 0;
}

const luaL_Reg http_lib[] = {
    {"request", http_request},
    {"checkURL", http_checkURL},
    {"websocket", http_websocket},
    {NULL, NULL}
};
