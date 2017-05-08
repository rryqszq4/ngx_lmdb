/**
 *    Copyright(c) 2017 rryqszq4
 *
 *
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <ngx_conf_file.h>
#include <nginx.h>

#include <lmdb.h>

#include "ngx_http_lmdb_module.h"

#define E(expr) CHECK((rc = (expr)) == MDB_SUCCESS, #expr)
#define RES(err, expr) ((rc = expr) == (err) || (CHECK(!rc, #expr), 0))
#define CHECK(test, msg) ((test) ? (void)0 : ((void)fprintf(stderr, \
    "%s:%d: %s: %s\n", __FILE__, __LINE__, msg, mdb_strerror(rc)), abort()))

static ngx_int_t ngx_http_lmdb_init(ngx_conf_t *cf);
static ngx_int_t ngx_http_lmdb_handler_init(ngx_http_core_main_conf_t *cmcf, ngx_http_lmdb_main_conf_t *lmcf);

static void *ngx_http_lmdb_create_main_conf(ngx_conf_t *cf);
static char *ngx_http_lmdb_init_main_conf(ngx_conf_t *cf, void *conf);

static void *ngx_http_lmdb_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_lmdb_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child);

char *ngx_http_lmdb_database(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
char *ngx_http_lmdb_read_content_phase(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
char *ngx_http_lmdb_write_content_phase(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

ngx_int_t ngx_http_lmdb_content_handler(ngx_http_request_t *r);
ngx_int_t ngx_http_lmdb_read_content_handler(ngx_http_request_t *r);
ngx_int_t ngx_http_lmdb_write_content_handler(ngx_http_request_t *r);

void ngx_http_lmdb_echo(ngx_http_request_t *r, char *data, size_t len);

static ngx_command_t ngx_http_lmdb_commands[] = {

    {ngx_string("lmdb_database"),
     NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF
         |NGX_CONF_TAKE1,
     ngx_http_lmdb_database,
     NGX_HTTP_LOC_CONF_OFFSET,
     0,
     NULL
    },

    {ngx_string("lmdb_read"),
     NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF
         |NGX_CONF_NOARGS,
     ngx_http_lmdb_read_content_phase,
     NGX_HTTP_LOC_CONF_OFFSET,
     0,
     ngx_http_lmdb_read_content_handler
    },

    {ngx_string("lmdb_write"),
     NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF
         |NGX_CONF_NOARGS,
     ngx_http_lmdb_write_content_phase,
     NGX_HTTP_LOC_CONF_OFFSET,
     0,
     ngx_http_lmdb_write_content_handler
    },

    ngx_null_command

};

static ngx_http_module_t ngx_http_lmdb_module_ctx = {
    NULL,                               /* preconfiguration */
    ngx_http_lmdb_init,               /* postconfiguration */

    ngx_http_lmdb_create_main_conf,     /* create main configuration */
    ngx_http_lmdb_init_main_conf,     /* init main configuration */

    NULL,                               /* create server configuration */
    NULL,                               /* merge server configuration */

    ngx_http_lmdb_create_loc_conf,      /* create location configuration */
    ngx_http_lmdb_merge_loc_conf       /* merge location configuration */
};

ngx_module_t ngx_http_lmdb_module = {
    NGX_MODULE_V1,
    &ngx_http_lmdb_module_ctx,    /* module context */
    ngx_http_lmdb_commands,       /* module directives */
    NGX_HTTP_MODULE,               /* module type */
    NULL,                          /* init master */
    NULL,                          /* init module */
    NULL,                           /* init process */
    NULL,                          /* init thread */
    NULL,                          /* exit thread */
    NULL,                           /* exit process */
    NULL,                          /* exit master */
    NGX_MODULE_V1_PADDING
};

static ngx_int_t
ngx_http_lmdb_init(ngx_conf_t *cf)
{
    ngx_http_core_main_conf_t *cmcf;
    ngx_http_lmdb_main_conf_t *lmcf;

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);
    lmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_lmdb_module);

    ngx_lmdb_request = NULL;

    if (ngx_http_lmdb_handler_init(cmcf, lmcf) != NGX_OK) {
        return NGX_ERROR;
    }

    return NGX_OK;
}

