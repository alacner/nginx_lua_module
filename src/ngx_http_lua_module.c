
/*
 * Copyright (C) Alacner Zhang (alacner@gmail.com)
 */


#include "strtok_r.h"
#define lua_strtok_r(a, b, c) strtok_r((a), (b), (c))

#include <nginx.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#define safe_emalloc(nmemb, size, offset)  malloc((nmemb) * (size) + (offset)) 


typedef struct {
    lua_State *lua; /* lua state handle */
} ngx_http_lua_main_conf_t;

typedef struct {
    ngx_http_complex_value_t file_src;
} ngx_http_lua_loc_conf_t;

typedef struct {
    ngx_chain_t *out; /* buffered response body chains */
    unsigned eof:1; /* last_buf has been sent ? */
} ngx_http_lua_ctx_t;

extern ngx_module_t ngx_http_lua_module;
static ngx_http_output_header_filter_pt ngx_http_next_header_filter;
static ngx_http_output_body_filter_pt ngx_http_next_body_filter;

#define LUA_NGX_REQUEST "_ngx.request" /* nginx request pointer */
#define LUA_NGX_OUT_BUFFER "_ngx.out_buffer" /* nginx request pointer */


static ngx_int_t ngx_http_lua_init(ngx_conf_t *cf);
static void * ngx_http_lua_create_main_conf(ngx_conf_t *cf);
static char * ngx_http_lua_init_main_conf(ngx_conf_t *cf, void *conf);
static void * ngx_http_lua_create_loc_conf(ngx_conf_t *cf);

static ngx_int_t ngx_http_lua_init_worker(ngx_cycle_t *cycle);
static void ngx_http_lua_exit(ngx_cycle_t *cycle);

static ngx_int_t ngx_http_lua_send_chain_link(ngx_http_request_t *r, ngx_http_lua_ctx_t *ctx, ngx_chain_t *in);
static ngx_int_t ngx_http_lua_header_filter(ngx_http_request_t *r);
static ngx_int_t ngx_http_lua_body_filter(ngx_http_request_t *r, ngx_chain_t *in);

static ngx_int_t ngx_set_http_out_header(ngx_http_request_t *r, char *key, char *value);

static lua_State* ngx_http_lua_create_interpreter(ngx_conf_t *cf, ngx_http_lua_main_conf_t *lmcf);
static char * ngx_http_lua_init_interpreter(ngx_conf_t *cf, ngx_http_lua_main_conf_t *lmcf);

static char *ngx_http_lua_file(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static ngx_int_t ngx_http_lua_file_handler(ngx_http_request_t *r);
static void ngx_http_lua_file_request_handler(ngx_http_request_t *r);

static u_char * ngx_http_lua_script_filename(ngx_pool_t *pool, u_char *src, size_t len);

static int luaC_ngx_print(lua_State *L);
static int luaF_ngx_print (lua_State *L);
static int luaF_ngx_set_header (lua_State *L);
static int luaF_ngx_set_cookie (lua_State *L);
static int luaF_ngx_flush(lua_State *L);
static int luaF_ngx_eof(lua_State *L);
static int luaM_ngx_get_header (lua_State *L);
static int luaM_ngx_get (lua_State *L);
static int luaM_ngx_post (lua_State *L);
static int luaM_ngx_get_cookie (lua_State *L);


static ngx_command_t  ngx_http_lua_commands[] = {
    {
        ngx_string("lua_file"),
        NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
        ngx_http_lua_file,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_lua_loc_conf_t, file_src),
        NULL
    },

    ngx_null_command
};


static ngx_http_module_t  ngx_http_lua_module_ctx = {
    NULL,                                  /* preconfiguration */
    ngx_http_lua_init,                     /* postconfiguration */

    ngx_http_lua_create_main_conf,         /* create main configuration */
    ngx_http_lua_init_main_conf,           /* init main configuration */

    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */

    ngx_http_lua_create_loc_conf,          /* create location configuration */
    NULL                                   /* merge location configuration */
};


