#include <lua.h>
extern int fs_handle_close(lua_State *L);
extern int fs_handle_readAll(lua_State *L);
extern int fs_handle_readAllByte(lua_State *L);
extern int fs_handle_readLine(lua_State *L);
extern int fs_handle_readChar(lua_State *L);
extern int fs_handle_readByte(lua_State *L);
extern int fs_handle_writeString(lua_State *L);
extern int fs_handle_writeLine(lua_State *L);
extern int fs_handle_writeByte(lua_State *L);
extern int fs_handle_flush(lua_State *L);
extern int fs_handle_seek(lua_State *L);
