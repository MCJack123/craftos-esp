#include <errno.h>
#include <lauxlib.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <esp_heap_caps.h>
#include "fs_handle.h"

extern size_t free_space_cache[2];

int fs_handle_close(lua_State *L) {
    if (*(FILE**)lua_touserdata(L, lua_upvalueindex(1)) == NULL)
        return luaL_error(L, "attempt to use a closed file");
    fclose(*(FILE**)lua_touserdata(L, lua_upvalueindex(1)));
    *(FILE**)lua_touserdata(L, lua_upvalueindex(1)) = NULL;
    free_space_cache[0] = free_space_cache[1] = 0;
    return 0;
}

int fs_handle_readAll(lua_State *L) {
    FILE * fp = *(FILE**)lua_touserdata(L, lua_upvalueindex(1));
    if (fp == NULL) luaL_error(L, "attempt to use a closed file");
    if (feof(fp)) return 0;
    const long pos = ftell(fp);
    fseek(fp, 0, SEEK_END);
    const long size = ftell(fp) - pos;
    char * retval = heap_caps_malloc(size + 1, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    memset(retval, 0, size + 1);
    fseek(fp, pos, SEEK_SET);
    int i;
    for (i = 0; !feof(fp) && i < size; i++) {
        int c = fgetc(fp);
        if (c == EOF && feof(fp)) c = '\n';
        if (c == '\n' && (i > 0 && retval[i-1] == '\r')) retval[--i] = '\n';
        else if (c > 127 || c < 0) retval[i] = '?';
        else retval[i] = (char)c;
    }
    lua_pushlstring(L, retval, i);
    heap_caps_free(retval);
    return 1;
}

int fs_handle_readLine(lua_State *L) {
    FILE * fp = *(FILE**)lua_touserdata(L, lua_upvalueindex(1));
    if (fp == NULL) luaL_error(L, "attempt to use a closed file");
    if (feof(fp) || ferror(fp)) return 0;
    char* retval = (char*)heap_caps_malloc(256, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    retval[0] = 0;
    for (unsigned i = 0; 1; i += 255) {
        if (fgets(&retval[i], 256, fp) == NULL || feof(fp)) break;
        int found = 0;
        for (unsigned j = 0; j < 256; j++) if (retval[i+j] == '\n') {found = 1; break;}
        if (found) break;
        char * retvaln = (char*)heap_caps_realloc(retval, i + 511, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
        if (retvaln == NULL) {
            heap_caps_free(retval);
            return luaL_error(L, "failed to allocate memory");
        }
        retval = retvaln;
    }
    size_t len = strlen(retval) - (strlen(retval) > 0 && retval[strlen(retval)-1] == '\n' && !lua_toboolean(L, 1));
    if (len == 0 && feof(fp)) {heap_caps_free(retval); return 0;}
    if (len > 0 && retval[len-1] == '\r') retval[--len] = '\0';
    lua_pushlstring(L, retval, len);
    free(retval);
    return 1;
}

int fs_handle_readChar(lua_State *L) {
    FILE * fp = *(FILE**)lua_touserdata(L, lua_upvalueindex(1));
    if (fp == NULL) luaL_error(L, "attempt to use a closed file");
    if (feof(fp)) return 0;
    char * retval = heap_caps_malloc(lua_isnumber(L, 1) ? lua_tointeger(L, 1) : 1, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    char * cur = retval;
    for (int i = 0; i < (lua_isnumber(L, 1) ? lua_tointeger(L, 1) : 1) && !feof(fp); i++) {
        uint32_t codepoint;
        const int c = fgetc(fp);
        if (c == EOF) break;
        else if (c < 0) {
            if (c & 64) {
                const int c2 = fgetc(fp);
                if (c2 == EOF) {*cur++ = '?'; break;}
                else if (c2 >= 0 || c2 & 64) codepoint = 0x80000000U;
                else if (c & 32) {
                    const int c3 = fgetc(fp);
                    if (c3 == EOF) {*cur++ = '?'; break;}
                    else if (c3 >= 0 || c3 & 64) codepoint = 0x80000000U;
                    else if (c & 16) {
                        if (c & 8) codepoint = 0x80000000U;
                        else {
                            const int c4 = fgetc(fp);
                            if (c4 == EOF) {*cur++ = '?'; break;}
                            else if (c4 >= 0 || c4 & 64) codepoint = 0x80000000U;
                            else codepoint = ((c & 0x7) << 18) | ((c2 & 0x3F) << 12) | ((c3 & 0x3F) << 6) | (c4 & 0x3F);
                        }
                    } else codepoint = ((c & 0xF) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
                } else codepoint = ((c & 0x1F) << 6) | (c2 & 0x3F);
            } else codepoint = 0x80000000U;
        } else codepoint = (unsigned char)c;
        if (codepoint > 255) *cur++ = '?';
        else {
            if (codepoint == '\r') {
                const int nextc = fgetc(fp);
                if (nextc == '\n') codepoint = nextc;
                else ungetc(nextc, fp);
            }
            *cur++ = (unsigned char)codepoint;
        }
    }
    lua_pushlstring(L, retval, cur - retval);
    heap_caps_free(retval);
    return 1;
}

int fs_handle_readByte(lua_State *L) {
    FILE * fp = *(FILE**)lua_touserdata(L, lua_upvalueindex(1));
    if (fp == NULL) luaL_error(L, "attempt to use a closed file");
    if (feof(fp)) return 0;
    if (lua_isnumber(L, 1)) {
        const size_t s = lua_tointeger(L, 1);
        if (s == 0) {
            const int c = fgetc(fp);
            if (c == EOF || feof(fp)) return 0;
            ungetc(c, fp);
            lua_pushstring(L, "");
            return 1;
        }
        char* retval = heap_caps_malloc(s, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
        const size_t actual = fread(retval, 1, s, fp);
        if (actual == 0 && feof(fp)) {free(retval); return 0;}
        lua_pushlstring(L, retval, actual);
        heap_caps_free(retval);
    } else {
        const int retval = fgetc(fp);
        if (retval == EOF || feof(fp)) return 0;
        lua_pushinteger(L, (unsigned char)retval);
    }
    return 1;
}

int fs_handle_readAllByte(lua_State *L) {
    FILE * fp = *(FILE**)lua_touserdata(L, lua_upvalueindex(1));
    if (fp == NULL) luaL_error(L, "attempt to use a closed file");
    if (feof(fp)) return 0;
    size_t size = 0;
    char * str = (char*)heap_caps_malloc(512, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    if (str == NULL) return luaL_error(L, "failed to allocate memory");
    while (!feof(fp)) {
        size += fread(&str[size], 1, 512, fp);
        if (size % 512 != 0) break;
        char * strn = (char*)heap_caps_realloc(str, size + 512, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
        if (strn == NULL) {
            free(str);
            return luaL_error(L, "failed to allocate memory");
        }
        str = strn;
    }
    lua_pushlstring(L, str, size);
    heap_caps_free(str);
    return 1;
}

int fs_handle_writeString(lua_State *L) {
    FILE * fp = *(FILE**)lua_touserdata(L, lua_upvalueindex(1));
    if (fp == NULL) luaL_error(L, "attempt to use a closed file");
    if (lua_isnoneornil(L, 1)) return 0;
    else if (!lua_isstring(L, 1) && !lua_isnumber(L, 1)) luaL_argerror(L, 1, "string expected");
    size_t sz = 0;
    const char * str = lua_tolstring(L, 1, &sz);
    fwrite(str, sz, 1, fp);
    return 0;
}

int fs_handle_writeLine(lua_State *L) {
    FILE * fp = *(FILE**)lua_touserdata(L, lua_upvalueindex(1));
    if (fp == NULL) luaL_error(L, "attempt to use a closed file");
    if (lua_isnoneornil(L, 1)) return 0;
    else if (!lua_isstring(L, 1) && !lua_isnumber(L, 1)) luaL_argerror(L, 1, "string expected");
    size_t sz = 0;
    const char * str = lua_tolstring(L, 1, &sz);
    fwrite(str, sz, 1, fp);
    fputc('\n', fp);
    return 0;
}

int fs_handle_writeByte(lua_State *L) {
    FILE * fp = *(FILE**)lua_touserdata(L, lua_upvalueindex(1));
    if (fp == NULL) luaL_error(L, "attempt to use a closed file");
    if (lua_type(L, 1) == LUA_TNUMBER) {
        const char b = (unsigned char)(lua_tointeger(L, 1) & 0xFF);
        fputc(b, fp);
    } else if (lua_isstring(L, 1)) {
        if (lua_rawlen(L, 1) == 0) return 0;
        fwrite(lua_tostring(L, 1), lua_rawlen(L, 1), 1, fp);
    } else luaL_argerror(L, 1, "number or string expected");
    return 0;
}

int fs_handle_flush(lua_State *L) {
    FILE * fp = *(FILE**)lua_touserdata(L, lua_upvalueindex(1));
    if (fp == NULL) luaL_error(L, "attempt to use a closed file");
    fflush(fp);
    free_space_cache[0] = free_space_cache[1] = 0;
    return 0;
}

int fs_handle_seek(lua_State *L) {
    FILE * fp = *(FILE**)lua_touserdata(L, lua_upvalueindex(1));
    if (fp == NULL) luaL_error(L, "attempt to use a closed file");
    const char * whence = luaL_optstring(L, 1, "cur");
    const long offset = (long)luaL_optinteger(L, 2, 0);
    int origin = 0;
    if (strcmp(whence, "set") == 0) origin = SEEK_SET;
    else if (strcmp(whence, "cur") == 0) origin = SEEK_CUR;
    else if (strcmp(whence, "end") == 0) origin = SEEK_END;
    else luaL_error(L, "bad argument #1 to 'seek' (invalid option '%s')", whence);
    if (fseek(fp, offset, origin) != 0) {
        lua_pushnil(L);
        lua_pushstring(L, strerror(errno));
        return 2;
    }
    lua_pushinteger(L, ftell(fp));
    return 1;
}