ngx_module_t  ngx_http_lua_module = {
    NGX_MODULE_V1,
    &ngx_http_lua_module_ctx,              /* module context */
    ngx_http_lua_commands,                 /* module directives */
    NGX_HTTP_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    ngx_http_lua_init_worker,              /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    ngx_http_lua_exit,                     /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


static ngx_int_t
ngx_http_lua_send_chain_link(ngx_http_request_t *r, ngx_http_lua_ctx_t *ctx,
        ngx_chain_t *in)
{
    ngx_int_t            rc;
    ngx_chain_t         *cl;
    ngx_chain_t        **ll;

    if (ctx->eof) {
        ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0,
                      "ctx->eof already set");
        return NGX_OK;
    }

    if (in == NULL) {
        ctx->eof = 1;

        ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0,
                      "send special buf");
        if (r->headers_out.status == 0) {
            r->headers_out.status = NGX_HTTP_OK;
        }

        ngx_http_send_header(r);

        if (ctx->out) {
            rc = ngx_http_output_filter(r, ctx->out);

            if (rc == NGX_ERROR || rc >= NGX_HTTP_SPECIAL_RESPONSE) {
                return rc;
            }

            ctx->out = NULL;
        }

        rc = ngx_http_send_special(r, NGX_HTTP_LAST);
        if (rc == NGX_ERROR || rc >= NGX_HTTP_SPECIAL_RESPONSE) {
            return rc;
        }

        return NGX_OK;
    }

    /* buffer all the output bufs */
    for (cl = ctx->out, ll = &ctx->out; cl; cl = cl->next) {
        ll = &cl->next;
    }

    *ll = in;

    return NGX_OK;
}


static int
luaF_ngx_print(lua_State *L)
{
    ngx_http_request_t          *r;
    ngx_http_lua_ctx_t          *ctx;
    const char                  *p;
    size_t                       len;
    size_t                       size;
    ngx_buf_t                   *b;
    ngx_chain_t                 *cl;
    ngx_int_t                    rc;
    int                          i;
    int                          nargs;

    lua_getglobal(L, LUA_NGX_REQUEST);
    r = lua_touserdata(L, -1);
    lua_pop(L, 1);

    if (r == NULL) {
        return luaL_error(L, "no request object found");
    }

    ctx = ngx_http_get_module_ctx(r, ngx_http_lua_module);

    if (ctx == NULL) {
        return luaL_error(L, "no request ctx found");
    }

    if (ctx->eof) {
        return luaL_error(L, "seen eof already");
    }

    nargs = lua_gettop(L);
    size = 0;

    for (i = 1; i <= nargs; i++) {
        luaL_checkstring(L, i);
        lua_tolstring(L, i, &len);
        size += len;
    }

    if (size == 0) {
        /* do nothing for empty strings */
        return 0;
    }

    b = ngx_create_temp_buf(r->pool, size);
    if (b == NULL) {
        return luaL_error(L, "out of memory");
    }

    for (i = 1; i <= nargs; i++) {
        p = lua_tolstring(L, i, &len);
        b->last = ngx_copy(b->last, (u_char *) p, len);
    }

    cl = ngx_alloc_chain_link(r->pool);
    if (cl == NULL) {
        return luaL_error(L, "out of memory");
    }

    cl->next = NULL;
    cl->buf = b;

    rc = ngx_http_lua_send_chain_link(r, ctx, cl);

    if (rc == NGX_ERROR || rc >= NGX_HTTP_SPECIAL_RESPONSE) {
        return luaL_error(L, "failed to send data through the output filters");
    }

    return 0;
}


