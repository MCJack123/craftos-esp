#include <lauxlib.h>
#include "../driver/audio.h"

static int speaker_playNote(lua_State *L) {
    return 0;
}

static int speaker_playSound(lua_State *L) {
    return 0;
}

static int speaker_playAudio(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    int nsamples = lua_rawlen(L, 1);
    if (nsamples == 0 || nsamples > 131072) {
        luaL_error(L, "Too many samples");
    }
    uint8_t* samples = malloc(nsamples);
    for (int i = 0; i < nsamples; i++) {
        lua_rawgeti(L, 1, i+1);
        int n;
        if (!lua_isnumber(L, -1) || (n = lua_tointeger(L, -1)) < -128 || n > 127) {
            free(samples);
            return luaL_error(L, "bad argument #1 (sample #%d out of range)", i + 1);
        }
        samples[i] = n + 128;
        lua_pop(L, 1);
    }
    bool queued = audio_queue(samples, nsamples);
    if (!queued) free(samples);
    lua_pushboolean(L, queued);
    return 1;
}

static int speaker_stop(lua_State *L) {
    return 0;
}

const luaL_Reg speaker_methods[] = {
    {"playNote", speaker_playNote},
    {"playSound", speaker_playSound},
    {"playAudio", speaker_playAudio},
    {"stop", speaker_stop},
    {NULL, NULL}
};
