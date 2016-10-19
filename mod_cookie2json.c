/* Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "apr.h"
#include "apr_lib.h"
#include "apr_strings.h"

#define APR_WANT_STRFUNC
#include "apr_want.h"

#include "httpd.h"
#include "http_config.h"
#include "http_core.h"
#include "http_request.h"

#include "http_log.h"
#include "http_main.h"
#include "http_protocol.h"
#include "http_request.h"
#include "util_script.h"
#include "http_connection.h"



/* ********************************************

    Structs & Defines

   ******************************************** */

#ifdef DEBUG                    // To print diagnostics to the error log
#define _DEBUG 1                // enable through gcc -DDEBUG
#else
#define _DEBUG 0
#endif


// mapping from ascii position to T/F allows [.0-9A-Z_a-z] see: http://www.asciitable.com/
static const char valid_callback_char_table[] = {
        // null to -
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0,

        1, // .
        0, // /
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // 0-9
        0, 0, 0, 0, 0, 0, 0, // : to @

        // A - Z
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1,

        0, 0, 0, 0, // [ to ^
        1, // _
        0, // `

        // a - z
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1,

        // ( to the end
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0
};

// module configuration - this is basically a global struct
typedef struct {
    int enabled;                // module enabled?
    char *callback_name_from;   // use this query string keys value as the callback
    apr_array_header_t *cookie_prefix;
                                // query string keys that will not be set in the cookie
    apr_array_header_t *callback_prefixes;
                                // check the callback against this list if it's not empty
} settings_rec;

module AP_MODULE_DECLARE_DATA cookie2json_module;