static int
luaF_ngx_set_header (lua_State *L)
{
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


static int
luaM_ngx_get_cookie (lua_State *L)
{
    ngx_http_request_t *r;

    lua_getglobal(L, LUA_NGX_REQUEST);
    r = lua_touserdata(L, -1);
    lua_pop(L, 1);

    if (r == NULL) {
        return luaL_error(L, "no request object found");
    }

    lua_newtable(L);

    ngx_int_t i, n;
    ngx_table_elt_t **cookies;
    u_char *p, *cookie;
    ssize_t size;

    n = r->headers_in.cookies.nelts;
    if (n == 0) {
        return 1;
    }

    cookies = r->headers_in.cookies.elts;

    char *strtok_buf, *variable, *k, *v;

    if (n == 1) {
        variable = lua_strtok_r((char *)(*cookies)->value.data, ";", &strtok_buf);

        while (variable) {
            k = lua_strtok_r(variable, "=", &v);
            lua_pushstring(L, v);
            lua_setfield(L, -2, k);
            variable = lua_strtok_r(NULL, ";", &strtok_buf);
        }
        return 1;
    }

    size = - (ssize_t) (sizeof("; ") - 1);

    for (i = 0; i < n; i++) {
        size += cookies[i]->value.len + sizeof("; ") - 1;
    }

    cookie = ngx_pnalloc(r->pool, size);
    if (cookie == NULL) {
        return 1;
    }

    p = cookie;

    for (i = 0; /* void */ ; i++) {
        p = ngx_copy(p, cookies[i]->value.data, cookies[i]->value.len);

        if (i == n - 1) {
            break;
        }

        *p++ = ';'; *p++ = ' ';
    }

    variable = lua_strtok_r((char *)cookie, ";", &strtok_buf);

    while (variable) {
        k = lua_strtok_r(variable, "=", &v);
        lua_pushstring(L, v);
        lua_setfield(L, -2, k);
        variable = lua_strtok_r(NULL, ";", &strtok_buf);
    }

    return 1;
}


static int
luaM_ngx_get_header (lua_State *L)
{
    ngx_http_request_t *r;

    lua_getglobal(L, LUA_NGX_REQUEST);
    r = lua_touserdata(L, -1);
    lua_pop(L, 1);

    if (r == NULL) {
        return luaL_error(L, "no request object found");
    }

    lua_newtable(L);

    ngx_list_part_t *part;
    ngx_table_elt_t *header;
    ngx_uint_t i;

    part = &r->headers_in.headers.part;
    header = part->elts;

    for (i = 0; /* void */ ; i++) {

        if (i >= part->nelts) {
            if (part->next == NULL) {
                break;
            }

            part = part->next;
            header = part->elts;
            i = 0;
        }

        lua_pushlstring(L, (const char *)header[i].value.data, header[i].value.len);
        lua_setfield(L, -2, (const char *)header[i].key.data);
    }

    return 1;
}


static int
luaM_ngx_get (lua_State *L)
{
    ngx_http_request_t *r;

    lua_getglobal(L, LUA_NGX_REQUEST);
    r = lua_touserdata(L, -1);
    lua_pop(L, 1);

    if (r == NULL) {
        return luaL_error(L, "no request object found");
    }

    lua_newtable(L);

    u_char *variables;
    variables = ngx_pnalloc(r->pool, r->args.len+1);
    ngx_cpystrn(variables, r->args.data, r->args.len+1);

    char *strtok_buf, *variable, *k, *v;
    variable = lua_strtok_r((char *)variables, "&", &strtok_buf);

    while (variable) {
        k = lua_strtok_r(variable, "=", &v);
        lua_pushstring(L, v);
        lua_setfield(L, -2, k);
        variable = lua_strtok_r(NULL, "&", &strtok_buf);
    }

    return 1;
}


static int
luaM_ngx_post (lua_State *L)
{
    ngx_http_request_t *r;
    ngx_file_t file;
    size_t size;
    u_char *body;
    ssize_t n;

    lua_getglobal(L, LUA_NGX_REQUEST);
    r = lua_touserdata(L, -1);
    lua_pop(L, 1);

    if (r == NULL) {
        return luaL_error(L, "no request object found");
    }

    lua_newtable(L);

    if (r->request_body == NULL || r->request_body->temp_file == NULL) {
        return 1;
    }

    file = r->request_body->temp_file->file;

    if (ngx_file_info(file.name.data, &file.info) == NGX_FILE_ERROR) {
        return luaL_error(L, "ngx_file_info failed");
    }

    size = (size_t) ngx_file_size(&file.info);

    body = ngx_pcalloc(r->pool, size);
    if (body == NULL) {
        return luaL_error(L, "palloc body out of memory");
    }

    n = ngx_read_file(&file, body, size, 0);

    if (n == NGX_ERROR) {
        ngx_log_error(NGX_LOG_CRIT, r->connection->log, ngx_errno,
                           ngx_read_file_n " \"%s\" failed", file.name.data);
        return luaL_error(L, "readfile failed");
    }

    lua_pushlstring(L, (const char *)body, size);
    lua_setfield(L, -2, "_request_data_");

    return 1;
}


static int
luaF_ngx_set_cookie (lua_State *L)
{
    ngx_http_request_t *r;
    ngx_table_elt_t *set_cookie;

    lua_getglobal(L, LUA_NGX_REQUEST);
    r = lua_touserdata(L, -1);
    lua_pop(L, 1);


    set_cookie = ngx_list_push(&r->headers_out.headers);
    if (set_cookie == NULL) {
        return luaL_error(L, "out of memory");
    }


    //Set-Cookie: _ca=heheheh; expires=Thu, 01-Jan-1970 00:04:10 GMT; path=/; domain=domain; secure
    size_t name_len = 0, value_len = 0, path_len = 0, domain_len = 0;

    const char *name = luaL_optlstring(L, 1, NULL, &name_len);
    const char *value = luaL_optlstring(L, 2, NULL, &value_len);
    int expire = luaL_optnumber(L, 3, 0);
    const char *path = luaL_optlstring(L, 4, NULL, &path_len);
    const char *domain = luaL_optlstring(L, 5, NULL, &domain_len);
    int secure = lua_toboolean(L, 6);
    int http_only = lua_toboolean(L, 7);

    int cnt = 0;
    lua_pushfstring(L, "%s=%s", name, value);
    cnt++;

    if (expire) {
        u_char *p;
        static u_char expires[] = "; expires=Thu, 31-Dec-37 23:55:55 GMT";

        p = ngx_pnalloc(r->pool, sizeof(expires) - 1);
        p = ngx_cpymem(p, expires, sizeof("; expires=") - 1);

        ngx_http_cookie_time(p, ngx_time() + expire);
        lua_pushfstring(L, "; expires=%s", p);
        cnt++;
    }
    
    if (path != NULL) {
        lua_pushfstring(L, "; path=%s", path);
        cnt++;
    }
    
    if (domain != NULL) {
        lua_pushfstring(L, "; domain=%s", domain);
        cnt++;
    }
    
    if (secure) {
        lua_pushfstring(L, "; secure");
        cnt++;
    }
    
    if (http_only) {
        lua_pushfstring(L, "; HttpOnly");
        cnt++;
    }

    lua_concat(L, cnt);

    size_t len = 0;
    const char * cookie = lua_tolstring(L, -1, &len);

    lua_pop(L, 1);

    set_cookie->hash = 1;
    ngx_str_set(&set_cookie->key, "Set-Cookie");
    set_cookie->value.len = len;
    set_cookie->value.data = (u_char *) cookie;

    ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0,
                   "set cookie: \"%V\"", &set_cookie->value);

    lua_pushboolean(L, 1);
    return 1;
}