static ngx_int_t
ngx_http_lmdb_handler_init(ngx_http_core_main_conf_t *cmcf, ngx_http_lmdb_main_conf_t *lmcf)
{
    ngx_int_t i;
    ngx_http_handler_pt *h;
    ngx_http_phases phase;
    ngx_http_phases phases[] = {
        NGX_HTTP_CONTENT_PHASE,
    };

    ngx_int_t phases_c;

    phases_c = sizeof(phases) / sizeof(ngx_http_phases);
    for (i = 0; i < phases_c; i++) {
        phase = phases[i];
        switch (phase) {
            case NGX_HTTP_CONTENT_PHASE:
                if (lmcf->enabled_content_handler) {
                    h = ngx_array_push(&cmcf->phases[phase].handlers);
                    if (h == NULL) {
                        return NGX_ERROR;
                    }
                    *h = ngx_http_lmdb_content_handler;
                }
                break;
            default:
                break;
        }
    }

    return NGX_OK;
}

static void *
ngx_http_lmdb_create_main_conf(ngx_conf_t *cf)
{
    ngx_http_lmdb_main_conf_t *lmcf;

    lmcf = ngx_pcalloc(cf->pool, sizeof(ngx_http_lmdb_main_conf_t));
    if (lmcf == NULL) {
        return NULL;
    }

    return lmcf;
}

static char *
ngx_http_lmdb_init_main_conf(ngx_conf_t *cf, void *conf)
{
    return NGX_CONF_OK;
}

static void *
ngx_http_lmdb_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_lmdb_loc_conf_t *llcf;

    llcf = ngx_pcalloc(cf->pool, sizeof(ngx_http_lmdb_loc_conf_t));
    if (llcf == NULL) {
        return NGX_CONF_ERROR;
    }

    //llcf->lmdb_query = NGX_CONF_UNSET_PTR;

    return llcf;
}

static char *
ngx_http_lmdb_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    //ngx_http_core_loc_conf_t *clcf;
    //clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);

    ngx_http_lmdb_loc_conf_t *prev = parent;
    ngx_http_lmdb_loc_conf_t *conf = child;

    prev->lmdb_database = conf->lmdb_database;

    return NGX_CONF_OK;
}


char *
ngx_http_lmdb_database(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_lmdb_loc_conf_t *llcf = conf;
    ngx_str_t *value;

    if (llcf->lmdb_database.len != 0) {
        return "is duplicated";
    }

    value = cf->args->elts;

    llcf->lmdb_database.len = value[1].len;
    llcf->lmdb_database.data = value[1].data;

    return NGX_CONF_OK;
}

char *
ngx_http_lmdb_read_content_phase(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_lmdb_main_conf_t *lmcf;
    ngx_http_lmdb_loc_conf_t *llcf;

    if (cmd->post == NULL) {
        return NGX_CONF_ERROR;
    }

    lmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_lmdb_module);
    llcf = conf;

    if (llcf->content_handler != NULL) {
        return "is duplicated";
    }

    llcf->content_handler = cmd->post;
    lmcf->enabled_content_handler = 1;

    return NGX_CONF_OK;
}

char *
ngx_http_lmdb_write_content_phase(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_lmdb_main_conf_t *lmcf;
    ngx_http_lmdb_loc_conf_t *llcf;

    if (cmd->post == NULL) {
        return NGX_CONF_ERROR;
    }

    lmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_lmdb_module);
    llcf = conf;

    if (llcf->content_handler != NULL) {
        return "is duplicated";
    }

    llcf->content_handler = cmd->post;
    lmcf->enabled_content_handler = 1;

    return NGX_CONF_OK;
}

ngx_int_t
ngx_http_lmdb_content_handler(ngx_http_request_t *r)
{
    ngx_http_lmdb_loc_conf_t *llcf;
    llcf = ngx_http_get_module_loc_conf(r, ngx_http_lmdb_module);
    if (llcf->content_handler == NULL) {
        return NGX_DECLINED;
    }
    return llcf->content_handler(r);
}

