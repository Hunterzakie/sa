/*****************************************************************************
 * generate_medialib.c : medialib generation interface for vlc
 *****************************************************************************
 * Copyright (C) 2025 VLC authors and VideoLAN
 * Copyright (C) 2025      Videolabs
 *
 * Author: Wassim Lalaoui <wassim@videolabs.io>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
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

/*****************************************************************************
 * Preamble
 *****************************************************************************/
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_plugin.h>

#include <vlc_interface.h>
#include <vlc_url.h>

#include <vlc_media_library.h>

/*****************************************************************************
 * intf_sys_t: description
 *****************************************************************************/

struct intf_sys_t
{
    vlc_thread_t    thread;
    bool     scan_completed;
    bool should_exit;

    vlc_mutex_t     lock;
    vlc_cond_t      wait;

    vlc_medialibrary_t *p_ml;
    vlc_ml_event_callback_t *p_callback;
};

static void on_ml_event(void *data, const vlc_ml_event_t *event)
{
    intf_thread_t *intf = data;
    intf_sys_t *sys = intf->p_sys;

    switch (event->i_type)
    {
        case VLC_ML_EVENT_BACKGROUND_IDLE_CHANGED:
            if (event->background_idle_changed.b_idle)
            {
                msg_Info(intf, "Medialibrary scan completed!");
                vlc_mutex_lock(&sys->lock);
                sys->scan_completed = true;
                vlc_cond_signal(&sys->wait);
                vlc_mutex_unlock(&sys->lock);
            }
            else
            {
                msg_Info(intf, "Medialibrary scanning...");
            }
            break;

        case VLC_ML_EVENT_PARSING_PROGRESS_UPDATED:
            msg_Info(intf, "Parsing progress: %d%%", event->parsing_progress.i_percent);
            break;
            
        default:
            break;
    }
}

static void *Run(void *data)
{
    vlc_thread_set_name("vlc-gen-ml");

    intf_thread_t *intf = data;
    intf_sys_t *sys = intf->p_sys;

    char *folders_str = var_InheritString(intf, "scan-folders");
    if (!folders_str || !*folders_str) {
        msg_Err(intf, "No folders specified. Use --scan-folders option.");
        free(folders_str);
        goto quit;
    }

    msg_Info(intf, "Using custom folders: %s", folders_str);

    bool success = true;
    char *saveptr;
    char *folder = strtok_r(folders_str, ",", &saveptr);
    while (folder) 
    {
        char *folder_uri = vlc_path2uri(folder, "file");
        if (!folder_uri) 
        {
            msg_Err(intf, "Failed to convert folder to URI: %s", folder);
            success = false;
            break;
        }

        msg_Info(intf, "Adding folder to medialibrary: %s", folder);
        int ret = vlc_ml_add_folder(sys->p_ml, folder_uri);
        free(folder_uri);
        
        if (ret != VLC_SUCCESS) 
        {
            msg_Err(intf, "Failed to add folder: %s (error: %d)", folder, ret);
            success = false;
            break;
        }

        folder = strtok_r(NULL, ",", &saveptr);
    }
    free(folders_str);

    if (!success) 
    {
        msg_Err(intf, "Failed to add one or more folders");
        goto quit;
    }
    msg_Info(intf, "Folders added successfully, scanning will start...");
    vlc_mutex_lock(&sys->lock);
    while (!sys->scan_completed && !sys->should_exit)
    {
        vlc_cond_wait(&sys->wait, &sys->lock);
    }
    if (sys->should_exit)
        msg_Info(intf, "Medialibrary generation cancelled");
    else
        msg_Info(intf, "Medialibrary generation completed");
    vlc_mutex_unlock(&sys->lock);

quit:
    libvlc_Quit(vlc_object_instance(intf));
    return NULL;
}

/*****************************************************************************
 * Open: initialize and create window
 *****************************************************************************/
static int Open(vlc_object_t *p_this)
{
    intf_thread_t  *intf = (intf_thread_t *)p_this;
    intf_sys_t *sys = intf->p_sys = vlc_obj_calloc(intf, 1, sizeof(intf_sys_t));

    if (!sys)
        return VLC_ENOMEM;

    vlc_mutex_init(&sys->lock);
    vlc_cond_init(&sys->wait);

    sys->p_ml = vlc_ml_instance_get(intf);
    if (!sys->p_ml) {
        msg_Err(intf, "Media library isn't available.");
        return VLC_EGENERIC;
    }

    msg_Info(intf, "Media library instance obtained successfully");

    sys->p_callback = vlc_ml_event_register_callback(sys->p_ml, on_ml_event, intf);
    if (!sys->p_callback) 
    {
        msg_Err(intf, "Failed to register medialibrary callback");
        return VLC_ENOMEM;
    }

    if (vlc_clone(&sys->thread, Run, intf))
        return VLC_ENOMEM;

    return VLC_SUCCESS;
}

/*****************************************************************************
 * Close: destroy interface window
 *****************************************************************************/
static void Close(vlc_object_t *p_this)
{
    msg_Info(p_this, "Medialibrary generation interface closed.\n");
    intf_thread_t *intf = (intf_thread_t *)p_this;
    intf_sys_t *sys = intf->p_sys;

    vlc_mutex_lock(&sys->lock);
    sys->should_exit = true;
    vlc_cond_signal(&sys->wait);
    vlc_mutex_unlock(&sys->lock);

    vlc_join(sys->thread, NULL);

    if (sys->p_ml && sys->p_callback)
        vlc_ml_event_unregister_callback(sys->p_ml, sys->p_callback);
}

/*****************************************************************************
 * Module descriptor
 *****************************************************************************/

vlc_module_begin ()
    set_shortname("generate_medialib")
    set_description(N_("Medialib generation interface"))
    set_capability("interface", 0)
    set_subcategory(SUBCAT_INTERFACE_MAIN)
    set_callbacks(Open, Close)
    add_shortcut("medialib_gen")
    add_string("scan-folders", NULL, "Folders to scan",
               "Comma-separated list of folders to add to media library")
vlc_module_end ()