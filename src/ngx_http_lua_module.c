/* Copyright (C) Alacner */
#define DDEBUG 1
#include "ddebug.h"

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

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

typedef struct {
    ngx_str_t file_src;
} ngx_http_lua_loc_conf_t;

#define LUA_NGX_REQUEST "_ngx.request" /* nginx request pointer */
#define LUA_NGX_OUT_BUFFER "_ngx.out_buffer" /* nginx request pointer */

#ifndef ngx_str_set
#define ngx_str_set(str, text)                                               \
    (str)->len = sizeof(text) - 1; (str)->data = (u_char *) text
#endif

//Global Setting
lua_State *L; /* lua state handle */

static void *ngx_http_lua_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_lua_file(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

static ngx_int_t ngx_http_lua_process_init(ngx_cycle_t *cycle);
static void ngx_http_lua_process_exit(ngx_cycle_t *cycle);

static ngx_int_t ngx_set_http_out_header(ngx_http_request_t *r, char *key, char *value);
static ngx_int_t ngx_set_http_by_lua(ngx_http_request_t *r);
static void log_wrapper(ngx_http_request_t *r, const char *ident, int level, lua_State *L);

static char g_foo_settings[64] = {0};

static int luaM_print (lua_State *L);
static int luaM_set_header (lua_State *L);
static int luaM_get_get (lua_State *L);
#if 0

static int luaM_get_request (lua_State *L);
static int luaM_get_post (lua_State *L);
static int luaM_get_files (lua_State *L);

static int luaM_get_cookie (lua_State *L);
static int luaM_set_cookie (lua_State *L);

#endif
static int
luaM_print (lua_State *L) {
    size_t l = 0;
    const char *str = luaL_optlstring(L, 1, NULL, &l);

    luaL_Buffer *lb; /* for out_buf */

    lua_getglobal(L, LUA_NGX_OUT_BUFFER);
    lb = lua_touserdata(L, -1);
    lua_pop(L, 1);

    luaL_addlstring(lb, str, l); 
    return 0;
}

static int
luaM_set_header (lua_State *L) {
    const char *key = luaL_optstring(L, 1, NULL);
    const char *value = luaL_optstring(L, 2, NULL);

    ngx_http_request_t *r;

    lua_getglobal(L, LUA_NGX_REQUEST);
    r = lua_touserdata(L, -1);
    lua_pop(L, 1);

    if (r == NULL) {
        return luaL_error(L, "no request object found");
    }

    if (ngx_set_http_out_header(r, (char *)key, (char *)value) == NGX_OK) {
        lua_pushboolean(L, 1);
    } else {
        lua_pushboolean(L, 0);
    }

    return 1;
}

/* Commands */
static ngx_command_t  ngx_http_lua_commands[] = {
    {
        ngx_string("lua_file"),
        NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
        ngx_http_lua_file,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_lua_loc_conf_t, file_src),
        NULL
    },

    ngx_null_command
};

static ngx_http_module_t  ngx_http_lua_module_ctx = {
    NULL,                                  /* preconfiguration */
    NULL,                                     /* postconfiguration */

    NULL,                                  /* create main configuration */
    NULL,                                  /* init main configuration */

    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */

    ngx_http_lua_create_loc_conf,                                  /* create location configuration */
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
        log_wrapper(r, "lua print: ", NGX_LOG_DEBUG, L);

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

static void *
ngx_http_lua_create_loc_conf(ngx_conf_t *cf) {
    ngx_http_lua_loc_conf_t  *conf;
    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_lua_loc_conf_t));
    if (conf == NULL) {
        return NGX_CONF_ERROR;
    }
    conf->file_src.len = 0;
    conf->file_src.data = NULL;
    return conf;
}

static ngx_int_t ngx_set_http_out_header(ngx_http_request_t *r, char *key, char *value){

    ngx_table_elt_t *head_type = (ngx_table_elt_t *)ngx_list_push(&r->headers_out.headers);
    if (head_type != NULL) {
        head_type->hash = 1;
        head_type->key.len = strlen(key);
        head_type->key.data = (u_char *)key;
        head_type->value.len = strlen(value);
        head_type->value.data = (u_char *)value;

        ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0,
            "set out header: %s: %s", key, value);
    }

    return NGX_OK;
}

