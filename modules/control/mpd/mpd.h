/*****************************************************************************
 * mpd.h
 *****************************************************************************
 * Copyright © 2022 VLC authors and VideoLAN
 *
 * Author: Lucas Ransan <lucas@ransan.tk>
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

#ifndef VLC_MPD_H
#define VLC_MPD_H

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdbool.h>

#ifdef HAVE_POLL_H
# include <poll.h>
#endif

#include <vlc_common.h>
#include <vlc_interface.h>
#include <vlc_vector.h>
#include <vlc_player.h>
#include <vlc_playlist.h>
#include <vlc_media_library.h>
#include <vlc_list.h>

typedef struct mpd_client_t {
    struct VLC_VECTOR(char) recvbuf;
    struct VLC_VECTOR(char) sendbuf;

    /* Index of this client's pfd in sys->fds.
       We can't use a pointer to it because its location isn't stable
       (vlc_vector_remove will memmove, vlc_vector_push will realloc).
       The index isn't stable either; it decreases when we remove a
       pfd before it. */
    size_t pfd;

    enum in_command_list {
        MPD_CL_NO,
        MPD_CL_YES, /* command_list_begin */
        MPD_CL_OK_YES, /* command_list_ok_begin */
    } in_command_list;

    const char *current_command;
    int command_number_in_list;

    unsigned idle_events;
    unsigned idle_events_waiting_for;
    bool idle;

    struct vlc_list node;
} mpd_client_t;

typedef struct mpd_playlist_item_t {
    uint64_t id;
    int32_t playlist_version;
} mpd_playlist_item_t;

/* MPD uses a single entry point called music directory, therefore
   clients expect relative paths to this entry point.
   As there can be multiple entry point in the media library, we
   generate a unique name for each of them and use those names as
   directories in the music directory.
   The unique name is composed of the name of the directory, and if
   two entry points have the same directory name, a discriminatory path
   is appended with chevrons (slashes are replaced with pipes).

   Example: the user has two music directories with the same name,
   `/home/user/music' and `/mnt/music'.
   In the MPD protocol, they will have the names
   `music<file:|||home|user|music>' and `music<file:|||mnt|music>'.

   TODO: make the discrimitant shorter. */
typedef struct mpd_entry_point_t {
    char *mrl;
    char *name;
} mpd_entry_point_t;

typedef struct VLC_VECTOR(mpd_entry_point_t) mpd_entry_point_vec_t;

/* Messages passed through intf_sys_t.pipe */
enum {
    MPD_WAKE_UP,
    MPD_STOP_SERVER,
    /* Expects an uint16_t to be sent containing the changed subsystems */
    MPD_CHANGED_SUBSYSTEMS,
    MPD_GEN_ML_ENTRY_POINTS,
};

typedef struct intf_sys_t {
    vlc_thread_t thread;

    struct VLC_VECTOR(struct pollfd) fds;
    struct vlc_list clients;

    int *sockv;
    int pipe[2];

    vlc_player_t *player;
    vlc_playlist_t *playlist;
    vlc_medialibrary_t *medialibrary;

    vlc_player_listener_id *player_listener;
    vlc_player_aout_listener_id *player_aout_listener;
    vlc_playlist_listener_id *playlist_listener;
    vlc_ml_event_callback_t *ml_event_callback;

    vlc_mutex_t pl_lock;
    int32_t playlist_version;
    /* Associates playlist songs to the playlist
       version on which they have been added. */
    struct VLC_VECTOR(mpd_playlist_item_t) pl;

    vlc_mutex_t ep_lock;
    /* TODO: use better data structure (hashmap/binary tree) */
    mpd_entry_point_vec_t entry_points;
} intf_sys_t;

void write_to_client_buffer(intf_thread_t *, mpd_client_t *, const char *);

void mpd_idle_events_update(intf_thread_t *intf, int event);
/* Tells the main thread to call mpd_idle_events_update */
void mpd_send_idle_events_update(intf_thread_t *intf, int event);

/* Convert from and to a relative path, taking into account entry
   points (see mpd_entry_point_t).

   Example: "file:///home/user/music/Some%20artist/Album/"
        <-> "music<file:|||home|user|music>/Some artist/Album" */
char *mpd_mrl2path(intf_thread_t *intf, const char *mrl);
char *mpd_path2mrl(intf_thread_t *intf, const char *path);

#endif