static int
luaC_ngx_print(lua_State *L)
{
    ngx_http_request_t *r;

    lua_getglobal(L, LUA_NGX_REQUEST);
    r = lua_touserdata(L, -1);
    lua_pop(L, 1);

    if (r && r->connection && r->connection->log) {
        ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0,
                "lua print: %s", luaL_optstring(L, -1, "(null)"));
    }
    return 0;
}


static void *
ngx_http_lua_create_main_conf(ngx_conf_t *cf)
{
    ngx_http_lua_main_conf_t  *lmcf;

    lmcf = ngx_pcalloc(cf->pool, sizeof(ngx_http_lua_main_conf_t));
    if (lmcf == NULL) {
        return NULL;
    }

    return lmcf;
}


static char *
ngx_http_lua_init_main_conf(ngx_conf_t *cf, void *conf)
{
    ngx_http_lua_main_conf_t *lmcf = conf;

    if (lmcf->lua == NULL) {
        if (ngx_http_lua_init_interpreter(cf, lmcf) != NGX_CONF_OK) {
            return NGX_CONF_ERROR;
        }
    }

    return NGX_CONF_OK;
}


static void *
ngx_http_lua_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_lua_loc_conf_t  *conf;
    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_lua_loc_conf_t));
    if (conf == NULL) {
        return NGX_CONF_ERROR;
    }
    return conf;
}


