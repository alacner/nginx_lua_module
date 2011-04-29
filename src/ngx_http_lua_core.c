#include "ngx_http_lua_core.h"

static int luaM_print (lua_State *L);
#if 0
static int luaM_get_header (lua_State *L);
static int luaM_set_header (lua_State *L);

static int luaM_get_request (lua_State *L);
static int luaM_get_get (lua_State *L);
static int luaM_get_post (lua_State *L);
static int luaM_get_files (lua_State *L);

static int luaM_get_cookie (lua_State *L);
static int luaM_set_cookie (lua_State *L);
#endif

static int
luaM_print (lua_State *L) {
	const char *str = luaL_optstring(L, 1, NULL);

	lua_getglobal(L, LUA_NGX_REQUEST);
	ngx_http_request_t *r = lua_touserdata(L, -1);
	lua_pop(L, 1);

	

	return 0;
}
