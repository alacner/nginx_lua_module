#define DDEBUG 1
#include "ddebug.h"

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#define OUT_BUFSIZE 256

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#if ! defined (LUA_VERSION_NUM) || LUA_VERSION_NUM < 501
#include "compat-5.1.h"
#endif

#if LUA_VERSION_NUM < 501
#define luaL_register(a, b, c) luaL_openlib((a), (b), (c), 0)
#endif

#define safe_emalloc(nmemb, size, offset)  malloc((nmemb) * (size) + (offset)) 

#define LUA_NGX_REQUEST "ngx.request" /* nginx request pointer */
#define LUA_NGX_RESPONSE_BUFFER "ngx.response.buffer" /* nginx request pointer */

//Global Setting
lua_State *L; /* lua state handle */

static char *ngx_http_lua_set(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_http_foo_set(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

static ngx_int_t ngx_http_lua_process_init(ngx_cycle_t *cycle);
static void ngx_http_lua_process_exit(ngx_cycle_t *cycle);

static ngx_int_t make_http_header(ngx_http_request_t *r);
static ngx_int_t make_http_body_by_lua(ngx_http_request_t *r, char *out_buf);
static void log_wrapper(ngx_http_request_t *r, const char *ident, int level, lua_State *L);

static char g_foo_settings[64] = {0};

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

    lua_getglobal(L, LUA_NGX_RESPONSE_BUFFER);
    char *out_buf = lua_touserdata(L, -1);
    lua_pop(L, 1);

    sprintf(out_buf, "%s%s", out_buf, str);

    /* push out_buf to lua */
    lua_pushlightuserdata(L, out_buf);
    lua_setglobal(L, LUA_NGX_RESPONSE_BUFFER);

    return 0;
}

/* Commands */
static ngx_command_t  ngx_http_lua_commands[] = {
    { ngx_string("ngx_lua_module"),
      NGX_HTTP_LOC_CONF|NGX_CONF_NOARGS,
      ngx_http_lua_set,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("lua"),
      NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_foo_set,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },  

      ngx_null_command
};

static ngx_http_module_t  ngx_http_lua_module_ctx = {
    NULL,                                  /* preconfiguration */
    NULL,                                     /* postconfiguration */

    NULL,                                  /* create main configuration */
    NULL,                                  /* init main configuration */

    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */

    NULL,                                  /* create location configuration */
    NULL                                   /* merge location configuration */
};

/* hook */
ngx_module_t  ngx_http_lua_module = {
    NGX_MODULE_V1,
    &ngx_http_lua_module_ctx,              /* module context */
    ngx_http_lua_commands,                 /* module directives */
    NGX_HTTP_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    ngx_http_lua_process_init,             /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    ngx_http_lua_process_exit,             /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};

int
ngx_http_lua_print(lua_State *L)
{
    ngx_http_request_t *r;

    lua_getglobal(L, LUA_NGX_REQUEST);
    r = lua_touserdata(L, -1);
    lua_pop(L, 1);

    if (r && r->connection && r->connection->log) {
        log_wrapper(r, "lua print: ", NGX_LOG_NOTICE, L);

    } else {
        dd("(lua-print) can't output print content to error log due "
                "to invalid logging context!");
    }

    return 0;
}

static void
log_wrapper(ngx_http_request_t *r, const char *ident, int level, lua_State *L)
{
    u_char              *buf;
    u_char              *p;
    u_char              *q;
    int                  nargs, i;
    size_t               size, len;

    nargs = lua_gettop(L);
    if (nargs == 0) {
        buf = NULL;
        goto done;
    }

    size = 0;

    for (i = 1; i <= nargs; i++) {
        if (lua_type(L, i) == LUA_TNIL) {
            size += sizeof("nil") - 1;
        } else {
            luaL_checkstring(L, i);
            lua_tolstring(L, i, &len);
            size += len;
        }
    }

    buf = ngx_palloc(r->pool, size + 1);
    if (buf == NULL) {
        luaL_error(L, "out of memory");
        return;
    }

    p = buf;
    for (i = 1; i <= nargs; i++) {
        if (lua_type(L, i) == LUA_TNIL) {
            *p++ = 'n';
            *p++ = 'i';
            *p++ = 'l';
        } else {
            q = (u_char *) lua_tolstring(L, i, &len);
            p = ngx_copy(p, q, len);
        }
    }

    *p++ = '\0';

done:
    ngx_log_error((ngx_uint_t) level, r->connection->log, 0,
            "%s%s", ident, (buf == NULL) ? (u_char *) "(null)" : buf);
}

/* setting header for no-cache */
static ngx_int_t make_http_header(ngx_http_request_t *r){
    ngx_uint_t        i;
    ngx_table_elt_t  *cc, **ccp;

    r->headers_out.content_type.len = sizeof("text/html") - 1;
    r->headers_out.content_type.data = (u_char *) "text/html";
    ccp = r->headers_out.cache_control.elts;
    if (ccp == NULL) {

        if (ngx_array_init(&r->headers_out.cache_control, r->pool,
                           1, sizeof(ngx_table_elt_t *))
            != NGX_OK)
        {
            return NGX_ERROR;
        }

        ccp = ngx_array_push(&r->headers_out.cache_control);
        if (ccp == NULL) {
            return NGX_ERROR;
        }

        cc = ngx_list_push(&r->headers_out.headers);
        if (cc == NULL) {
            return NGX_ERROR;
        }

        cc->hash = 1;
        cc->key.len = sizeof("Cache-Control") - 1;
        cc->key.data = (u_char *) "Cache-Control";

        *ccp = cc;

    } else {
        for (i = 1; i < r->headers_out.cache_control.nelts; i++) {
            ccp[i]->hash = 0;
        }

        cc = ccp[0];
    }

    cc->value.len = sizeof("no-cache") - 1;
    cc->value.data = (u_char *) "no-cache";

    return NGX_OK;
}

static ngx_int_t make_http_body_by_lua(ngx_http_request_t *r, char *out_buf){

    /* push ngx_http_request_t to lua */
    lua_pushlightuserdata(L, r);
    lua_setglobal(L, LUA_NGX_REQUEST);


    /* push out_buf to lua */
    lua_pushlightuserdata(L, out_buf);
    lua_setglobal(L, LUA_NGX_RESPONSE_BUFFER);

    lua_newtable(L); /* ngx */

    lua_pushnumber(L, ngx_random());
    lua_setfield(L, -2, "random");

    lua_pushcfunction(L, luaM_print);
    lua_setfield(L, -2, "print");

    /* {{{ ngx.server */
    lua_newtable(L);

    lua_pushlstring(L, (const char *)r->uri.data, r->uri.len);
    lua_setfield(L, -2, "REQUEST_URI");

    lua_pushlstring(L, (const char *)r->args.data, r->args.len);
    lua_setfield(L, -2, "QUERY_STRING");

    lua_pushnumber(L, (int)time((time_t*)NULL));
    lua_setfield(L, -2, "REQUEST_TIME");

    lua_pushnumber(L, r->method);
    lua_setfield(L, -2, "REQUEST_METHOD");

    lua_setfield(L, -2, "server");
    /* }}} */

    lua_setglobal(L, "ngx");

    if (luaL_dofile(L, "/usr/local/nginx/conf/test.lua") != 0) {
	ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                "runtime error: %s", lua_tostring(L, -1));
        lua_pop(L, 1);
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }


    return NGX_OK;
}

