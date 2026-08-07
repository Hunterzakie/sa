/*****************************************************************************
 * autorun.h autorun interface and extension data file handling
 *****************************************************************************
 * Copyright (C) 2024 Videolabs
 *
 * Authors: Nyima Tamang <nyimasubroutine@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include <vlc_arrays.h>
#include <vlc_common.h>
#include <vlc_configuration.h>
#include <vlc_messages.h>
#include <vlc_threads.h>
#include <vlc_extensions.h>

#include <stdbool.h>
#include <string.h>

#include "misc/webservices/json.h"
#include "../misc/webservices/json.h"
#include "../misc/webservices/json_helper.h"
#include "extension.h"

struct lua_state {
    vlc_mutex_t lock; 
    vlc_atomic_rc_t rc;
    bool initialized; // protected by lock
    DECL_ARRAY(struct extension_t *) extensions; 
};

extern struct lua_state extensions_cache;



static char *getDataPath()
{
    const char *user_dir = config_GetUserDir(VLC_USERDATA_DIR);
    const char *fname = "autorun_data.json";
    char* psz_ret;

    if (asprintf(&psz_ret, "%s%s%s", user_dir, DIR_SEP, fname) == -1){
        return NULL;
    }

    return psz_ret;
}

int getCachedExtensionIdx(char const * ext_name);

void writeExtensionsCache(vlc_object_t * obj, json_value* val);

struct extension_t * createExtensionFromJson(vlc_object_t * obj, json_value* val);

/*
 * reads from save file and load extensions into cache
 */
void loadExtensionsIntoCache(vlc_object_t *obj, char * psz_json);


/*
 * read in data and intialize our state
 */
void init_use_state(vlc_object_t *obj);

int AutorunStart(vlc_object_t *obj);

void AutorunStop(vlc_object_t *obj);
