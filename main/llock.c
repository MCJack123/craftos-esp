#include <lua.h>

void _lua_lock(lua_State *L) {}
void _lua_unlock(lua_State *L) {}
void * _lua_newlock() {return NULL;}
void _lua_freelock(void * l) {}