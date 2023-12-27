#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <esp_system.h>
#include <lauxlib.h>
#include <nvs.h>
#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>
#include "../event.h"

typedef struct timer {
    int id;
    TimerHandle_t timer;
    struct timer* next;
} timer_ll_t;

static char * label = NULL;
static int nextTimerID = 0;
static timer_ll_t* timer_ll_head = NULL, *timer_ll_tail = NULL;
extern lua_State *paramQueue;

int os_getComputerID(lua_State *L) {lua_pushinteger(L, 0); return 1;}
int os_getComputerLabel(lua_State *L) {
    if (label == NULL) return 0;
    lua_pushstring(L, label);
    return 1;
}

int os_setComputerLabel(lua_State *L) {
    if (label) free(label);
    size_t sz;
    const char * lbl = luaL_checklstring(L, 1, &sz);
    label = heap_caps_malloc(sz + 1, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    strcpy(label, lbl);
    nvs_handle_t handle;
    nvs_open("config", NVS_READWRITE, &handle);
    nvs_set_blob(handle, "label", label, sz);
    nvs_commit(handle);
    nvs_close(handle);
    return 0;
}

int os_queueEvent(lua_State *L) {
    int count = lua_gettop(L);
    const char * name = lua_tostring(L, 1);
    lua_State *param = lua_newthread(paramQueue);
    lua_xmove(L, param, count - 1);
    return 0;
}

int os_clock(lua_State *L) {
    lua_pushinteger(L, clock() / CLOCKS_PER_SEC);
    return 1;
}

static void timer(TimerHandle_t timer) {
    int id = (int)(ptrdiff_t)pvTimerGetTimerID(timer);
    event_t event;
    event.type = EVENT_TYPE_TIMER;
    event.timer.timerID = id;
    if (event_push_isr(&event) == pdTRUE) portYIELD_FROM_ISR();
    timer_ll_t* tm = timer_ll_head, *last = NULL;
    while (tm) {
        if (tm->id == id) {
            if (last) last->next = tm->next;
            if (timer_ll_head == tm) timer_ll_head = tm->next;
            if (timer_ll_tail == tm) timer_ll_tail = last;
            free(tm);
            xTimerDelete(timer, portMAX_DELAY);
            return;
        }
        last = tm;
        tm = tm->next;
    }
}

int os_startTimer(lua_State *L) {
    double time = luaL_checknumber(L, 1);
    if (time < 0) return 0;
    int id = nextTimerID++;
    int ticks = pdMS_TO_TICKS(time * 1000);
    if (ticks <= 0) {
        event_t event;
        event.type = EVENT_TYPE_TIMER;
        event.timer.timerID = id;
        event_push(&event);
    } else {
        TimerHandle_t handle = xTimerCreate("timer", ticks, pdFALSE, (ptrdiff_t)id, timer);
        xTimerStart(handle, portMAX_DELAY);
        timer_ll_t* tm = heap_caps_malloc(sizeof(timer_ll_t), MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
        tm->id = id;
        tm->timer = handle;
        tm->next = NULL;
        if (timer_ll_tail) timer_ll_tail->next = tm;
        else timer_ll_head = timer_ll_tail = tm;
    }
    lua_pushinteger(L, id);
    return 1;
}

int os_cancelTimer(lua_State *L) {
    int id = luaL_checkinteger(L, 1);
    timer_ll_t* tm = timer_ll_head, *last = NULL;
    while (tm) {
        if (tm->id == id) {
            xTimerStop(tm->timer, portMAX_DELAY);
            xTimerDelete(tm->timer, portMAX_DELAY);
            if (last) last->next = tm->next;
            if (timer_ll_head == tm) timer_ll_head = tm->next;
            if (timer_ll_tail == tm) timer_ll_tail = last;
            free(tm);
            return 0;
        }
        last = tm;
        tm = tm->next;
    }
    return 0;
}

int os_time(lua_State *L) {
    const char * type = "ingame";
    if (lua_gettop(L) > 0) type = lua_tostring(L, 1);
    time_t t = time(NULL);
    struct tm rightNow;
    if (strcmp(type, "utc") == 0) rightNow = *gmtime(&t);
    else rightNow = *localtime(&t);
    int hour = rightNow.tm_hour;
    int minute = rightNow.tm_min;
    int second = rightNow.tm_sec;
    lua_pushnumber(L, (double)hour + ((double)minute/60.0) + ((double)second/3600.0));
    return 1;
}

int os_epoch(lua_State *L) {
    const char * type = "ingame";
    if (lua_gettop(L) > 0) type = lua_tostring(L, 1);
    if (strcmp(type, "utc") == 0) {
        lua_pushnumber(L, (long long)time(NULL) * 1000LL);
    } else if (strcmp(type, "local") == 0) {
        time_t t = time(NULL);
        lua_pushnumber(L, (long long)mktime(localtime(&t)) * 1000LL);
    } else {
        time_t t = time(NULL);
        struct tm rightNow = *localtime(&t);
        int hour = rightNow.tm_hour;
        int minute = rightNow.tm_min;
        int second = rightNow.tm_sec;
        double m_time = (double)hour + ((double)minute/60.0) + ((double)second/3600.0);
        double m_day = rightNow.tm_yday;
        lua_pushnumber(L, m_day * 86400000 + (int) (m_time * 3600000.0f));
    }
    return 1;
}

int os_day(lua_State *L) {
    const char * type = "ingame";
    if (lua_gettop(L) > 0) type = lua_tostring(L, 1);
    time_t t = time(NULL);
    if (strcmp(type, "ingame") == 0) {
        struct tm rightNow = *localtime(&t);
        lua_pushinteger(L, rightNow.tm_yday);
        return 1;
    } else if (strcmp(type, "local")) t = mktime(localtime(&t));
    lua_pushinteger(L, t/(60*60*24));
    return 1;
}

int os_setAlarm(lua_State *L) {
    
    return 1;
}

int os_cancelAlarm(lua_State *L) {
    int id = luaL_checkinteger(L, 1);
    timer_ll_t* tm = timer_ll_head, *last = NULL;
    while (tm) {
        if (tm->id == id) {
            xTimerStop(tm->timer, portMAX_DELAY);
            xTimerDelete(tm->timer, portMAX_DELAY);
            if (last) last->next = tm->next;
            if (timer_ll_head == tm) timer_ll_head = tm->next;
            if (timer_ll_tail == tm) timer_ll_tail = last;
            free(tm);
            return 0;
        }
        last = tm;
        tm = tm->next;
    }
    return 0;
}

int os_shutdown(lua_State *L) {
    esp_restart(); // TODO: fix
    return 0;
}

int os_reboot(lua_State *L) {
    esp_restart();
    return 0;
}

const luaL_Reg os_lib[] = {
    {"getComputerID", os_getComputerID},
    {"computerID", os_getComputerID},
    {"getComputerLabel", os_getComputerLabel},
    {"computerLabel", os_getComputerLabel},
    {"setComputerLabel", os_setComputerLabel},
    {"queueEvent", os_queueEvent},
    {"clock", os_clock},
    {"startTimer", os_startTimer},
    {"cancelTimer", os_cancelTimer},
    {"time", os_time},
    {"epoch", os_epoch},
    {"day", os_day},
    {"setAlarm", os_setAlarm},
    {"cancelAlarm", os_cancelAlarm},
    {"shutdown", os_shutdown},
    {"reboot", os_reboot},
    {NULL, NULL}
};
