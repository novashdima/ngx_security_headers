/*
 * Copyright (c) 2019 Danila Vershinin ( https://www.getpagespeed.com )
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <ngx_string.h>

#define NGX_HTTP_SECURITY_HEADER_OMIT  0

#define NGX_HTTP_XSS_HEADER_OFF        1
#define NGX_HTTP_XSS_HEADER_ON         2
#define NGX_HTTP_XSS_HEADER_BLOCK      3

#define NGX_HTTP_FO_HEADER_SAME        1
#define NGX_HTTP_FO_HEADER_DENY        2

/* The Referrer Policy header */
#define NGX_HTTP_RP_HEADER_NO                        1
#define NGX_HTTP_RP_HEADER_DOWNGRADE                 2
#define NGX_HTTP_RP_HEADER_SAME_ORIGIN               3
#define NGX_HTTP_RP_HEADER_ORIGIN                    4
#define NGX_HTTP_RP_HEADER_STRICT_ORIGIN             5
#define NGX_HTTP_RP_HEADER_ORIGIN_WHEN_CROSS         6
#define NGX_HTTP_RP_HEADER_STRICT_ORIG_WHEN_CROSS    7
#define NGX_HTTP_RP_HEADER_UNSAFE_URL                8

typedef struct {
    ngx_flag_t                 enable;
    ngx_flag_t                 hide_server_tokens;
    ngx_flag_t                 hsts_preload;

    ngx_uint_t                 xss;
    ngx_uint_t                 fo;
    ngx_uint_t                 rp;

    ngx_hash_t                 text_types;
    ngx_array_t                *text_types_keys;

} ngx_http_security_headers_loc_conf_t;

static ngx_str_t empty_val = ngx_string("");

static ngx_str_t hide_headers[] = {
    ngx_string("x-powered-by"),
    ngx_string("x-cf-powered-by"),
    ngx_string("via"),
    ngx_string("x-amz-cf-id"),
    ngx_string("x-amz-cf-pop"),
    ngx_string("x-page-speed"),
    ngx_string("x-varnish"),
    ngx_string("x-cache"),
    ngx_string("x-cache-hits"),
    ngx_string("x-cache-status"),
    ngx_string("x-application-version"),
    ngx_string("x-hudson"),
    ngx_string("x-hudson-theme"),
    ngx_string("x-instance-identity"),
    ngx_string("x-jenkins"),
    ngx_string("x-jenkins-session"),
    ngx_string("x-envoy-upstream-service-time"),
    ngx_string("x-drupal-cache"),
    ngx_string("x-generator"),
    ngx_string("x-backend-server"),
    ngx_string("x-wix-request-id"),
    ngx_string("x-request-id"),
    ngx_string("x-sucuri-id"),
    ngx_string("x-hacker")
};

static ngx_conf_enum_t  ngx_http_xss_protection[] = {
    { ngx_string("off"),    NGX_HTTP_XSS_HEADER_OFF },
    { ngx_string("on"),     NGX_HTTP_XSS_HEADER_ON },
    { ngx_string("block"),  NGX_HTTP_XSS_HEADER_BLOCK },
    { ngx_string("omit"),   NGX_HTTP_SECURITY_HEADER_OMIT },
    { ngx_null_string, 0 }
};

static ngx_conf_enum_t  ngx_http_frame_options[] = {
    { ngx_string("sameorigin"),  NGX_HTTP_FO_HEADER_SAME },
    { ngx_string("deny"),        NGX_HTTP_FO_HEADER_DENY },
    { ngx_string("omit"),        NGX_HTTP_SECURITY_HEADER_OMIT },
    { ngx_null_string, 0 }
};

static ngx_conf_enum_t  ngx_http_referrer_policy[] = {
    { ngx_string("no-referrer"),
      NGX_HTTP_RP_HEADER_NO },

    { ngx_string("no-referrer-when-downgrade"),
      NGX_HTTP_RP_HEADER_DOWNGRADE },

    { ngx_string("same-origin"),
      NGX_HTTP_RP_HEADER_SAME_ORIGIN },

    { ngx_string("origin"),
      NGX_HTTP_RP_HEADER_ORIGIN },

    { ngx_string("strict-origin"),
      NGX_HTTP_RP_HEADER_STRICT_ORIGIN },

    { ngx_string("origin-when-cross-origin"),
      NGX_HTTP_RP_HEADER_ORIGIN_WHEN_CROSS },

    { ngx_string("unsafe-url"),
      NGX_HTTP_RP_HEADER_UNSAFE_URL },

    { ngx_string("omit"),
      NGX_HTTP_SECURITY_HEADER_OMIT },

    { ngx_null_string, 0 }
};

