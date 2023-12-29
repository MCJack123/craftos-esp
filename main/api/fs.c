#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <lauxlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <esp_littlefs.h>
#include <esp_vfs_fat.h>
#include "fs_handle.h"
#include "../driver/storage.h"

extern int tsort(lua_State *L);

char* dirname(char* path) {
	if (path[0] == '/') strcpy(path, &path[1]);
    char tch;
    if (strrchr(path, '/') != NULL) tch = '/';
    else if (strrchr(path, '\\') != NULL) tch = '\\';
    else {
        path[0] = '\0';
        return path;
    }
    path[strrchr(path, tch) - path] = '\0';
	return path;
}

char * unconst(const char * str) {
    char * retval = heap_caps_malloc(strlen(str) + 1, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    strcpy(retval, str);
    return retval;
}

static inline bool allchar(const char* str, char c) {
    while (*str) if (*str++ != c) return false;
    return true;
}

char* fixpath(const char* pat) {
    char* path = unconst(pat);
    char* retval = (char*)malloc(strlen(path) + 2);
    retval[0] = 0;
    for (char* tok = strtok(path, "/\\"); tok; tok = strtok(NULL, "/\\")) {
        if (strcmp(tok, "") == 0) continue;
        else if (strcmp(tok, "..") == 0) {
            char* p = strrchr(retval, '/');
            if (p) *p = '\0';
            else strcpy(retval, "..");
        } else if (allchar(tok, ".")) continue;
        else {
            strcat(retval, "/");
            strcat(retval, tok);
        }
    }
    free(path);
    if (strcmp(retval, "") == 0) strcpy(retval, "/");
    return retval;
}

void err(lua_State *L, char * path, const char * err) {
    char * msg = (char*)malloc(strlen(path) + strlen(err) + 3);
    sprintf(msg, "%s: %s", path, err);
    free(path);
    lua_pushstring(L, msg);
    lua_error(L);
}

int fs_list(lua_State *L) {
    struct dirent *dir;
    int i;
    DIR * d;
    char * path = fixpath(lua_tostring(L, 1));
    d = opendir(path);
    if (d) {
        lua_newtable(L);
        for (i = 1; (dir = readdir(d)) != NULL; i++) {
            lua_pushinteger(L, i);
            lua_pushstring(L, dir->d_name);
            lua_settable(L, -3);
        }
        closedir(d);
        if (strcmp(path, "/") == 0) {
            // apparently not needed anymore?
            /*lua_pushinteger(L, i);
            lua_pushliteral(L, "rom");
            lua_settable(L, -3);*/
            if (diskMounted) {
                lua_pushinteger(L, i);
                lua_pushliteral(L, "disk");
                lua_settable(L, -3);
            }
        }
    } else err(L, path, "Not a directory");
    free(path);
    lua_pushcfunction(L, tsort);
    lua_pushvalue(L, -2);
    lua_call(L, 1, 0);
    return 1;
}

int fs_exists(lua_State *L) {
    struct stat st;
    char * path = fixpath(lua_tostring(L, 1));
    lua_pushboolean(L, stat(path, &st) == 0);
    free(path);
    return 1;
}

int fs_isDir(lua_State *L) {
    struct stat st;
    char * path = fixpath(lua_tostring(L, 1));
    lua_pushboolean(L, stat(path, &st) == 0 && S_ISDIR(st.st_mode));
    free(path);
    return 1;
}

static bool isReadOnly(const char* path) {
    if (strcmp(path, "/rom") == 0 || strcmp(path, "rom") == 0) return true;
    else if (strcmp(path, "/disk") == 0 || strcmp(path, "disk") == 0) return false;
    else if (strcmp(path, "/") == 0 || strcmp(path, "") == 0) return false;
    if (access(path, F_OK) != 0) {
        char* p = unconst(path);
        bool retval = isReadOnly(dirname(p));
        free(p);
        return retval;
    }
    return access(path, W_OK) != 0;
}

int fs_isReadOnly(lua_State *L) {
    char * path = fixpath(lua_tostring(L, 1));
    lua_pushboolean(L, isReadOnly(path));
    free(path);
    return 1;
}

int fs_getName(lua_State *L) {
    char * path = unconst(lua_tostring(L, 1));
    lua_pushstring(L, basename(path));
    free(path);
    return 1;
}

int fs_getDrive(lua_State *L) {
    char * path = fixpath(lua_tostring(L, 1));
    if (strncmp(path, "/rom/", 5) == 0) lua_pushliteral(L, "rom");
    else if (strncmp(path, "/disk/", 6) == 0) lua_pushliteral(L, "disk");
    else lua_pushliteral(L, "hdd");
    free(path);
    return 1;
}

int fs_getSize(lua_State *L) {
    struct stat st;
    char * path = fixpath(lua_tostring(L, 1));
    if (stat(path, &st) != 0) err(L, path, "No such file");
    lua_pushinteger(L, st.st_size);
    free(path);
    return 1;
}

int fs_getFreeSpace(lua_State *L) {
    char * path = fixpath(lua_tostring(L, 1));
    size_t cap, used;
    if (strncmp(path, "/rom", 4) == 0) cap = 0, used = 0;
    else if (strncmp(path, "/disk", 5) == 0) esp_vfs_fat_info("/disk", &cap, &used);
    else esp_littlefs_info("data", &cap, &used);
    lua_pushinteger(L, used);
    return 1;
}

int fs_getCapacity(lua_State *L) {
    char * path = fixpath(lua_tostring(L, 1));
    size_t cap, used;
    if (strncmp(path, "/rom", 4) == 0) cap = 0, used = 0;
    else if (strncmp(path, "/disk", 5) == 0) esp_vfs_fat_info("/disk", &cap, &used);
    else esp_littlefs_info("data", &cap, &used);
    lua_pushinteger(L, cap);
    return 1;
}

int recurse_mkdir(const char * path) {
    if (mkdir(path, 0777) != 0) {
        if (errno == ENOENT && strcmp(path, "/") != 0) {
            if (recurse_mkdir(dirname(unconst(path)))) return 1;
            mkdir(path, 0777);
        } else return 1;
    }
    return 0;
}

int fs_makeDir(lua_State *L) {
    char * path = fixpath(lua_tostring(L, 1));
    if (recurse_mkdir(path) != 0) err(L, path, strerror(errno));
    free(path);
    return 0;
}

int fs_move(lua_State *L) {
    char * fromPath, *toPath;
    fromPath = fixpath(lua_tostring(L, 1));
    toPath = fixpath(lua_tostring(L, 2));
    char* dir = unconst(toPath);
    if (strcmp(dirname(dir), "") != 0) recurse_mkdir(dir);
    free(dir);
    if (rename(fromPath, toPath) != 0) {
        free(toPath);
        err(L, fromPath, strerror(errno));
    }
    free(fromPath);
    free(toPath);
    return 0;
}

static int aux_copy(const char* fromPath, const char* toPath) {
    static EXT_RAM_ATTR char tmp[1024];
    FILE * fromfp, *tofp;
    int read;
    struct stat st;
    if (stat(fromPath, &st) != 0) return -1;
    if (S_ISDIR(st.st_mode)) {
        struct dirent *dir;
        int i;
        DIR * d;
        d = opendir(fromPath);
        if (d) {
            for (i = 1; (dir = readdir(d)) != NULL; i++) {
                char* fp = heap_caps_malloc(strlen(fromPath) + strlen(dir->d_name) + 2, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
                char* tp = heap_caps_malloc(strlen(toPath) + strlen(dir->d_name) + 2, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
                strcpy(fp, fromPath);
                strcat(fp, "/");
                strcat(fp, dir->d_name);
                strcpy(tp, toPath);
                strcat(tp, "/");
                strcat(tp, dir->d_name);
                int err = aux_copy(fp, tp);
                heap_caps_free(fp);
                heap_caps_free(tp);
                if (err) return err;
            }
            closedir(d);
        } else return -1;
    } else {
        char* dir = unconst(toPath);
        if (strcmp(dirname(dir), "") != 0) recurse_mkdir(dir);
        free(dir);
        fromfp = fopen(fromPath, "r");
        if (fromfp == NULL) {
            return -1;
        }
        tofp = fopen(toPath, "w");
        if (tofp == NULL) {
            fclose(fromfp);
            return -1;
        }

        while (!feof(fromfp)) {
            read = fread(tmp, 1, 1024, fromfp);
            fwrite(tmp, read, 1, tofp);
            if (read < 1024) break;
        }

        fclose(fromfp);
        fclose(tofp);
    }
    return 0;
}

int fs_copy(lua_State *L) {
    char* fromPath = fixpath(lua_tostring(L, 1));
    char* toPath = fixpath(lua_tostring(L, 2));
    if (aux_copy(fromPath, toPath)) {
        free(toPath);
        err(L, fromPath, "Failed to copy");
    }
    free(fromPath);
    free(toPath);
    return 0;
}

int aux_delete(const char* path) {
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    if (S_ISDIR(st.st_mode)) {
        struct dirent *dir;
        int i;
        DIR * d;
        int ok = 0;
        d = opendir(path);
        if (d) {
            for (i = 1; (dir = readdir(d)) != NULL; i++) {
                char* p = heap_caps_malloc(strlen(path) + strlen(dir->d_name) + 2, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
                strcpy(p, path);
                strcat(p, "/");
                strcat(p, dir->d_name);
                ok = aux_delete(p) || ok;
                heap_caps_free(p);
            }
            closedir(d);
            rmdir(path);
            return ok;
        } else return -1;
    } else return unlink(path);
}

int fs_delete(lua_State *L) {
    char * path = fixpath(lua_tostring(L, 1));
    if (aux_delete(path) != 0) err(L, path, strerror(errno));
    free(path);
    return 0;
}

int fs_combine(lua_State *L) {
    char * basePath = unconst(luaL_checkstring(L, 1));
    for (int i = 2; i <= lua_gettop(L); i++) {
        if (!lua_isstring(L, i)) {
            free(basePath);
            luaL_checkstring(L, i);
            return 0;
        }
        const char * str = lua_tostring(L, i);
        char* ns = heap_caps_malloc(strlen(basePath)+strlen(str)+3, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
        strcpy(ns, basePath);
        if (strlen(basePath) > 0 && basePath[strlen(basePath)-1] == '/' && str[0] == '/') strcat(ns, str + 1);
        else {
            if (strlen(basePath) && basePath[strlen(basePath)-1] != '/' && str[0] != '/') strcat(ns, "/");
            strcat(ns, str);
        }
        free(basePath);
        basePath = ns;
    }
    char* newPath = fixpath(basePath);
    lua_pushstring(L, newPath + 1);
    free(newPath);
    free(basePath);
    return 1;
}

static int fs_open(lua_State *L) {
    const char * mode = luaL_checkstring(L, 2);
    if ((mode[0] != 'r' && mode[0] != 'w' && mode[0] != 'a') || (mode[1] != 'b' && mode[1] != '\0')) luaL_error(L, "%s: Unsupported mode", mode);
    const char * path = fixpath(luaL_checkstring(L, 1));
    struct stat st;
    if (mode[0] == 'r' && stat(path, &st) != 0) {
        lua_pushnil(L);
        lua_pushfstring(L, "%s: No such file", path);
        free(path);
        return 2;
    }
    else if (S_ISDIR(st.st_mode)) { 
        lua_pushnil(L);
        if (strcmp(mode, "r") == 0 || strcmp(mode, "rb") == 0) lua_pushfstring(L, "%s: No such file", path);
        else lua_pushfstring(L, "%s: Cannot write to directory", path);
        free(path);
        return 2; 
    }
    char* dir = unconst(path);
    if (strcmp(dirname(dir), "") != 0) recurse_mkdir(dir);
    free(dir);
    FILE ** fp = (FILE**)lua_newuserdata(L, sizeof(FILE*));
    int fpid = lua_gettop(L);
    *fp = fopen(path, mode);
    if (*fp == NULL) { 
        lua_remove(L, fpid);
        lua_pushnil(L);
        lua_pushfstring(L, "%s: No such file", path);
        free(path);
        return 2; 
    }
    lua_createtable(L, 0, 4);
    lua_pushstring(L, "close");
    lua_pushvalue(L, fpid);
    lua_pushcclosure(L, fs_handle_close, 1);
    lua_settable(L, -3);
    if (strcmp(mode, "r") == 0 || strcmp(mode, "rb") == 0) {
        lua_pushstring(L, "readAll");
        lua_pushvalue(L, fpid);
        lua_pushcclosure(L, fs_handle_readAllByte, 1);
        lua_settable(L, -3);

        lua_pushstring(L, "readLine");
        lua_pushvalue(L, fpid);
        lua_pushboolean(L, false);
        lua_pushcclosure(L, fs_handle_readLine, 2);
        lua_settable(L, -3);

        lua_pushstring(L, "read");
        lua_pushvalue(L, fpid);
        lua_pushcclosure(L, strcmp(mode, "rb") == 0 ? fs_handle_readByte : fs_handle_readChar, 1);
        lua_settable(L, -3);

        lua_pushstring(L, "seek");
        lua_pushvalue(L, fpid);
        lua_pushcclosure(L, fs_handle_seek, 1);
        lua_settable(L, -3);
    } else if (strcmp(mode, "w") == 0 || strcmp(mode, "a") == 0 || strcmp(mode, "wb") == 0 || strcmp(mode, "ab") == 0) {
        lua_pushstring(L, "write");
        lua_pushvalue(L, fpid);
        lua_pushcclosure(L, fs_handle_writeByte, 1);
        lua_settable(L, -3);

        lua_pushstring(L, "writeLine");
        lua_pushvalue(L, fpid);
        lua_pushcclosure(L, fs_handle_writeLine, 1);
        lua_settable(L, -3);

        lua_pushstring(L, "flush");
        lua_pushvalue(L, fpid);
        lua_pushcclosure(L, fs_handle_flush, 1);
        lua_settable(L, -3);

        lua_pushstring(L, "seek");
        lua_pushvalue(L, fpid);
        lua_pushcclosure(L, fs_handle_seek, 1);
        lua_settable(L, -3);
    } else {
        // This should now be unreachable, but we'll keep it here for safety
        fclose(*fp);
        lua_remove(L, fpid);
        free(path);
        luaL_error(L, "%s: Unsupported mode", mode);
    }
    free(path);
    return 1;
}

int fs_getDir(lua_State *L) {
    char * path = unconst(lua_tostring(L, 1));
    lua_pushstring(L, dirname(path));
    free(path);
    return 1;
}

#define st_time_ms(st) ((st##tim.tv_nsec / 1000000) + (st##time * 1000.0))

int fs_attributes(lua_State *L) {
    const char* path = fixpath(lua_tostring(L, 1));
    struct stat st;
    if (stat(path, &st) != 0) {
        lua_pushnil(L);
        return 1;
    }
    lua_createtable(L, 0, 6);
    lua_pushinteger(L, st_time_ms(st.st_m));
    lua_setfield(L, -2, "modification");
    lua_pushinteger(L, st_time_ms(st.st_m));
    lua_setfield(L, -2, "modified");
    lua_pushinteger(L, st_time_ms(st.st_c));
    lua_setfield(L, -2, "created");
    lua_pushinteger(L, S_ISDIR(st.st_mode) ? 0 : st.st_size);
    lua_setfield(L, -2, "size");
    lua_pushboolean(L, S_ISDIR(st.st_mode));
    lua_setfield(L, -2, "isDir");
    lua_pushboolean(L, isReadOnly(path));
    lua_setfield(L, -2, "isReadOnly");
    free(path);
    return 1;
}

const luaL_Reg fs_lib[] = {
    {"list", fs_list},
    {"exists", fs_exists},
    {"isDir", fs_isDir},
    {"isReadOnly", fs_isReadOnly},
    {"getName", fs_getName},
    {"getDrive", fs_getDrive},
    {"getSize", fs_getSize},
    {"getFreeSpace", fs_getFreeSpace},
    {"makeDir", fs_makeDir},
    {"move", fs_move},
    {"copy", fs_copy},
    {"delete", fs_delete},
    {"combine", fs_combine},
    {"open", fs_open},
    {"getDir", fs_getDir},
    {"attributes", fs_attributes},
    {NULL, NULL}
};