static ngx_int_t ngx_set_http_out_header(ngx_http_request_t *r, char *key, char *value)
{
    int len;

    if (strcasecmp("Content-Type", key) == 0) { /* if key: content-type */
        len = ngx_strlen(value);
        r->headers_out.content_type_len = len; 
        r->headers_out.content_type.len = len;
        r->headers_out.content_type.data = (u_char *)value;
        r->headers_out.content_type_lowcase = NULL;
        return NGX_OK;
    }

    ngx_table_elt_t *head_type;
    head_type = (ngx_table_elt_t *)ngx_list_push(&r->headers_out.headers);
    if (head_type != NULL) {
        head_type->hash = 1;
        head_type->key.len = ngx_strlen(key);
        head_type->key.data = (u_char *)key;
        head_type->value.len = ngx_strlen(value);
        head_type->value.data = (u_char *)value;

        ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0,
            "set out header: %s: %s", key, value);
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_lua_file_handler(ngx_http_request_t *r)
{
    ngx_int_t rc;

    if (r->method == NGX_HTTP_POST) {

        r->request_body_in_file_only = 1;
        r->request_body_in_persistent_file = 1;
        r->request_body_in_clean_file = 1;
        r->request_body_file_group_access = 1;
        r->request_body_file_log_level = 0;

        ngx_log_error(NGX_LOG_ALERT, r->connection->log, 0, "post:%V", &r->uri);
        rc = ngx_http_read_client_request_body(r, ngx_http_lua_file_request_handler);

        if (rc >= NGX_HTTP_SPECIAL_RESPONSE) {
            return rc;
        }

        return NGX_DONE;
    }

    ngx_log_error(NGX_LOG_ALERT, r->connection->log, 0, "get:%V", &r->uri);

    ngx_http_lua_file_request_handler(r);
    return NGX_OK;
}

static void
ngx_http_lua_file_request_handler(ngx_http_request_t *r)
{
    lua_State *L;
    ngx_http_lua_ctx_t *ctx;
    ngx_http_lua_main_conf_t *lmcf;
    ngx_http_lua_loc_conf_t *llcf;
    ngx_str_t file_src;
    u_char *script_filename;

    llcf = ngx_http_get_module_loc_conf(r, ngx_http_lua_module);

    if (ngx_http_complex_value(r, &llcf->file_src, &file_src) != NGX_OK) {
        ngx_http_finalize_request(r, NGX_ERROR);
        return;
    }
    script_filename = ngx_http_lua_script_filename(r->pool, file_src.data, file_src.len);

    if (script_filename == NULL) {
        ngx_http_finalize_request(r, NGX_ERROR);
        return;
    }

    ctx = ngx_http_get_module_ctx(r, ngx_http_lua_module);

    if (ctx == NULL) {
        ctx = ngx_pcalloc(r->pool, sizeof(ngx_http_lua_ctx_t));
        if (ctx == NULL) {
            ngx_http_finalize_request(r, NGX_ERROR);
            return;
        }

        ngx_http_set_ctx(r, ctx, ngx_http_lua_module);
    }

    lmcf = ngx_http_get_module_main_conf(r, ngx_http_lua_module);
    L = lmcf->lua;

    ngx_set_http_out_header(r, "X-Powered-By", LUA_RELEASE);

    /* push ngx_http_request_t to lua */
    lua_pushlightuserdata(L, r);
    lua_setglobal(L, LUA_NGX_REQUEST);

    ngx_log_error(NGX_LOG_ALERT, r->connection->log, 0, "lua_newtable()");

    lua_newtable(L); /* ngx */

    lua_pushnumber(L, ngx_random());
    lua_setfield(L, -2, "random");

    lua_pushcfunction(L, luaF_ngx_print);
    lua_setfield(L, -2, "print");

    lua_pushcfunction(L, luaF_ngx_set_header);
    lua_setfield(L, -2, "set_header");

    lua_pushcfunction(L, luaF_ngx_flush);
    lua_setfield(L, -2, "flush");

    lua_pushcfunction(L, luaF_ngx_eof);
    lua_setfield(L, -2, "eof");

    luaM_ngx_get_cookie(L);
    lua_setfield(L, -2, "cookie");

    luaM_ngx_get_header(L);
    lua_setfield(L, -2, "header");

    lua_pushcfunction(L, luaF_ngx_set_cookie);
    lua_setfield(L, -2, "set_cookie");

    luaM_ngx_get(L);
    lua_setfield(L, -2, "get");

    luaM_ngx_post(L);
    lua_setfield(L, -2, "post");

    /* {{{ ngx.server */
    lua_newtable(L);

    lua_pushstring(L, (const char *)script_filename);
    lua_setfield(L, -2, "SCRIPT_FILENAME");

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
    if (luaL_dofile(L, (const char *)script_filename) != 0) {
	    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                "runtime error: %s", lua_tostring(L, -1));
        lua_pop(L, 1);
        ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
        return;
    }

    return;
}


