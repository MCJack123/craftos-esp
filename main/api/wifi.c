#include <string.h>
#include <lua.h>
#include <lauxlib.h>
#include <esp_event.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include "../driver/wifi.h"
#include "../event.h"

ESP_EVENT_DEFINE_BASE(WIFI_API_EVENT);

enum {
    WIFI_TASK_SCAN,
    WIFI_TASK_CONNECT,
    WIFI_TASK_DISCONNECT
};

typedef struct {
    uint8_t type;
    uint8_t has_password;
    char network[33];
    char password[33];
} wifi_task_t;

static bool inited = false;

static void wifi_process(void* handler_arg, esp_event_base_t base, int32_t id, void* data) {
    switch (id) {
        case WIFI_TASK_SCAN: {
            event_t event;
            event.type = EVENT_TYPE_WIFI_SCAN;
            event.wifi.networks = wifi_scan(&event.wifi.networkCount);
            event_push(&event);
            break;
        } case WIFI_TASK_CONNECT: {
            event_t event;
            wifi_task_t* task = data;
            if (wifi_connect(task->network, task->has_password ? task->password : NULL, NULL) == ESP_OK)
                event.type = EVENT_TYPE_WIFI_CONNECT;
            else event.type = EVENT_TYPE_WIFI_DISCONNECT;
            event_push(&event);
            break;
        } case WIFI_TASK_DISCONNECT: {
            wifi_disconnect();
            break;
        }
    }
}

static void check_inited(void) {
    if (!inited) {
        esp_event_handler_register_with(common_event_loop, WIFI_API_EVENT, WIFI_TASK_SCAN, wifi_process, NULL);
        esp_event_handler_register_with(common_event_loop, WIFI_API_EVENT, WIFI_TASK_CONNECT, wifi_process, NULL);
        esp_event_handler_register_with(common_event_loop, WIFI_API_EVENT, WIFI_TASK_DISCONNECT, wifi_process, NULL);
        inited = true;
    }
}

static int _wifi_scan(lua_State *L) {
    check_inited();
    lua_pushboolean(L, esp_event_post_to(common_event_loop, WIFI_API_EVENT, WIFI_TASK_SCAN, NULL, 0, 0) == ESP_OK);
    return 1;
}

static int _wifi_status(lua_State *L) {
    check_inited();
    wifi_status_t status = wifi_status();
    if (status.connected) {
        lua_createtable(L, 0, 4);
        lua_pushstring(L, status.ssid);
        lua_setfield(L, -2, "network");
        lua_pushnumber(L, status.bars);
        lua_setfield(L, -2, "bars");
        lua_pushliteral(L, "802.11");
        if (status.mode == 'x') lua_pushliteral(L, "ax");
        else lua_pushlstring(L, &status.mode, 1);
        lua_concat(L, 2);
        lua_setfield(L, -2, "mode");
        lua_pushstring(L, wifi_security_names[status.security]);
        lua_setfield(L, -2, "security");
    } else lua_pushnil(L);
    return 1;
}

static int _wifi_connect(lua_State *L) {
    const char* ssid = luaL_checkstring(L, 1);
    const char* password = luaL_optstring(L, 2, NULL);
    check_inited();
    wifi_task_t task;
    task.type = WIFI_TASK_CONNECT;
    strncpy(task.network, ssid, 32);
    if (password) strncpy(task.password, password, 32);
    task.has_password = password != NULL;
    lua_pushboolean(L, esp_event_post_to(common_event_loop, WIFI_API_EVENT, WIFI_TASK_CONNECT, &task, sizeof(task), 0) == ESP_OK);
    return 1;
}

static int _wifi_disconnect(lua_State *L) {
    check_inited();
    lua_pushboolean(L, esp_event_post_to(common_event_loop, WIFI_API_EVENT, WIFI_TASK_DISCONNECT, NULL, 0, 0) == ESP_OK);
    return 1;
}

const luaL_Reg wifi_lib[] = {
    {"status", _wifi_status},
    {"scan", _wifi_scan},
    {"connect", _wifi_connect},
    {"disconnect", _wifi_disconnect},
    {NULL, NULL}
};