// See here for the structure of request_rec:
// http://ci.apache.org/projects/httpd/trunk/doxygen/structrequest__rec.html
static int hook(request_rec *r)
{
    settings_rec *cfg = ap_get_module_config( r->per_dir_config,
                                              &cookie2json_module );

    /* Do not run in subrequests, don't run if not enabled */
    if( !(cfg->enabled || r->main) ) {
        return DECLINED;
    }

    // It's a HEAD request, nothing we want to return here.
    if( r->header_only ) {
        return DECLINED;
    }

    // the response body
    char *body = "";

    // ********************************
    // Parse the cookie
    // ********************************

    // See if we have any cookies being sent to us. If the client sent a single
    // cookie header with multiple values, they will be split by a ; For example:
    // Cookie: a=1; b=2
    // However, if the client sent multiple cookie headers, with a single value
    // each, they'll be split by a , For example:
    // Cookie: a=1, b=2
    // A combination of the above is also possible, so we might receive a string
    // like: a=1, b=2; c=3
    // Support all these cases.

    const char *cookie_header;
    if( (cookie_header = apr_table_get(r->headers_in, "Cookie")) ){

        _DEBUG && fprintf( stderr, "Cookie header: %s\n", cookie_header );

        // Iterate over each cookie: directive sent
        char *last_cookie;
        char *cookie = apr_strtok(
                            apr_pstrdup( r->pool, cookie_header ), ",", &last_cookie );

        // in the example of 'a=1, b=2; c=3', this gives 'a=1' and then 'b=2; c=3'
        while( cookie != NULL ) {

            // This MAY contain leading whitespace. Let's get rid of that. I'd use
            // apr_collapse_spaces, but it removes /all/ whitespace :(
            // this will return NULL if there's no match, so then just use the
            // original.
            while( isspace(*cookie) ) { cookie++; }

            _DEBUG && fprintf( stderr, "Individual cookie: %s\n", cookie );

            // protect against the pathological case where the cookie is malformed
            // and there's no length left on the cookie now.
            if( !strlen(cookie) ) {
                // And get the next cookie -- has to be done at every break
                cookie = apr_strtok( NULL, ",", &last_cookie );

                continue;
            }

            // Now, iterate over the pairs in the individual cookie directives.
            // In the example of 'b=2; c=3' this will give 'b=2' then 'c=3'
            char *last_pair;
            char *pair = apr_strtok( apr_pstrdup( r->pool, cookie ), ";", &last_pair );

            while( pair != NULL ) {

                // This MAY contain leading whitespace. Let's get rid of that. I'd use
                // apr_collapse_spaces, but it removes /all/ whitespace :(
                // this will return NULL if there's no match, so then just use the
                // original.
                while( isspace(*pair) ) { pair++; }

                _DEBUG && fprintf( stderr, "Individual pair: %s\n", pair );

                // From here on, actually process the pairs

                // length of the substr before the = sign (or index of the = sign)
                int contains_equals_at = strcspn( pair, "=" );

                // Does not contains a =, or starts with a =, meaning it's garbage
                if( !strstr(pair, "=") || contains_equals_at < 1 ) {

                    // And get the next pair -- has to be done at every break
                    pair = apr_strtok( NULL, ";", &last_pair );

                    continue;
                }

                // So this IS a key value pair. Let's get the key and the value.
                // first, get the key - everything up to the first =
                char *key   = apr_pstrndup( r->pool, pair, contains_equals_at );

                // now get the value, everything AFTER the = sign. We do that by
                // moving the pointer past the = sign.
                char *value = apr_pstrdup( r->pool, pair );
                value += contains_equals_at + 1;

                // Are you whitelisting based on prefixes? If so, let's make sure
                // this key is ok.
                // Following tutorial code here again:
                // http://xrl.us/AprTutorial
                int i;
                int is_whitelisted = 0;
                for( i = 0; i < cfg->cookie_prefix->nelts; i++ ) {

                    char *prefix = ((char **)cfg->cookie_prefix->elts)[i];

                    _DEBUG && fprintf( stderr,
                                        "checking white list prefix: %s\n", prefix );

                    //it's indeed whiteliested, we should use this value instead
                    if( strncasecmp( key, prefix, strlen(prefix) ) == 0 ) {
                        _DEBUG && fprintf( stderr,
                            "Cookie %s is white listed against %s\n", pair, prefix );

                        is_whitelisted++;
                        break;
                    }
                }

                // if there was a white list but we didn't find a match for this key,
                // we have to skip it
                if( !(apr_is_empty_array( cfg->cookie_prefix )) && !is_whitelisted ) {
                    _DEBUG && fprintf( stderr,
                        "Cookie %s is not on the whitelist - skipping\n", pair );

                    // And get the next pair -- has to be done at every break
                    pair = apr_strtok( NULL, ";", &last_pair );

                    continue;
                }


                body = apr_pstrcat( r->pool,
                            body,           // what we have so far
                             // If we already have pairs in here, we need the
                             // delimiter, otherwise we don't.
                             (strlen(body) ? ", " : "" ),
                             // Quote the key/values - could contain anything
                             "\"", key,      "\": ",
                             "\"", value,    "\"",
                            NULL
                        );

                // And get the next pair -- has to be done at every break
                pair = apr_strtok( NULL, ";", &last_pair );
            }

            // And get the next cookie -- has to be done at every break
            cookie = apr_strtok( NULL, ",", &last_cookie );
        }

    // nothing to see here, move along
    } else {
        _DEBUG && fprintf( stderr, "No cookie header present\n" );
    }

    _DEBUG && fprintf( stderr, "body will contain: %s\n", body );

    // ********************************
    // Is there a callback?
    // ********************************

    char *callback = "";

    // No callback name configured, or no query string? nothing to do here
    if( strlen(cfg->callback_name_from) && r->args && strlen( r->args ) > 1 ) {

        // Now, iterate over the pairs in the individual cookie directives.
        // In the example of 'b=2; c=3' this will give 'b=2' then 'c=3'
        char *last_pair;
        char *pair = apr_strtok( apr_pstrdup( r->pool, r->args ), "&", &last_pair );

        while( pair != NULL ) {

            _DEBUG && fprintf( stderr, "Query string pair: %s\n", pair );

            // length of the substr before the = sign (or index of the = sign)
            int contains_equals_at = strcspn( pair, "=" );

            // Does not contains a =, or starts with a =, meaning it's garbage
            if( !strstr(pair, "=") || contains_equals_at < 1 ) {

                // And get the next pair -- has to be done at every break
                pair = apr_strtok( NULL, "=", &last_pair );
                continue;
            }

            // So this IS a key value pair. Let's get the key and the value.
            // first, get the key - everything up to the first =
            char *key   = apr_pstrndup( r->pool, pair, contains_equals_at );

            // now get the value, everything AFTER the = sign. We do that by
            // moving the pointer past the = sign.
            char *value = apr_pstrdup( r->pool, pair );
            value += contains_equals_at + 1;

            _DEBUG && fprintf( stderr, "qs pair=%s, key=%s, value=%s\n",
                                        pair, key, value );

            // This might be the callback name - if so we're done
            if( cfg->callback_name_from && !(strlen( callback )) &&
                strcasecmp( key, cfg->callback_name_from ) == 0
            ) {
                _DEBUG && fprintf( stderr, "validating callback %s\n", value);

                // validate the callback to avoid script injection under some circumstances
                char *current = value;
                while (*current && valid_callback_char_table[*current]) *current++;

                // didn't find a bad char
                if( *current == '\0' ) {
                    _DEBUG && fprintf( stderr, "validating the callback %s against prefixes\n", value);

                    int i;
                    int allowed = 1; // Assume it's allowed initially
                    // now, check that it's allowed by the prefix list
                    for( i = 0; i < cfg->callback_prefixes->nelts; i++ ) {
                        allowed = 0; // There's at least one prefix, so mark not allowed until validated.

                        const char *prefix = ((char **)cfg->callback_prefixes->elts)[i];
                        _DEBUG && fprintf(stderr, "checking callback %s against prefix: %s\n", value, prefix);

                        // check if the value starts with a white-listed prefix.
                        if( strncasecmp( value, prefix, strlen(prefix) ) == 0 ) {
                            _DEBUG && fprintf( stderr, "callback %s is allowed %s\n", value, prefix );

                            allowed = 1;
                            break;
                        }
                    }

                    if (!allowed) {
                        _DEBUG && fprintf( stderr, "found disallowed callback %s in JSONP; returning 400\n", value);
                        return HTTP_BAD_REQUEST;
                    }

                    // ok, this is our callback
                    callback = value;
                    _DEBUG && fprintf(stderr, "using %s as the callback name\n", callback);
                    break;
                } else {
                    _DEBUG && fprintf( stderr, "found unsafe character %c in JSONP callback %s; returning 400\n", *current, value);
                    return HTTP_BAD_REQUEST;
                }
            }

            // And get the next pair -- has to be done at every break
            pair = apr_strtok( NULL, "&", &last_pair );
            continue;
        }
    }

    // ********************************
    // Create the response
    // ********************************

    body = apr_pstrcat( r->pool, "{ ", body, " }", NULL );

    // you want it wrapped in a callback?
    if( strlen( callback ) ) {
        body = apr_pstrcat( r->pool, callback, "({\n",
                                "  status: 200,\n",
                                "  body: ", body, "\n",
                                "});",
                                NULL );
    }

    // ********************************
    // Send back the body
    // ********************************

    // create bucket & bucket brigade, following the code in:
    // http://svn.apache.org/repos/asf/httpd/httpd/trunk/modules/generators/mod_asis.c
    apr_bucket_brigade *bucket_brigade;
    apr_bucket *bucket;
    apr_bucket *eos_bucket;

    // the connection to the client
    conn_rec *conn = r->connection;

    // create a brigade for the body we're about to return.
    bucket_brigade = apr_brigade_create( r->pool, conn->bucket_alloc );

    // XXX apr_brigade_puts / apr_brigade_writev seems the 'right' way to
    // do this, but I can't figure out what the 'flush' and 'ctx' arguments
    // to the function are supposed to be. Documentation isn't helping and
    // google doesn't offer anything useful either :(
    // http://www.apachetutor.org/dev/brigades
    // http://www.cs.virginia.edu/~jcw5q/talks/apache/bucketbrigades.ac2002.pdf

    // create a bucket for the body we're about to return.
    bucket      = apr_bucket_pool_create(
                        body,
                        (apr_size_t) (strlen(body)),
                        r->pool,
                        conn->bucket_alloc
                    );

    // note that this is end of stream - no more data after this bucket
    eos_bucket  = apr_bucket_eos_create( conn->bucket_alloc );

    // and append
    APR_BRIGADE_INSERT_TAIL( bucket_brigade, bucket );
    APR_BRIGADE_INSERT_TAIL( bucket_brigade, eos_bucket );

    // Set the content type, now that we have a working JSON response
    // This has to be done /before/ passing the brigade off.
    // Note, this can't be set using an 'apr_table_addn( r->headers_out .. )
    // because it will be overwritten by the core:
    // ./server/config.c: handler = r->content_type ? r->content_type : ap_default_type(r);
    // Instead, set it directly on r:
    r->content_type = "text/javascript";

    // pass the brigade - we're done
    apr_status_t rv;
    rv = ap_pass_brigade( r->output_filters, bucket_brigade );
    apr_brigade_cleanup( bucket_brigade );

    if (rv != APR_SUCCESS ) {
        _DEBUG && fprintf( stderr,
                       "mod_cookie2json: ap_pass_brigade failed for %s", r->uri );
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    return OK;
}

/* ********************************************

    Default settings

   ******************************************** */

/* initialize all attributes */
static void *init_settings(apr_pool_t *p, char *d)
{
    settings_rec *cfg;

    cfg = (settings_rec *) apr_pcalloc(p, sizeof(settings_rec));
    cfg->enabled                    = 0;
    cfg->callback_name_from         = "";
    cfg->cookie_prefix              = apr_array_make(p, 2, sizeof(const char*) );
    cfg->callback_prefixes          = apr_array_make(p, 2, sizeof(const char*) );

    return cfg;
}

/* Set the value of a config variabe, strings only */
static const char *set_config_value(cmd_parms *cmd, void *mconfig,
                                    const char *value)
{
    settings_rec *cfg;

    cfg = (settings_rec *) mconfig;

    char name[50];
    sprintf( name, "%s", cmd->cmd->name );

    /*
     * Apply restrictions on attributes.
     */
    if( strlen(value) == 0 ) {
        return apr_psprintf(cmd->pool, "%s not allowed to be NULL", name);
    }

    /* Use this query string argument for the cookie name */
    if( strcasecmp(name, "C2JSONCallBackNameFrom") == 0 ) {
        cfg->callback_name_from = apr_pstrdup(cmd->pool, value);

    /* all the keys that will not be put into the cookie */
    } else if( strcasecmp(name, "C2JSONPrefix") == 0 ) {

        // following tutorial here:
        // http://dev.ariel-networks.com/apr/apr-tutorial/html/apr-tutorial-19.html
        const char *str                                   = apr_pstrdup(cmd->pool, value);
        *(const char**)apr_array_push(cfg->cookie_prefix) = str;

        _DEBUG && fprintf( stderr, "prefix white list as str = %s\n", apr_array_pstrcat( cmd->pool, cfg->cookie_prefix, '-' ) );

    /* callback param will be validated against this */
    } else if( strcasecmp(name, "C2JSONCallBackPrefix") == 0 ) {
        const char *str                                       = apr_pstrdup(cmd->pool, value);
        *(const char**)apr_array_push(cfg->callback_prefixes) = str;

        _DEBUG && fprintf( stderr, "callback prefix list as str = %s\n", apr_array_pstrcat( cmd->pool, cfg->callback_prefixes, '-' ) );
    } else {
        return apr_psprintf(cmd->pool, "No such variable %s", name);
    }

    return NULL;
}

/* Set the value of a config variabe, ints/booleans only */
static const char *set_config_enable(cmd_parms *cmd, void *mconfig,
                                    int value)
{
    settings_rec *cfg;

    cfg = (settings_rec *) mconfig;

    char name[50];
    sprintf( name, "%s", cmd->cmd->name );

    if( strcasecmp(name, "C2JSON") == 0 ) {
        cfg->enabled           = value;

    } else {
        return apr_psprintf(cmd->pool, "No such variable %s", name);
    }

    return NULL;
}

/* ********************************************

    Configuration options

   ******************************************** */

static const command_rec commands[] = {
    AP_INIT_FLAG( "C2JSON",                     set_config_enable,  NULL, OR_FILEINFO,
                  "whether or not to enable querystring to cookie module"),
    AP_INIT_TAKE1("C2JSONCallBackNameFrom",     set_config_value,   NULL, OR_FILEINFO,
                  "the callback name will come from this query paramater"),
    AP_INIT_ITERATE("C2JSONCallBackPrefix",     set_config_value,   NULL, OR_FILEINFO,
                  "the callback name will be validated against these prefixes"),
    AP_INIT_ITERATE("C2JSONPrefix",             set_config_value,   NULL, OR_FILEINFO,
                  "only cookies whose key matches these prefixes will be returned" ),
    {NULL}
};

/* ********************************************

    Register module to Apache

   ******************************************** */

static void register_hooks(apr_pool_t *p)
{   // Because this is a /handler/, be sure to use ap_hook_handler, and not
    // ap_hook_fixups: http://www.apachetutor.org/dev/request
    ap_hook_handler( hook, NULL, NULL, APR_HOOK_MIDDLE );
}

module AP_MODULE_DECLARE_DATA cookie2json_module = {
    STANDARD20_MODULE_STUFF,
    init_settings,              /* dir config creator */
    NULL,                       /* dir merger --- default is to override */
    NULL,                       /* server config */
    NULL,                       /* merge server configs */
    commands,                   /* command apr_table_t */
    register_hooks              /* register hooks */
};