static ngx_int_t ngx_http_security_headers_filter(ngx_http_request_t *r);
static void *ngx_http_security_headers_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_security_headers_merge_loc_conf(ngx_conf_t *cf,
    void *parent, void *child);
static ngx_int_t ngx_http_security_headers_init(ngx_conf_t *cf);
static ngx_int_t ngx_set_headers_out_by_search(ngx_http_request_t *r,
    ngx_str_t *key, ngx_str_t *value);

ngx_str_t  ngx_http_security_headers_default_text_types[] = {
    ngx_string("text/html"),
    ngx_string("application/xhtml+xml"),
    ngx_string("text/xml"),
    ngx_string("text/plain"),
    ngx_null_string
};

static ngx_command_t  ngx_http_security_headers_commands[] = {

    { ngx_string( "security_headers" ),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof( ngx_http_security_headers_loc_conf_t, enable ),
      NULL },

    { ngx_string( "hide_server_tokens" ),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_security_headers_loc_conf_t, hide_server_tokens ),
      NULL },

    { ngx_string( "security_headers_hsts_preload" ),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_security_headers_loc_conf_t, hsts_preload ),
      NULL },

    { ngx_string("security_headers_xss"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_enum_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_security_headers_loc_conf_t, xss),
      ngx_http_xss_protection },

     { ngx_string("security_headers_frame"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_enum_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_security_headers_loc_conf_t, fo),
      ngx_http_frame_options },

    { ngx_string("security_headers_referrer_policy"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_enum_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_security_headers_loc_conf_t, rp),
      ngx_http_referrer_policy },

    { ngx_string("security_headers_text_types"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
      ngx_http_types_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_security_headers_loc_conf_t, text_types_keys),
      &ngx_http_security_headers_default_text_types[0] },

      ngx_null_command
};


static ngx_http_module_t  ngx_http_security_headers_module_ctx = {
    NULL,                                  /* preconfiguration */
    ngx_http_security_headers_init,        /* postconfiguration */

    NULL,                                  /* create main configuration */
    NULL,                                  /* init main configuration */

    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */

    ngx_http_security_headers_create_loc_conf, /* create location config */
    ngx_http_security_headers_merge_loc_conf     /* merge location config */
};


