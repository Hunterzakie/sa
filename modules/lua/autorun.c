/*****************************************************************************
 * autorun.c autorun interface and extension data file handling
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

#include "autorun.h"
#include "vlc_threads.h"

struct lua_state extensions_cache;
struct ext_key{
    char** ppsz;
    void* p_data;
    char const* name;
    json_type jtype;
};

char const * psz_vlsub_json = "{"
"\"autorun\": true,"
"\"capabilites\": 5,"
"\"name\": \"/home/nt/Documents/cleanvlc/share/lua/extensions/VLSub.lua\","
"\"title\": \"Dummy Autorun\","
"\"author\": \"Actually Vlsub in disguise\","
"\"version\": \"0.11.1\","
"\"url\": \"https://www.opensubtitles.org/\","
"\"description\": \"Download subtitles from OpenSubtitles.org\","
"\"shortdescription\": \"Dummy Autorun\","
"\"icondata\": \"(null)\","
"\"icondatasize\": 0,"
"\"timestamp\": \"1723670489\","
"}";

//TODO: moved to shared file between extension.c  
/** Watch timer callback
 * The timer expired, Lua may be stuck, ask the user what to do now
 **/
static void WatchTimerCallback( void *data )
{
    extension_t *p_ext = data;
    struct lua_extension *sys = p_ext->p_sys;
    extensions_manager_t *p_mgr = sys->p_mgr;

    vlc_mutex_lock(&sys->command_lock);

    for( struct command_t *cmd = sys->command;
         cmd != NULL;
         cmd = cmd->next )
        if( cmd->i_command == CMD_DEACTIVATE )
        {   /* We have a pending Deactivate command... */
            if (sys->p_progress_id != NULL)
            {
                vlc_dialog_release(p_mgr, sys->p_progress_id);
                sys->p_progress_id = NULL;
            }
            KillExtension(p_ext);
            vlc_mutex_unlock(&sys->command_lock);
            return;
        }

    if (sys->p_progress_id == NULL)
    {
        sys->p_progress_id =
            vlc_dialog_display_progress( p_mgr, true, 0.0,
                                         _( "Yes" ),
                                         _( "Extension not responding!" ),
                                         _( "Extension '%s' does not respond.\n"
                                         "Do you want to kill it now? " ),
                                         p_ext->psz_title );
        if (sys->p_progress_id == NULL)
        {
            KillExtension(p_ext);
            vlc_mutex_unlock(&sys->command_lock);
            return;
        }
        vlc_timer_schedule(sys->timer, false, VLC_TICK_FROM_MS(100),
                           VLC_TIMER_FIRE_ONCE);
    }
    else
    {
        if (vlc_dialog_is_cancelled(p_mgr, sys->p_progress_id))
        {
            vlc_dialog_release(p_mgr, sys->p_progress_id);
            sys->p_progress_id = NULL;
            KillExtension(p_ext);
            vlc_mutex_unlock(&sys->command_lock);
            return;
        }
        vlc_timer_schedule(sys->timer, false, VLC_TICK_FROM_MS(100),
                           VLC_TIMER_FIRE_ONCE);
    }
    vlc_mutex_unlock(&sys->command_lock);
}

struct extension_t * createExtensionFromJson(vlc_object_t * obj, json_value* val){
    //TODO: change to array
    assert (val->type == json_object);
    struct extension_t * p_ext = calloc(1, sizeof(*p_ext));
    struct lua_extension * sys = p_ext->p_sys = calloc(1, sizeof(*sys));

    /* Watch timer */
    if( vlc_timer_create( &sys->timer, WatchTimerCallback, p_ext ) )
    {
        free( p_ext->psz_name );
        free(sys);
        free( p_ext );
        return 0;
    }

    /* Mutexes and conditions */
    vlc_mutex_init(&sys->command_lock);
    vlc_mutex_init(&sys->running_lock);
    vlc_cond_init(&sys->wait);

