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

#define CLFACTORY_BEGIN_CODE "return function() "
#define CLFACTORY_BEGIN_SIZE (sizeof(CLFACTORY_BEGIN_CODE)-1)

#define CLFACTORY_END_CODE " end"
#define CLFACTORY_END_SIZE (sizeof(CLFACTORY_END_CODE)-1)

typedef struct {
    int sent_begin;
    int sent_end;
    int extraline;
    FILE *f;
    char buff[LUAL_BUFFERSIZE];
} clfactory_file_ctx_t;


//Global Setting
lua_State *L; /* lua state handle */

static char *ngx_http_lua_set(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_http_foo_set(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

static ngx_int_t ngx_http_lua_process_init(ngx_cycle_t *cycle);
static void ngx_http_lua_process_exit(ngx_cycle_t *cycle);

static ngx_int_t make_http_header(ngx_http_request_t *r);
static ngx_int_t make_http_get_body(ngx_http_request_t *r, char *out_buf);

int ngx_http_lua_clfactory_loadfile(lua_State *L, const char *filename);
static int clfactory_errfile(lua_State *L, const char *what, int fname_index);
static const char * clfactory_getF(lua_State *L, void *ud, size_t *size);

static char g_foo_settings[64] = {0};

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

static ngx_int_t make_http_get_body(ngx_http_request_t *r, char *out_buf){
    char *qs_start = (char *)r->args_start;
    char *qs_end = (char *)r->uri_end;
    char uri[128] = {0};
    char *id;

    if (qs_start == NULL || qs_end == NULL){
        return NGX_HTTP_BAD_REQUEST;
    }
    if ((memcmp(qs_start, "id=", 3) == 0)){
        id = qs_start + 3;
        *qs_end = '\0';
    }else{
        return NGX_HTTP_BAD_REQUEST;
    }
    snprintf(uri, r->uri.len + 1, "%s", r->uri.data);
    sprintf(out_buf, "nconfig=%snid=%snuri=%snret=%lxn", g_foo_settings, id, uri, ngx_random());

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
    } else if (r->method == NGX_HTTP_GET) {
        /* make http get body buffer */
        rc = make_http_get_body(r, out_buf);
        if (rc != NGX_OK) {
            return rc;
        }
    } else {
        return NGX_HTTP_NOT_ALLOWED;
    }


    lua_State *cr = lua_newthread(L);
#if 0
    if (cr) {
        /*  new globals table for coroutine */
        lua_newtable(cr);

        /*  {{{ inherit coroutine's globals to main thread's globals table
         *  for print() function will try to find tostring() in current
         *  globals *  table. */
        lua_createtable(cr, 0, 1);
        lua_pushvalue(cr, LUA_GLOBALSINDEX);
        lua_setfield(cr, -2, "__index");
        lua_setmetatable(cr, -2);
        /*  }}} */

        lua_replace(cr, LUA_GLOBALSINDEX);
    }

    if (cr == NULL) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                "(lua-content-by-chunk) failed to create new coroutine "
                "to handle request");

        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    /*  move code closure to new coroutine */
    lua_xmove(L, cr, 1);

    /*  set closure's env table to new coroutine's globals table */
    lua_pushvalue(cr, LUA_GLOBALSINDEX);
    lua_setfenv(cr, -2);
#endif
    if (0!=ngx_http_lua_clfactory_loadfile(cr, "/usr/local/nginx/conf/test.lua")) { /* load the compile template functions */
    //if (luaL_loadfile(cr, "/usr/local/nginx/conf/test.lua") || lua_pcall(cr, 0, 0, 0)) { /* load the compile template functions */
	ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                "runtime error: %s", lua_tostring(cr, -1));
        lua_pop(cr, 1);
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    //sprintf(out_buf, "%s", lua_tostring(cr, -1));
    //lua_pop(cr, 1);

    //lua_close(cr);



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
    luaL_openlibs(L);

    //lua_pushcfunction(L, ngx_http_lua_print);
    //lua_setglobal(L, "print");

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

int
ngx_http_lua_clfactory_loadfile(lua_State *L, const char *filename)
{
    clfactory_file_ctx_t        lf;
    int                         status, readstatus;
    int                         c;

    /* index of filename on the stack */
    int                         fname_index;

    fname_index = lua_gettop(L) + 1;

    lf.extraline = 0;

    if (filename == NULL) {
        lua_pushliteral(L, "=stdin");
        lf.f = stdin;

    } else {
        lua_pushfstring(L, "@%s", filename);
        lf.f = fopen(filename, "r");

        if (lf.f == NULL)
            return clfactory_errfile(L, "open", fname_index);
    }

    c = getc(lf.f);

    if (c == '#') {  /* Unix exec. file? */
        lf.extraline = 1;

        while ((c = getc(lf.f)) != EOF && c != '\n') {
            /* skip first line */
        }

        if (c == '\n') {
            c = getc(lf.f);
        }
    }

    if (c == LUA_SIGNATURE[0] && filename) {  /* binary file? */
        /* no binary file supported as closure factory code needs to be */
        /* compiled to bytecode along with user code */
        return clfactory_errfile(L, "load binary file", fname_index);
    }

    ungetc(c, lf.f);

    lf.sent_begin = lf.sent_end = 0;
    status = lua_load(L, clfactory_getF, &lf, lua_tostring(L, -1));

    readstatus = ferror(lf.f);

    if (filename)
        fclose(lf.f);  /* close file (even in case of errors) */

    if (readstatus) {
        lua_settop(L, fname_index);  /* ignore results from `lua_load' */
        return clfactory_errfile(L, "read", fname_index);
    }

    lua_remove(L, fname_index);

    return status;
}

static int
clfactory_errfile(lua_State *L, const char *what, int fname_index)
{
    const char      *serr;
    const char      *filename;

    serr = strerror(errno);
    filename = lua_tostring(L, fname_index) + 1;

    lua_pushfstring(L, "cannot %s %s: %s", what, filename, serr);
    lua_remove(L, fname_index);

    return LUA_ERRFILE;
}


static const char *
clfactory_getF(lua_State *L, void *ud, size_t *size)
{
    clfactory_file_ctx_t        *lf;

    lf = (clfactory_file_ctx_t *) ud;

    if (lf->sent_begin == 0) {
        lf->sent_begin = 1;
        *size = CLFACTORY_BEGIN_SIZE;
        return CLFACTORY_BEGIN_CODE;
    }

    if (lf->extraline) {
        lf->extraline = 0;
        *size = 1;
        return "\n";
    }

    if (feof(lf->f)) {
        if (lf->sent_end == 0) {
            lf->sent_end = 1;
            *size = CLFACTORY_END_SIZE;
            return CLFACTORY_END_CODE;
        }

        return NULL;
    }

    *size = fread(lf->buff, 1, sizeof(lf->buff), lf->f);

    return (*size > 0) ? lf->buff : NULL;
}