static char *
ngx_http_lua_file(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    //ngx_http_lua_loc_conf_t *llcf = conf;
    ngx_http_core_loc_conf_t   *clcf;
    ngx_http_lua_main_conf_t  *lmcf;

    lmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_lua_module);

    if (lmcf->lua == NULL) {
        if (ngx_http_lua_init_interpreter(cf, lmcf) != NGX_CONF_OK) {
            return NGX_CONF_ERROR;
        }
    }

    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);

    clcf->handler = ngx_http_lua_file_handler;
    ngx_conf_set_str_slot(cf, cmd, conf);

    return NGX_CONF_OK;
}


static lua_State*
ngx_http_lua_create_interpreter(ngx_conf_t *cf, ngx_http_lua_main_conf_t *lmcf)
{
    lua_State *L;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, cf->log, 0, "create lua interpreter");

    if (ngx_set_environment(cf->cycle, NULL) == NULL) {
        return NULL;
    }

    L = luaL_newstate();
    if (L == NULL) {
        ngx_log_error(NGX_LOG_ALERT, cf->log, 0, "luaL_newstate() failed");
        return NULL;
    }

    lua_gc(L, LUA_GCSTOP, 0);
    luaL_openlibs(L);
    lua_gc(L, LUA_GCRESTART, 0);

    lua_pushcfunction(L, luaC_ngx_print);
    lua_setglobal(L, "print");

    return L;
}


