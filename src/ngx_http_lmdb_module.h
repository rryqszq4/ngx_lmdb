/**
 *    Copyright(c) 2017 rryqszq4
 *
 *
 */

#ifndef NGX_HTTP_LMDB_MODULE_H
#define NGX_HTTP_LMDB_MODULE_H

#include <ngx_core.h>
#include <ngx_http.h>
#include <ngx_config.h>
#include <nginx.h>

#include <lmdb.h>

#define NGX_HTTP_LMDB_MODULE_NAME "ngx_lmdb"
#define NGX_HTTP_LMDB_MODULE_VERSION "0.0.1"

extern ngx_module_t ngx_http_lmdb_module;
ngx_http_request_t *ngx_lmdb_request;

typedef struct {
    MDB_val key;
    MDB_val value;
} ngx_http_lmdb_query_t;

typedef struct {
    unsigned enabled_content_handler : 1;

    
} ngx_http_lmdb_main_conf_t;

typedef struct {
    ngx_str_t lmdb_database;

    ngx_http_lmdb_query_t lmdb_query;

    ngx_int_t (*content_handler)(ngx_http_request_t *r);
} ngx_http_lmdb_loc_conf_t;

typedef struct {
    ngx_chain_t **last;
    ngx_chain_t *out;
} ngx_http_lmdb_rputs_chain_list_t;

typedef struct {
    ngx_http_lmdb_rputs_chain_list_t *rputs_chain;

    unsigned request_body_more : 1;
} ngx_http_lmdb_ctx_t;

#endif