ngx_module_t  ngx_http_security_headers_module = {
    NGX_MODULE_V1,
    &ngx_http_security_headers_module_ctx,       /* module context */
    ngx_http_security_headers_commands,          /* module directives */
    NGX_HTTP_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


/* next header filter in chain */

static ngx_http_output_header_filter_pt  ngx_http_next_header_filter;

/* header filter handler */

static ngx_int_t
ngx_http_security_headers_filter(ngx_http_request_t *r)
{
    ngx_http_security_headers_loc_conf_t  *slcf;

    ngx_table_elt_t   *h_server;

    ngx_str_t   key;
    ngx_str_t   val;

    ngx_str_t  scheme          = ngx_string("scheme");
    ngx_uint_t scheme_hash_key = ngx_hash_key(scheme.data, scheme.len);
    ngx_http_variable_value_t *scheme_value;

    slcf = ngx_http_get_module_loc_conf(r, ngx_http_security_headers_module);

    if (1 == slcf->hide_server_tokens) {
        /* Hide the Server header */
        h_server = r->headers_out.server;
        if (h_server == NULL) {
            h_server = ngx_list_push(&r->headers_out.headers);
            if (h_server == NULL) {
                return NGX_ERROR;
            }
            ngx_str_set(&h_server->key, "Server");
            ngx_str_set(&h_server->value, "");
            r->headers_out.server = h_server;
        }
        h_server->hash = 0;

        size_t hide_headers_count = sizeof(hide_headers) / sizeof(hide_headers[0]);

        for (size_t i = 0; i < hide_headers_count; ++i) {
            ngx_set_headers_out_by_search(r, &hide_headers[i], &empty_val);
        }
    }

    if (1 != slcf->enable) {
        return ngx_http_next_header_filter(r);
    }

    /* add X-Content-Type-Options to output */
    if (r->headers_out.status == NGX_HTTP_OK) {
        ngx_str_set(&key, "X-Content-Type-Options");
        ngx_str_set(&val, "nosniff");

        ngx_set_headers_out_by_search(r, &key, &val);
    }

    /* Add X-XSS-Protection */
    if (r->headers_out.status != NGX_HTTP_NOT_MODIFIED
        && NGX_HTTP_SECURITY_HEADER_OMIT != slcf->xss
        && ngx_http_test_content_type(r, &slcf->text_types) != NULL)
    {

        switch (slcf->xss) {
            case NGX_HTTP_XSS_HEADER_ON:
                ngx_str_set(&val, "1");
                break;
            case NGX_HTTP_XSS_HEADER_BLOCK:
                ngx_str_set(&val, "1; mode=block");
                break;
            case NGX_HTTP_XSS_HEADER_OFF:
                ngx_str_set(&val, "0");
                break;
            default:
                val.len = 0;
                val.data = NULL;
        }

        if (val.data) {
            ngx_str_set(&key, "X-XSS-Protection");
            ngx_set_headers_out_by_search(r, &key, &val);
        }
    }

    scheme_value = ngx_http_get_variable(r, &scheme, scheme_hash_key);
    if (scheme_value && !scheme_value->not_found && scheme_value->len == 5 && ngx_strncmp(scheme_value->data, "https", 5) == 0)
    {
        ngx_str_set(&key, "Strict-Transport-Security");
        if (1 == slcf->hsts_preload) {
            ngx_str_set(&val, "max-age=31536000; includeSubDomains; preload");
        } else {
            ngx_str_set(&val, "max-age=31536000; includeSubDomains");
        }
        ngx_set_headers_out_by_search(r, &key, &val);
    }

    /* Add X-Frame-Options */
    if (r->headers_out.status != NGX_HTTP_NOT_MODIFIED
        && NGX_HTTP_SECURITY_HEADER_OMIT != slcf->fo
        && ngx_http_test_content_type(r, &slcf->text_types) != NULL)
    {

        switch (slcf->fo) {
            case NGX_HTTP_FO_HEADER_SAME:
                ngx_str_set(&val, "SAMEORIGIN");
                break;
            case NGX_HTTP_FO_HEADER_DENY:
                ngx_str_set(&val, "DENY");
                break;
            default:
                val.len = 0;
                val.data = NULL;
        }

        if (val.data) {
            ngx_str_set(&key, "X-Frame-Options");
            ngx_set_headers_out_by_search(r, &key, &val);
        }
    }

    /* Referrer-Policy: no-referrer-when-downgrade */
    if (r->headers_out.status != NGX_HTTP_NOT_MODIFIED
        && NGX_HTTP_SECURITY_HEADER_OMIT != slcf->rp) {

            switch (slcf->rp) {
                case NGX_HTTP_RP_HEADER_NO:
                    ngx_str_set(&val, "no-referrer");
                    break;
                case NGX_HTTP_RP_HEADER_DOWNGRADE:
                    ngx_str_set(&val, "no-referrer-when-downgrade");
                    break;
                case NGX_HTTP_RP_HEADER_SAME_ORIGIN:
                    ngx_str_set(&val, "same-origin");
                    break;
                case NGX_HTTP_RP_HEADER_ORIGIN:
                    ngx_str_set(&val, "origin");
                    break;
                case NGX_HTTP_RP_HEADER_STRICT_ORIGIN:
                    ngx_str_set(&val, "strict-origin");
                    break;
                case NGX_HTTP_RP_HEADER_ORIGIN_WHEN_CROSS:
                    ngx_str_set(&val, "origin-when-cross-origin");
                    break;
                case NGX_HTTP_RP_HEADER_STRICT_ORIG_WHEN_CROSS:
                    ngx_str_set(&val, "strict-origin-when-cross-origin");
                    break;
                case NGX_HTTP_RP_HEADER_UNSAFE_URL:
                    ngx_str_set(&val, "unsafe-url");
                    break;
                default:
                    val.len = 0;
                    val.data = NULL;
            }
        if (val.data) {
            ngx_str_set(&key, "Referrer-Policy");
            ngx_set_headers_out_by_search(r, &key, &val);
        }
    }



    /* proceed to the next handler in chain */
    return ngx_http_next_header_filter(r);
}


static void *
ngx_http_security_headers_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_security_headers_loc_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_security_headers_loc_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    conf->xss =    NGX_CONF_UNSET_UINT;
    conf->fo  =    NGX_CONF_UNSET_UINT;
    conf->rp  =    NGX_CONF_UNSET_UINT;
    conf->enable = NGX_CONF_UNSET;
    conf->hide_server_tokens = NGX_CONF_UNSET_UINT;
    conf->hsts_preload = NGX_CONF_UNSET_UINT;

    return conf;
}


