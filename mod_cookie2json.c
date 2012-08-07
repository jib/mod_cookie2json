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

#include <math.h>

/* ********************************************

    Structs & Defines

   ******************************************** */

#ifdef DEBUG                    // To print diagnostics to the error log
#define _DEBUG 1                // enable through gcc -DDEBUG
#else
#define _DEBUG 0
#endif


// module configuration - this is basically a global struct
typedef struct {
    int enabled;                // module enabled?
    char *callback_name_from;   // use this query string keys value as the callback
    apr_array_header_t *cookie_prefix;
                                // query string keys that will not be set in the cookie
} settings_rec;

module AP_MODULE_DECLARE_DATA querystring2cookie_module;

// See here for the structure of request_rec:
// http://ci.apache.org/projects/httpd/trunk/doxygen/structrequest__rec.html
static int hook(request_rec *r)
{
    settings_rec *cfg = ap_get_module_config( r->per_dir_config,
                                              &querystring2cookie_module );

    /* Do not run in subrequests, don't run if not enabled */
    if( !(cfg->enabled || r->main) ) {
        return DECLINED;
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
    cfg->cookie_prefix              = apr_array_make(p, 2, sizeof(const char*) );

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


        char *ary = apr_array_pstrcat( cmd->pool, cfg->cookie_prefix, '-' );
        _DEBUG && fprintf( stderr, "prefix white list as str = %s\n", ary );

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
    AP_INIT_ITERATE( "C2JSONPrefix",            set_config_value,   NULL, OR_FILEINFO,
                  "Only cookies whose key matches these prefixes will be returned" ),
    {NULL}
};

/* ********************************************

    Register module to Apache

   ******************************************** */

static void register_hooks(apr_pool_t *p)
{   /* code gets skipped if modules return a status code from
       their fixup hooks, so be sure to run REALLY first. See:
       http://svn.apache.org/viewvc?view=revision&revision=1154620
    */
    ap_hook_fixups( hook, NULL, NULL, APR_HOOK_REALLY_FIRST );
}


module AP_MODULE_DECLARE_DATA querystring2cookie_module = {
    STANDARD20_MODULE_STUFF,
    init_settings,              /* dir config creater */
    NULL,                       /* dir merger --- default is to override */
    NULL,                       /* server config */
    NULL,                       /* merge server configs */
    commands,                   /* command apr_table_t */
    register_hooks              /* register hooks */
};


