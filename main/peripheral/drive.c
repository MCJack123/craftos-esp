#include <esp_vfs_fat.h>
#include <ff.h>
#include <lauxlib.h>
#include "../driver/storage.h"

static int drive_isDiskPresent(lua_State *L) {
    lua_pushboolean(L, diskMounted);
    return 1;
}

static int drive_getDiskLabel(lua_State *L) {
    if (!diskMounted) {
        lua_pushnil(L);
        return 1;
    }
    char label[12] = {0};
    f_getlabel("0:", label, NULL);
    lua_pushstring(L, label);
    return 1;
}

static int drive_setDiskLabel(lua_State *L) {
    if (!diskMounted) {
        return luaL_error(L, "No disk inserted");
    }
    luaL_checkstring(L, 1);
    lua_settop(L, 1);
    lua_pushliteral(L, "0:");
    lua_insert(L, 1);
    lua_concat(L, 2);
    const char* label = lua_tostring(L, 1);
    f_setlabel(label);
    return 0;
}

static int drive_hasData(lua_State *L) {
    lua_pushboolean(L, diskMounted);
    return 1;
}

static int drive_getMountPath(lua_State *L) {
    if (diskMounted) lua_pushliteral(L, "disk");
    else lua_pushnil(L);
    return 1;
}

static int drive_hasAudio(lua_State *L) {
    lua_pushboolean(L, false);
    return 1;
}

static int drive_getAudioTitle(lua_State *L) {
    lua_pushnil(L);
    return 1;
}

static int drive_playAudio(lua_State *L) {
    return 0;
}

static int drive_stopAudio(lua_State *L) {
    return 0;
}

static int drive_ejectDisk(lua_State *L) {
    return 0;
}

static int drive_getDiskID(lua_State *L) {
    if (!diskMounted) {
        lua_pushnil(L);
        return 1;
    }
    uint32_t id;
    f_getlabel("0:", NULL, &id);
    lua_pushinteger(L, id);
    return 1;
}

const luaL_Reg drive_methods[] = {
    {"isDiskPresent", drive_isDiskPresent},
    {"getDiskLabel", drive_getDiskLabel},
    {"setDiskLabel", drive_setDiskLabel},
    {"hasData", drive_hasData},
    {"getMountPath", drive_getMountPath},
    {"hasAudio", drive_hasAudio},
    {"getAudioTitle", drive_getAudioTitle},
    {"playAudio", drive_playAudio},
    {"stopAudio", drive_stopAudio},
    {"ejectDisk", drive_ejectDisk},
    {"getDiskID", drive_getDiskID},
    {NULL, NULL}
};