static char *
ngx_http_lua_init_interpreter(ngx_conf_t *cf, ngx_http_lua_main_conf_t *lmcf)
{
    lmcf->lua = ngx_http_lua_create_interpreter(cf, lmcf);

    if (lmcf->lua == NULL) {
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}


static ngx_int_t
ngx_http_lua_init_worker(ngx_cycle_t *cycle)
{
    ngx_http_lua_main_conf_t  *lmcf;

    lmcf = ngx_http_cycle_get_module_main_conf(cycle, ngx_http_lua_module);

    if (lmcf) {
    }

    return NGX_OK;
}


static void
ngx_http_lua_exit(ngx_cycle_t *cycle)
{
    ngx_http_lua_main_conf_t  *lmcf;

    lmcf = ngx_http_cycle_get_module_main_conf(cycle, ngx_http_lua_module);
    if (lmcf->lua) {
        lua_close(lmcf->lua);
    }
}


static ngx_int_t
ngx_http_lua_header_filter(ngx_http_request_t *r)
{
    //ngx_http_lua_ctx_t *ctx;

    //ctx = ngx_http_get_module_ctx(r, ngx_http_lua_module);

    return ngx_http_next_header_filter(r);
}


static ngx_int_t
ngx_http_lua_body_filter(ngx_http_request_t *r, ngx_chain_t *in)
{
    ngx_http_lua_ctx_t  *ctx;
    ngx_chain_t *cl;

    ctx = ngx_http_get_module_ctx(r, ngx_http_lua_module);

    if (ctx == NULL || ctx->eof) {
        return ngx_http_next_body_filter(r, in);
    }

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http lua filter");

    for (cl = in; cl; cl = cl->next) {
        cl->buf->pos = cl->buf->last;
        cl->buf->file_pos = cl->buf->file_last;
    }

    if (r == r->connection->data && r->postponed) {
        /* notify the downstream postpone filter to flush the postponed
         * outputs of the current request */
        return ngx_http_next_body_filter(r, NULL);
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_lua_init(ngx_conf_t *cf)
{
    ngx_http_next_header_filter = ngx_http_top_header_filter;
    ngx_http_top_header_filter = ngx_http_lua_header_filter;

    ngx_http_next_body_filter = ngx_http_top_body_filter;
    ngx_http_top_body_filter = ngx_http_lua_body_filter;

    return NGX_OK;
}


static u_char *
ngx_http_lua_script_filename(ngx_pool_t *pool, u_char *src, size_t len)
{
    u_char *p, *dst;

    if (len == 0) {
        return NULL;
    }

    if (src[0] == '/') {
        /* being an absolute path already */
        dst = ngx_palloc(pool, len + 1);
        if (dst == NULL) {
            return NULL;
        }

        p = ngx_copy(dst, src, len);

        *p = '\0';

        return dst;
    }

    dst = ngx_palloc(pool, ngx_cycle->prefix.len + len + 1);
    if (dst == NULL) {
        return NULL;
    }

    p = ngx_copy(dst, ngx_cycle->prefix.data, ngx_cycle->prefix.len);
    p = ngx_copy(p, src, len);

    *p = '\0';

    return dst;
}


/**
 * Force flush out response content
 * */
static int
luaF_ngx_flush(lua_State *L)
{
    ngx_http_request_t          *r;
    ngx_http_lua_ctx_t          *ctx;
    ngx_buf_t                   *buf;
    ngx_chain_t                 *cl;
    ngx_int_t                    rc;

    lua_getglobal(L, LUA_NGX_REQUEST);
    r = lua_touserdata(L, -1);
    lua_pop(L, 1);

    if (r == NULL) {
        return luaL_error(L, "no request object found");
    }

    ctx = ngx_http_get_module_ctx(r, ngx_http_lua_module);
    if (ctx == NULL) {
        return luaL_error(L, "no request ctx found");
    }

    if (ctx->eof) {
        return luaL_error(L, "already seen eof");
    }

    buf = ngx_calloc_buf(r->pool);
    if (buf == NULL) {
        return luaL_error(L, "memory allocation error");
    }

    buf->flush = 1;

    cl = ngx_alloc_chain_link(r->pool);
    if (cl == NULL) {
        return luaL_error(L, "out of memory");
    }

    cl->next = NULL;
    cl->buf = buf;

    rc = ngx_http_lua_send_chain_link(r, ctx, cl);

    if (rc == NGX_ERROR || rc >= NGX_HTTP_SPECIAL_RESPONSE) {
        return luaL_error(L, "failed to send chain link: %d", (int) rc);
    }

    return 0;
}


/**
 * Send last_buf, terminate output stream
 * */
static int
luaF_ngx_eof(lua_State *L)
{
    ngx_http_request_t      *r;
    ngx_http_lua_ctx_t      *ctx;
    ngx_int_t                rc;

    lua_getglobal(L, LUA_NGX_REQUEST);
    r = lua_touserdata(L, -1);
    lua_pop(L, 1);

    if (r == NULL) {
        return luaL_error(L, "no request object found");
    }

    ctx = ngx_http_get_module_ctx(r, ngx_http_lua_module);

    rc = ngx_http_lua_send_chain_link(r, ctx, NULL/*indicate last_buf*/);

    if (rc == NGX_ERROR || rc >= NGX_HTTP_SPECIAL_RESPONSE) {
        return luaL_error(L, "failed to send eof buf");
    }

    return 0;
}