ngx_int_t 
ngx_http_lmdb_read_content_handler(ngx_http_request_t *r)
{
    ngx_http_lmdb_rputs_chain_list_t *chain = NULL;
    ngx_int_t rc;
    ngx_http_lmdb_loc_conf_t *llcf;
    ngx_http_lmdb_ctx_t *ctx;

    int l_rc;
    MDB_env *env;
    MDB_dbi dbi;
    MDB_txn *txn;
    //MDB_val *data = NULL;

    llcf = ngx_http_get_module_loc_conf(r, ngx_http_lmdb_module);
    ctx = ngx_http_get_module_ctx(r, ngx_http_lmdb_module);

    if (ctx == NULL) {
        ctx = ngx_pcalloc(r->pool, sizeof(*ctx));
        if (ctx == NULL) {
            return NGX_ERROR;
        }
    }

    ctx->request_body_more = 1;
    ngx_http_set_ctx(r, ctx, ngx_http_lmdb_module);

    ngx_lmdb_request = r;

    /* lmdb */
    l_rc = mdb_env_create(&env);
    l_rc = mdb_env_set_maxreaders(env, 1);
    l_rc = mdb_env_set_mapsize(env, 10485760);
    l_rc = mdb_env_set_maxdbs(env, 4);
    l_rc = mdb_env_open(env, (char *)llcf->lmdb_database.data, 0, 0664);

    l_rc = mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
    l_rc = mdb_dbi_open(txn, NULL, 0, &dbi);

    if (l_rc) {

    }

    ngx_str_t key;
    if (NGX_OK != ngx_http_arg(r, (u_char *)"key", 3, &key)) {
        return NGX_HTTP_BAD_REQUEST;
    }

    llcf->lmdb_query.key.mv_size = sizeof(int);
    llcf->lmdb_query.key.mv_data = key.data;
    //llcf->lmdb_query.key.mv_data = "020 3141592 foo bar";

    l_rc = mdb_get(txn, dbi, &(llcf->lmdb_query.key), &(llcf->lmdb_query.value));

    //ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "%d %s %s %d", l_rc, mdb_strerror(l_rc), llcf->lmdb_query.value.mv_data, strlen(llcf->lmdb_query.value.mv_data));

    //ngx_http_lmdb_echo(r, (char *)llcf->lmdb_database.data, llcf->lmdb_database.len);

    ngx_http_lmdb_echo(r, (char *)llcf->lmdb_query.value.mv_data, strlen(llcf->lmdb_query.value.mv_data));


    mdb_dbi_close(env, dbi);
    mdb_env_close(env);

    ctx = ngx_http_get_module_ctx(r, ngx_http_lmdb_module);
    chain = ctx->rputs_chain;

    if (!r->headers_out.status){
        r->headers_out.status = NGX_HTTP_OK;
    }

    if (r->method == NGX_HTTP_HEAD){
        rc = ngx_http_send_header(r);
        if (rc != NGX_OK){
            return rc;
        }
    }

    if (chain != NULL){
        (*chain->last)->buf->last_buf = 1;
    }

    rc = ngx_http_send_header(r);
    if (rc != NGX_OK){
        return rc;
    }

    ngx_http_output_filter(r, chain->out);

    ngx_http_set_ctx(r, NULL, ngx_http_lmdb_module);
    

    return NGX_OK;
}

ngx_int_t 
ngx_http_lmdb_write_content_handler(ngx_http_request_t *r)
{
    return NGX_OK;
}

void 
ngx_http_lmdb_echo(ngx_http_request_t *r, char *data, size_t len)
{
    ngx_buf_t *b;
    ngx_http_lmdb_rputs_chain_list_t *chain;
    ngx_http_lmdb_ctx_t *ctx;

    u_char *u_str;
    ngx_str_t ns;

    ctx = ngx_http_get_module_ctx(r, ngx_http_lmdb_module);

    ns.len = len;
    ns.data = (u_char *) data;

    if (ctx->rputs_chain == NULL){
        chain = ngx_pcalloc(r->pool, sizeof(ngx_http_lmdb_rputs_chain_list_t));
        chain->out = ngx_alloc_chain_link(r->pool);
        chain->last = &chain->out;
    }else {
        chain = ctx->rputs_chain;
        (*chain->last)->next = ngx_alloc_chain_link(r->pool);
        chain->last = &(*chain->last)->next;
    }

    b = ngx_calloc_buf(r->pool);
    (*chain->last)->buf = b;
    (*chain->last)->next = NULL;

    u_str = ngx_pstrdup(r->pool, &ns);
    //u_str[ns.len] = '\0';
    (*chain->last)->buf->pos = u_str;
    (*chain->last)->buf->last = u_str + ns.len;
    (*chain->last)->buf->memory = 1;
    ctx->rputs_chain = chain;

    if (r->headers_out.content_length_n == -1){
        r->headers_out.content_length_n += ns.len + 1;
    }else {
        r->headers_out.content_length_n += ns.len;
    }
}





