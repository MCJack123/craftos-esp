#include <lua.h>
#include <lauxlib.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include <sys/time.h>
#include "../module/terminal.h"

unsigned short current_colors = 0xF0;
int cursorX = 0, cursorY = 1;
int cursorBlink = 0;

int term_write(lua_State *L) {
    size_t sz;
    const char* text = luaL_checklstring(L, 1, &sz);
    terminal_write(cursorX, cursorY, (const uint8_t*)text, sz, current_colors);
    cursorX += sz;
    terminal_cursor(cursorBlink ? current_colors & 0xF : -1, cursorX, cursorY);
    return 0;
}

int term_scroll(lua_State *L) {
    int scroll = lua_tointeger(L, 1);
    terminal_scroll(scroll, current_colors);
    return 0;
}

int term_setCursorPos(lua_State *L) {
    cursorX = luaL_checkinteger(L, 1) - 1;
    cursorY = luaL_checkinteger(L, 2) - 1;
    terminal_cursor(cursorBlink ? current_colors & 0xF : -1, cursorX, cursorY);
    return 0;
}

int term_getCursorBlink(lua_State *L) {
    lua_pushboolean(L, cursorBlink);
    return 1;
}

int term_setCursorBlink(lua_State *L) {
    cursorBlink = lua_toboolean(L, 1);
    terminal_cursor(cursorBlink ? current_colors & 0xF : -1, cursorX, cursorY);
    return 0;
}

int term_getCursorPos(lua_State *L) {
    lua_pushinteger(L, cursorX+1);
    lua_pushinteger(L, cursorY+1);
    return 2;
}

int term_getSize(lua_State *L) {
    lua_pushinteger(L, TERM_WIDTH);
    lua_pushinteger(L, TERM_HEIGHT);
    return 2;
}

int term_clear(lua_State *L) {
    terminal_clear(-1, current_colors);
    return 0;
}

int term_clearLine(lua_State *L) {
    if (cursorY < 0 || cursorY >= TERM_HEIGHT) return 0;
    terminal_clear(cursorY, current_colors);
    return 0;
}

static int log2i(unsigned int n) {
    if (n == 0) return 0;
    int i = 0;
    while (n) {i++; n >>= 1;}
    return i - 1;
}

int term_setTextColor(lua_State *L) {
    current_colors = (current_colors & 0xF0) | ((int)log2(lua_tointeger(L, 1)) & 0x0F);
    return 0;
}

int term_setBackgroundColor(lua_State *L) {
    current_colors = (current_colors & 0x0F) | (((int)log2(lua_tointeger(L, 1)) & 0x0F) << 4);
    return 0;
}

int term_isColor(lua_State *L) {
    lua_pushboolean(L, 1);
    return 1;
}

int term_getTextColor(lua_State *L) {
    lua_pushinteger(L, 1 << (current_colors & 0x0F));
    return 1;
}

int term_getBackgroundColor(lua_State *L) {
    lua_pushinteger(L, 1 << (current_colors >> 4));
    return 1;
}

int hexch(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    else if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    else return 0;
}

int term_blit(lua_State *L) {
    size_t len, blen, flen;
    const char * str = lua_tolstring(L, 1, &len);
    const char * fg = lua_tolstring(L, 2, &flen);
    const char * bg = lua_tolstring(L, 3, &blen);
    if (len != flen || flen != blen) {
        lua_pushstring(L, "Arguments must be the same length");
        lua_error(L);
    }
    uint8_t* colors = malloc(len);
    for (int i = 0; i < len; i++) colors[i] = hexch(fg[i]) | (hexch(bg[i]) << 4);
    terminal_blit(cursorX, cursorY, (const uint8_t*)str, colors, len);
    free(colors);
    cursorX += len;
    terminal_cursor(cursorBlink ? current_colors & 0xF : -1, cursorX, cursorY);
    return 0;
}

int term_getPaletteColor(lua_State *L) {
    int color = log2(lua_tointeger(L, 1));
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    return 3;
}

int term_nativePaletteColor(lua_State *L) {
    int color = log2(lua_tointeger(L, 1));
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    return 3;
}

int term_setPaletteColor(lua_State *L) {
    return 0;
}

const luaL_Reg term_lib[] = {
    {"write", term_write},
    {"scroll", term_scroll},
    {"getCursorPos", term_getCursorPos},
    {"setCursorPos", term_setCursorPos},
    {"getCursorBlink", term_getCursorBlink},
    {"setCursorBlink", term_setCursorBlink},
    {"getSize", term_getSize},
    {"clear", term_clear},
    {"clearLine", term_clearLine},
    {"setTextColour", term_setTextColor},
    {"setTextColor", term_setTextColor},
    {"setBackgroundColour", term_setBackgroundColor},
    {"setBackgroundColor", term_setBackgroundColor},
    {"isColour", term_isColor},
    {"isColor", term_isColor},
    {"getTextColour", term_getTextColor},
    {"getTextColor", term_getTextColor},
    {"getBackgroundColour", term_getBackgroundColor},
    {"getBackgroundColor", term_getBackgroundColor},
    {"blit", term_blit},
    {"getPaletteColor", term_getPaletteColor},
    {"getPaletteColour", term_getPaletteColor},
    {"setPaletteColor", term_setPaletteColor},
    {"setPaletteColour", term_setPaletteColor},
    {"nativePaletteColor", term_nativePaletteColor},
    {"nativePaletteColour", term_nativePaletteColor},
    {NULL, NULL}
};
