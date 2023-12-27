#include <string.h>
#include <lauxlib.h>
#include "../module/redstone.h"

static const char* side_names[] = {"left", "right", "top", "bottom", "front", "back"};
static int output[6];

static enum redstone_channel get_channel(const char* name) {
    for (int i = 0; i < 6; i++) if (strcmp(name, side_names[i]) == 0) return i;
    return -1;
}

static int rs_getSides(lua_State *L) {
    lua_createtable(L, 6, 0);
    for (int i = 0; i < 6; i++) {
        lua_pushstring(L, side_names[i]);
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

static int rs_getInput(lua_State *L) {
    enum redstone_channel channel = get_channel(luaL_checkstring(L, 1));
    if (channel == -1) luaL_error(L, "Invalid side");
    lua_pushboolean(L, redstone_getInput(channel));
    return 1;
}

static int rs_getOutput(lua_State *L) {
    enum redstone_channel channel = get_channel(luaL_checkstring(L, 1));
    if (channel == -1) luaL_error(L, "Invalid side");
    lua_pushboolean(L, output[channel]);
    return 1;
}

static int rs_setOutput(lua_State *L) {
    enum redstone_channel channel = get_channel(luaL_checkstring(L, 1));
    if (channel == -1) luaL_error(L, "Invalid side");
    redstone_setOutput(channel, lua_toboolean(L, 2));
    output[channel] = lua_toboolean(L, 2) ? 15 : 0;
    return 0;
}

static int rs_getAnalogInput(lua_State *L) {
    enum redstone_channel channel = get_channel(luaL_checkstring(L, 1));
    if (channel == -1) luaL_error(L, "Invalid side");
    lua_pushnumber(L, redstone_getAnalogInput(channel));
    return 1;
}

static int rs_getAnalogOutput(lua_State *L) {
    enum redstone_channel channel = get_channel(luaL_checkstring(L, 1));
    if (channel == -1) luaL_error(L, "Invalid side");
    lua_pushnumber(L, output[channel]);
    return 1;
}

static int rs_setAnalogOutput(lua_State *L) {
    enum redstone_channel channel = get_channel(luaL_checkstring(L, 1));
    if (channel == -1) luaL_error(L, "Invalid side");
    int num = luaL_checkinteger(L, 2);
    if (num < 0) num = 0;
    else if (num > 15) num = 15;
    redstone_setAnalogOutput(channel, num);
    output[channel] = num;
    return 0;
}

static int rs_0(lua_State *L) {
    enum redstone_channel channel = get_channel(luaL_checkstring(L, 1));
    if (channel == -1) luaL_error(L, "Invalid side");
    lua_pushnumber(L, 0);
    return 1;
}

static int rs_false(lua_State *L) {
    enum redstone_channel channel = get_channel(luaL_checkstring(L, 1));
    if (channel == -1) luaL_error(L, "Invalid side");
    lua_pushboolean(L, false);
    return 1;
}

static int rs_none(lua_State *L) {
    enum redstone_channel channel = get_channel(luaL_checkstring(L, 1));
    if (channel == -1) luaL_error(L, "Invalid side");
    return 0;
}

const luaL_Reg rs_lib[] = {
    {"getSides", rs_getSides},
    {"getInput", rs_getInput},
    {"setOutput", rs_setOutput},
    {"getOutput", rs_getOutput},
    {"getAnalogInput", rs_getAnalogInput},
    {"setAnalogOutput", rs_setAnalogOutput},
    {"getAnalogOutput", rs_getAnalogOutput},
    {"getAnalogueInput", rs_getAnalogInput},
    {"setAnalogueOutput", rs_setAnalogOutput},
    {"getAnalogueOutput", rs_getAnalogOutput},
    {"getBundledInput", rs_0},
    {"getBundledOutput", rs_0},
    {"setBundledOutput", rs_none},
    {"testBundledInput", rs_false},
    {NULL, NULL}
};
