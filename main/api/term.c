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
    int scroll = luaL_checkinteger(L, 1);
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
    current_colors = (current_colors & 0xF0) | ((int)log2(luaL_checkinteger(L, 1)) & 0x0F);
    return 0;
}

int term_setBackgroundColor(lua_State *L) {
    current_colors = (current_colors & 0x0F) | (((int)log2(luaL_checkinteger(L, 1)) & 0x0F) << 4);
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
    const char * str = luaL_checklstring(L, 1, &len);
    const char * fg = luaL_checklstring(L, 2, &flen);
    const char * bg = luaL_checklstring(L, 3, &blen);
    if (len != flen || flen != blen) {
        lua_pushstring(L, "Arguments must be the same length");
        lua_error(L);
    }
    uint8_t* colors = heap_caps_malloc(len, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    for (int i = 0; i < len; i++) colors[i] = hexch(fg[i]) | (hexch(bg[i]) << 4);
    terminal_blit(cursorX, cursorY, (const uint8_t*)str, colors, len);
    free(colors);
    cursorX += len;
    terminal_cursor(cursorBlink ? current_colors & 0xF : -1, cursorX, cursorY);
    return 0;
}

int term_getPaletteColor(lua_State *L) {
    int color = log2i(luaL_checkinteger(L, 1)) & 0x0F;
    uint8_t v = palette[color];
    lua_pushnumber(L, (v & 0x80 ? 0.5 : 0.0) + (v & 0x40 ? 0.25 : 0.0) + (v & 0x02 ? 0.25 : 0.0));
    lua_pushnumber(L, (v & 0x20 ? 0.5 : 0.0) + (v & 0x10 ? 0.25 : 0.0) + (v & 0x02 ? 0.25 : 0.0));
    lua_pushnumber(L, (v & 0x08 ? 0.5 : 0.0) + (v & 0x04 ? 0.25 : 0.0) + (v & 0x02 ? 0.25 : 0.0));
    return 3;
}

int term_nativePaletteColor(lua_State *L) {
    int color = log2i(luaL_checkinteger(L, 1)) & 0x0F;
    uint8_t v = defaultPalette[color];
    lua_pushnumber(L, (v & 0x80 ? 0.5 : 0.0) + (v & 0x40 ? 0.25 : 0.0) + (v & 0x02 ? 0.25 : 0.0));
    lua_pushnumber(L, (v & 0x20 ? 0.5 : 0.0) + (v & 0x10 ? 0.25 : 0.0) + (v & 0x02 ? 0.25 : 0.0));
    lua_pushnumber(L, (v & 0x08 ? 0.5 : 0.0) + (v & 0x04 ? 0.25 : 0.0) + (v & 0x02 ? 0.25 : 0.0));
    return 3;
}

int term_setPaletteColor(lua_State *L) {
    int color = log2i(luaL_checkinteger(L, 1)) & 0x0F;
    uint8_t rr, gg, bb;
    if (lua_gettop(L) > 2) {
        lua_Number r = luaL_checknumber(L, 2);
        lua_Number g = luaL_checknumber(L, 3);
        lua_Number b = luaL_checknumber(L, 4);
        rr = (int)floor(r * 4 + 0.5);
        gg = (int)floor(g * 4 + 0.5);
        bb = (int)floor(b * 4 + 0.5);
    } else {
        uint32_t rgb = luaL_checkinteger(L, 2);
        rr = (rgb >> 22) & 3;
        gg = (rgb >> 14) & 3;
        bb = (rgb >> 6) & 3;
        if (rgb & 0x200000) rr++;
        if (rgb & 0x002000) gg++;
        if (rgb & 0x000020) bb++;
    }
    uint8_t i = 1;
    if (rr && gg && bb) {rr--; gg--; bb--; i = 3;}
    else if (rr >= 4 || gg >= 4 || bb >= 4) {
        // conflict; both high and low range component required
        // find whether high or low colors are used more
        if (!rr && !gg) bb = 3;
        else if (!gg && !bb) rr = 3;
        else if (!bb && !rr) gg = 3;
        else if (!rr) rr = 1;
        else if (!gg) gg = 1;
        else if (!bb) bb = 1;
        // this should cover all of them?
        if (rr && gg && bb) {rr--; gg--; bb--; i = 3;}
    }
    uint8_t c = ((rr & 3) << 6) | ((gg & 3) << 4) | ((bb & 3) << 2) | i;
    palette[color] = c;
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
