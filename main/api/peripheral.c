#include <string.h>
#include <lauxlib.h>

extern const luaL_Reg drive_methods[];
extern const luaL_Reg modem_methods[];
extern const luaL_Reg speaker_methods[];

static int peripheral_getNames(lua_State *L) {
    lua_createtable(L, 3, 1);
    lua_pushliteral(L, "left");
    lua_rawseti(L, -2, 1);
    lua_pushliteral(L, "right");
    lua_rawseti(L, -2, 2);
    lua_pushliteral(L, "back");
    lua_rawseti(L, -2, 3);
    return 1;
}

static int peripheral_isPresent(lua_State *L) {
    const char* side = luaL_checkstring(L, 1);
    lua_pushboolean(L, strcmp(side, "left") == 0 || strcmp(side, "right") == 0 || strcmp(side, "back") == 0);
    return 1;
}

static int peripheral_getType(lua_State *L) {
    const char* side = luaL_checkstring(L, 1);
    if (strcmp(side, "left") == 0) lua_pushliteral(L, "speaker");
    else if (strcmp(side, "right") == 0) lua_pushliteral(L, "drive");
    else if (strcmp(side, "back") == 0) lua_pushliteral(L, "modem");
    else lua_pushnil(L);
    return 1;
}

static int peripheral_hasType(lua_State *L) {
    const char* side = luaL_checkstring(L, 1);
    const char* type = luaL_checkstring(L, 2);
    lua_pushboolean(L,
        (strcmp(side, "left") == 0 && strcmp(type, "speaker") == 0) ||
        (strcmp(side, "right") == 0 && strcmp(type, "drive") == 0) ||
        (strcmp(side, "back") == 0 && strcmp(type, "modem") == 0)
    );
    return 1;
}

static int peripheral_getMethods(lua_State *L) {
    const luaL_Reg* reg;
    const char* side = luaL_checkstring(L, 1);
    if (strcmp(side, "left") == 0) reg = speaker_methods;
    else if (strcmp(side, "right") == 0) reg = drive_methods;
    else if (strcmp(side, "back") == 0) reg = modem_methods;
    else return luaL_error(L, "No such peripheral");
    lua_newtable(L);
    for (int i = 1; reg->name; i++, reg++) {
        lua_pushstring(L, reg->name);
        lua_rawseti(L, -2, i);
    }
    return 1;
}

static int peripheral_call(lua_State *L) {
    const luaL_Reg* reg;
    const char* side = luaL_checkstring(L, 1);
    const char* method = luaL_checkstring(L, 2);
    if (strcmp(side, "left") == 0) reg = speaker_methods;
    else if (strcmp(side, "right") == 0) reg = drive_methods;
    else if (strcmp(side, "back") == 0) reg = modem_methods;
    else return luaL_error(L, "No such peripheral");
    for (; reg->name; reg++) {
        if (strcmp(reg->name, method) == 0) {
            lua_remove(L, 1);
            lua_remove(L, 1);
            return reg->func(L);
        }
    }
    return luaL_error(L, "No such method");
}

const luaL_Reg peripheral_lib[] = {
    {"getNames", peripheral_getNames},
    {"isPresent", peripheral_isPresent},
    {"getType", peripheral_getType},
    {"hasType", peripheral_hasType},
    {"getMethods", peripheral_getMethods},
    {"call", peripheral_call},
    {NULL, NULL}
};