static char *
ngx_http_security_headers_merge_loc_conf(ngx_conf_t *cf, void *parent,
    void *child)
{
    ngx_http_security_headers_loc_conf_t *prev = parent;
    ngx_http_security_headers_loc_conf_t *conf = child;

    ngx_conf_merge_value(conf->enable, prev->enable, 0);
    ngx_conf_merge_value(conf->hide_server_tokens, prev->hide_server_tokens, 0);
    ngx_conf_merge_value(conf->hsts_preload, prev->hsts_preload, 1);

    if (ngx_http_merge_types(cf, &conf->text_types_keys, &conf->text_types,
                             &prev->text_types_keys, &prev->text_types,
                             ngx_http_security_headers_default_text_types)
        != NGX_OK)
    {
        return NGX_CONF_ERROR;
    }

    ngx_conf_merge_uint_value(conf->xss, prev->xss,
                              NGX_HTTP_XSS_HEADER_OFF);
    ngx_conf_merge_uint_value(conf->fo, prev->fo,
                              NGX_HTTP_FO_HEADER_SAME);
    ngx_conf_merge_uint_value(conf->rp, prev->rp,
                              NGX_HTTP_RP_HEADER_STRICT_ORIG_WHEN_CROSS);

    return NGX_CONF_OK;
}


static ngx_int_t
ngx_http_security_headers_init(ngx_conf_t *cf)
{
    /* install handler in header filter chain */

    ngx_http_next_header_filter = ngx_http_top_header_filter;
    ngx_http_top_header_filter = ngx_http_security_headers_filter;

    return NGX_OK;
}

static ngx_int_t
ngx_set_headers_out_by_search(ngx_http_request_t *r,
    ngx_str_t *key, ngx_str_t *value)
{
    ngx_list_part_t            *part;
    ngx_uint_t                  i;
    ngx_table_elt_t            *h;
    ngx_flag_t                  matched = 0;

    part = &r->headers_out.headers.part;
    h = part->elts;

    for (i = 0; /* void */; i++) {

        if (i >= part->nelts) {
            if (part->next == NULL) {
                break;
            }

            part = part->next;
            h = part->elts;
            i = 0;
        }

        if (h[i].hash == 0) {
            continue;
        }

        if (h[i].key.len == key->len
            && ngx_strncasecmp(h[i].key.data, key->data,
                               h[i].key.len) == 0)
        {
            goto matched;
        }

        /* not matched */
        continue;
matched:

        if (value->len == 0 || matched) {
            h[i].value.len = 0;
            h[i].hash = 0;
        } else {
            h[i].value = *value;
            h[i].hash = 1;
        }

        matched = 1;
    }

    if (matched){
        return NGX_OK;
    }

    /* XXX we still need to create header slot even if the value
     * is empty because some builtin headers like Last-Modified
     * relies on this to get cleared */

    h = ngx_list_push(&r->headers_out.headers);
    if (h == NULL) {
        return NGX_ERROR;
    }

    if (value->len == 0) {
        h->hash = 0;

    } else {
        h->hash = 1;
    }

    h->key = *key;
    h->value = *value;

    h->lowcase_key = ngx_pnalloc(r->pool, h->key.len);
    if (h->lowcase_key == NULL) {
        return NGX_ERROR;
    }

    ngx_strlow(h->lowcase_key, h->key.data, h->key.len);

    return NGX_OK;
}