static ngx_int_t ngx_set_http_by_lua(ngx_http_request_t *r){
    ngx_int_t     rc;
    ngx_buf_t    *b;
    ngx_chain_t   out;
    luaL_Buffer lb; /* for out_buf */
    ngx_http_lua_loc_conf_t *llcf;

    llcf = ngx_http_get_module_loc_conf(r, ngx_http_lua_module);

    /* push ngx_http_request_t to lua */
    lua_pushlightuserdata(L, r);
    lua_setglobal(L, LUA_NGX_REQUEST);


    luaL_buffinit(L, &lb);
    lua_pushlightuserdata(L, &lb);
    lua_setglobal(L, LUA_NGX_OUT_BUFFER);


    lua_newtable(L); /* ngx */

    lua_pushstring(L, llcf->file_src.data);
    lua_setfield(L, -2, "script_path");

    lua_pushnumber(L, ngx_random());
    lua_setfield(L, -2, "random");

    lua_pushcfunction(L, luaM_print);
    lua_setfield(L, -2, "print");

    lua_pushcfunction(L, luaM_set_header);
    lua_setfield(L, -2, "set_header");

    /* {{{ ngx.server */
    lua_newtable(L);

    lua_pushlstring(L, (const char *)r->uri.data, r->uri.len);
    lua_setfield(L, -2, "REQUEST_URI");

    lua_pushlstring(L, (const char *)r->args.data, r->args.len);
    lua_setfield(L, -2, "QUERY_STRING");

    lua_pushnumber(L, (int)time((time_t*)NULL));
    lua_setfield(L, -2, "REQUEST_TIME");

    lua_pushlstring(L, (const char *)r->http_protocol.data, r->http_protocol.len);
    lua_setfield(L, -2, "SERVER_PROTOCOL");

    lua_pushlstring(L, (const char *)r->method_name.data, r->method_name.len);
    lua_setfield(L, -2, "REQUEST_METHOD");

    if (r->headers_in.connection) {
        lua_pushlstring(L, (const char *)r->headers_in.connection->value.data, r->headers_in.connection->value.len);
        lua_setfield(L, -2, "HTTP_CONNECTION");
    }

    lua_pushlstring(L, (const char *)r->headers_in.host->value.data, r->headers_in.host->value.len);
    lua_setfield(L, -2, "HTTP_HOST");

    lua_pushlstring(L, (const char *)r->headers_in.user_agent->value.data, r->headers_in.user_agent->value.len);
    lua_setfield(L, -2, "HTTP_USER_AGENT");

    if (r->headers_in.referer) {
        lua_pushlstring(L, (const char *)r->headers_in.referer->value.data, r->headers_in.referer->value.len);
        lua_setfield(L, -2, "HTTP_REFERER");
    }

#if (NGX_HTTP_PROXY || NGX_HTTP_REALIP || NGX_HTTP_GEO)
    if (r->headers_in.x_forwarded_for) {
        lua_pushlstring(L, (const char *)r->headers_in.x_forwarded_for->value.data, r->headers_in.x_forwarded_for->value.len);
        lua_setfield(L, -2, "X_FORWARDED_FOR");
    }
#endif

#if (NGX_HTTP_REALIP)
    if (r->headers_in.x_real_ip) {
        lua_pushlstring(L, (const char *)r->headers_in.x_real_ip->value.data, r->headers_in.x_real_ip->value.len);
        lua_setfield(L, -2, "X_REAL_IP");
    }
#endif

    lua_setfield(L, -2, "server");
    /* }}} */

    lua_setglobal(L, "ngx");

    // execute lua code
    if (luaL_dofile(L, "/usr/local/nginx/conf/test.lua") != 0) {
	ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                "runtime error: %s", lua_tostring(L, -1));
        lua_pop(L, 1);
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    luaL_pushresult(&lb);

    const char *out_buf = lua_tostring(L, -1);

    ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0,
            "concat all ngx.print data: %s", lua_tostring(L, -1));

    lua_pop(L, 1);


    // buff
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

    return ngx_http_output_filter(r, &out);
}

static ngx_int_t
ngx_http_lua_handler(ngx_http_request_t *r)
{
    ngx_int_t     rc;

    if (!(r->method & (NGX_HTTP_GET|NGX_HTTP_POST|NGX_HTTP_HEAD))) {
        return NGX_HTTP_NOT_ALLOWED;
    }

    rc = ngx_http_discard_request_body(r);

    if (rc != NGX_OK && rc != NGX_AGAIN) {
        return rc;
    }

    if (r->method == NGX_HTTP_HEAD) {
        r->headers_out.status = NGX_HTTP_OK;
        return ngx_http_send_header(r);
    }

    return ngx_set_http_by_lua(r);
}

static char *
ngx_http_lua_file(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_core_loc_conf_t *clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);

    /* register hanlder */
    clcf->handler = ngx_http_lua_handler;
    ngx_conf_set_str_slot(cf, cmd, conf);

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