static ngx_int_t
ngx_http_lua_handler(ngx_http_request_t *r)
{
    ngx_int_t     rc;
    ngx_buf_t    *b;
    ngx_chain_t   out;

    /* Http Output Buffer */
    char out_buf[OUT_BUFSIZE] = {0};

    if (!(r->method & (NGX_HTTP_GET|NGX_HTTP_HEAD))) {
        return NGX_HTTP_NOT_ALLOWED;
    }

    rc = ngx_http_discard_request_body(r);

    if (rc != NGX_OK && rc != NGX_AGAIN) {
        return rc;
    }

    /* make http header */
    rc = make_http_header(r);
    if (rc != NGX_OK) {
        return rc;
    }

    if (r->method == NGX_HTTP_HEAD) {
        r->headers_out.status = NGX_HTTP_OK;
        return ngx_http_send_header(r);
    } else if (r->method == NGX_HTTP_GET || r->method == NGX_HTTP_GET) {
        /* make http body buffer by lua */
        rc = make_http_body_by_lua(r, out_buf);
        if (rc != NGX_OK) {
            return rc;
        }
    } else {
        return NGX_HTTP_NOT_ALLOWED;
    }

    b = ngx_pcalloc(r->pool, sizeof(ngx_buf_t));
    if (b == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    out.buf = b;
    out.next = NULL;

    b->pos = (u_char *)out_buf;
    b->last = (u_char *)out_buf + strlen(out_buf);
    b->memory = 1;
    b->last_buf = 1;
    r->headers_out.status = NGX_HTTP_OK;
    r->headers_out.content_length_n = strlen(out_buf);

    rc = ngx_http_send_header(r);

    if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
        return rc;
    }

    ngx_http_output_filter(r, &out);
    return ngx_http_output_filter(r, &out);
}

static char *
ngx_http_lua_set(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_core_loc_conf_t *clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);

    /* register hanlder */
    clcf->handler = ngx_http_lua_handler;

    return NGX_CONF_OK;
}

static char *
ngx_http_foo_set(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_str_t *value = cf->args->elts;
    memcpy(g_foo_settings, value[1].data, value[1].len);
    g_foo_settings[value[1].len] = '\0';

    return NGX_CONF_OK;
}

static ngx_int_t
ngx_http_lua_process_init(ngx_cycle_t *cycle)
{
    L = luaL_newstate();
    if (L == NULL) {
        ngx_log_error(NGX_LOG_ERR, cycle->log, 0, "Failed to initialize Lua VM");
        dd("Failed to initialize Lua VM");
        return NGX_ERROR;
    }

    lua_gc(L, LUA_GCSTOP, 0);
    luaL_openlibs(L);
    lua_gc(L, LUA_GCRESTART, 0);

    lua_pushcfunction(L, ngx_http_lua_print);
    lua_setglobal(L, "print");

    dd("Lua VM initialized!");
    //ngx_log_debug0(NGX_LOG_DEBUG_CORE, cycle->log, 0, "Lua VM initialized!");

    return NGX_OK;
}

static void
ngx_http_lua_process_exit(ngx_cycle_t *cycle)
{
    lua_close(L);
    return;
}