    struct ext_key keys[] = {
        {.ppsz=&p_ext->psz_name, .name="name", .jtype=json_string},
        {.ppsz=&p_ext->psz_title,.name="title",.jtype=json_string},
        {.ppsz=&p_ext->psz_author,.name="author",.jtype= json_string},
        {.ppsz=&p_ext->psz_version,.name="version",.jtype=json_string},
        {.ppsz=&p_ext->psz_url,.name="url",.jtype= json_string},
        {.ppsz=&p_ext->psz_description,.name="description",.jtype= json_string},
        {.ppsz=&p_ext->psz_shortdescription,.name="shortdescription",.jtype=json_string },
        {.p_data=&p_ext->i_icondata_size,.name="icondatasize",.jtype= json_integer },
        {.p_data=&sys->i_capabilities,.name="capabilites",.jtype=json_integer},
        {.p_data=&sys->b_autorun,.name="autorun",.jtype=json_boolean},
        {.p_data=&sys->last_saved,.name="timestamp",.jtype=json_string},
    };

    for (size_t i = 0; i < ARRAY_SIZE(keys); i++){
        if (strcmp(keys[i].name, "timestamp") == 0){
            char *endptr;
            char *psz_timestamp = json_dupstring(val, "timestamp");
            assert(psz_timestamp != NULL);

            time_t timestamp = strtol(psz_timestamp, &endptr, 10);
            if (*endptr != '\0') {
                msg_Dbg(obj, "autorun: failed creating extension from json, invalid time stamp '%s' \n", psz_timestamp);
                return NULL;
            }
            sys->last_saved = timestamp;
        } else if (strcmp(keys[i].name, "autorun") == 0){
            const json_value * key_val  = json_getbyname(val, keys[i].name);
            assert(key_val != NULL);

            vlc_mutex_lock(&sys->command_lock);
            *(bool*)keys[i].p_data = key_val->u.boolean;
            vlc_mutex_unlock(&sys->command_lock);
        /* Default Operations */
        } else if (keys[i].jtype == json_string){
            *keys[i].ppsz = json_dupstring(val, keys[i].name);
        }else if (keys[i].jtype == json_integer){
            const json_value * key_val  = json_getbyname(val, keys[i].name);
            assert(key_val != NULL);

            *(int*)keys[i].p_data = key_val->u.integer;
        }
    }

    if (p_ext->psz_name == NULL || p_ext->psz_title == NULL || p_ext->psz_author == NULL){
        return NULL;
    }
    
    msg_Dbg(obj, "autorun: extension %s was created", p_ext->psz_title);
    return p_ext;
}

int getCachedExtensionIdx(char const * ext_name){
    extension_t* p_ext;
    size_t i = 0;

    ARRAY_FOREACH(p_ext, extensions_cache.extensions){
        if (strcmp(p_ext->psz_name, ext_name) == 0)
            return i;
        i++;
    }
    return -1;
}

void loadExtensionsCache(vlc_object_t *obj, char * psz_json){
    assert(&extensions_cache.lock);
    size_t psz_exts_len = strlen(psz_json);
    assert(psz_exts_len != 0);
    
    json_value *val = json_parse(psz_json, psz_exts_len); 
    assert (val != NULL);

    // load each extension into cache
    extension_t *ext = createExtensionFromJson(obj, val);
    char const* psz_ext_name = jsongetstring(val, "name");

    //skip ext
    if (ext == NULL){
        msg_Info(obj, "autorun: failed to create %s, skipping.", psz_ext_name);
        return;
    }

    ARRAY_APPEND(extensions_cache.extensions, ext);
}



void init_use_state(vlc_object_t *obj){
    vlc_mutex_assert(&extensions_cache.lock);
    const char * path = getDataPath();
    assert(path != NULL);

    if (!extensions_cache.initialized){
        vlc_atomic_rc_init(&extensions_cache.rc);
        // TODO: temporary, replace with file content string
        loadExtensionsCache(obj, psz_vlsub_json);
        extensions_cache.initialized = true; 
    }else {
        vlc_atomic_rc_inc(&extensions_cache.rc);
    }
}


int AutorunStart(vlc_object_t *obj){
    msg_Dbg(obj,"Starting Auto Run interface");

    vlc_mutex_lock(&extensions_cache.lock);
    init_use_state(obj);
    vlc_mutex_unlock(&extensions_cache.lock);

    return VLC_SUCCESS;
}

void AutorunStop(vlc_object_t *obj){
    if (vlc_atomic_rc_dec(&extensions_cache.rc)){
        // free state
    }
}