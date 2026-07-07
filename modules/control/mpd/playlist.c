/*****************************************************************************
 * playlist.c
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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "playlist.h"
#include "protocol.h"

/* The callbacks in here should only touch at locked fields in
 * intf_sys_t and call mpd_send_idle_events_update, because they can
 * be called by any thread.
 */

static void player_on_state_changed(vlc_player_t *player, enum vlc_player_state state, void *data) {
    VLC_UNUSED(player);
    VLC_UNUSED(state);
    mpd_send_idle_events_update(data, MPD_SUBSYSTEM_PLAYER);
}

static void player_on_current_media_changed(vlc_player_t *player, input_item_t *new_media, void *data) {
    VLC_UNUSED(player);
    VLC_UNUSED(new_media);
    mpd_send_idle_events_update(data, MPD_SUBSYSTEM_PLAYLIST);
    mpd_send_idle_events_update(data, MPD_SUBSYSTEM_PLAYER);
}

static void player_on_error_changed(vlc_player_t *player, enum vlc_player_error error, void *data) {
    VLC_UNUSED(player);
    VLC_UNUSED(error);
    mpd_send_idle_events_update(data, MPD_SUBSYSTEM_PLAYER);
}

static void player_on_track_list_changed(vlc_player_t *player, enum vlc_player_list_action action, const struct vlc_player_track *track, void *data) {
    VLC_UNUSED(player);
    VLC_UNUSED(action);
    VLC_UNUSED(track);
    mpd_send_idle_events_update(data, MPD_SUBSYSTEM_PLAYLIST);
}

static void player_on_titles_changed(vlc_player_t *player, vlc_player_title_list *titles, void *data) {
    VLC_UNUSED(player);
    VLC_UNUSED(titles);
    mpd_send_idle_events_update(data, MPD_SUBSYSTEM_PLAYER);
}

static void player_on_volume_changed(audio_output_t *aout, float new_volume, void *data) {
    VLC_UNUSED(aout);
    VLC_UNUSED(new_volume);

    mpd_send_idle_events_update(data, MPD_SUBSYSTEM_MIXER);
}

static void player_on_mute_changed(audio_output_t *aout, bool new_muted, void *data) {
    VLC_UNUSED(aout);
    VLC_UNUSED(new_muted);

    mpd_send_idle_events_update(data, MPD_SUBSYSTEM_MIXER);
}

static void player_on_device_changed(audio_output_t *aout, const char *device, void *data) {
    VLC_UNUSED(aout);
    VLC_UNUSED(device);

    mpd_send_idle_events_update(data, MPD_SUBSYSTEM_OUTPUT);
}

static void playlist_on_items_reset(vlc_playlist_t *playlist, vlc_playlist_item_t *const items[], size_t count, void *data) {
    VLC_UNUSED(playlist);

    intf_thread_t *intf = data;
    intf_sys_t *sys = intf->p_sys;

    msg_Dbg(intf, "playlist_on_items_reset");

    vlc_mutex_lock(&sys->pl_lock);
    sys->playlist_version++;
    vlc_vector_clear(&sys->pl);
    if (count > 0) {
        vlc_vector_insert_hole(&sys->pl, 0, count);
        for (size_t i = 0; i < count; i++) {
            sys->pl.data[i] = (mpd_playlist_item_t){
                .id = vlc_playlist_item_GetId(items[i]),
                .playlist_version = sys->playlist_version,
            };
        }
    }
    vlc_mutex_unlock(&sys->pl_lock);

    mpd_send_idle_events_update(intf, MPD_SUBSYSTEM_PLAYLIST);
}

static void playlist_on_items_added(vlc_playlist_t *playlist, size_t index, vlc_playlist_item_t *const items[], size_t count, void *data) {
    VLC_UNUSED(playlist);

    intf_thread_t *intf = data;
    intf_sys_t *sys = intf->p_sys;

    msg_Dbg(intf, "playlist_on_items_added");

    vlc_mutex_lock(&sys->pl_lock);
    sys->playlist_version++;
    vlc_vector_insert_hole(&sys->pl, index, count);
    for (size_t i = 0; i < count; i++) {
        sys->pl.data[index + i] = (mpd_playlist_item_t){
            .id = vlc_playlist_item_GetId(items[i]),
            .playlist_version = sys->playlist_version,
        };
    }
    vlc_mutex_unlock(&sys->pl_lock);

    mpd_send_idle_events_update(intf, MPD_SUBSYSTEM_PLAYLIST);
}

static void playlist_on_items_moved(vlc_playlist_t *playlist, size_t index, size_t count, size_t target, void *data) {
    VLC_UNUSED(playlist);

    intf_thread_t *intf = data;
    intf_sys_t *sys = intf->p_sys;

    msg_Dbg(intf, "playlist_on_items_moved");

    vlc_mutex_lock(&sys->pl_lock);
    sys->playlist_version++;
    vlc_vector_move_slice(&sys->pl, index, count, target);

    size_t from = __MIN(index, target);
    size_t to = __MAX(index, target) + count;
    for (size_t i = from; i < to; i++)
        sys->pl.data[i].playlist_version = sys->playlist_version;
    vlc_mutex_unlock(&sys->pl_lock);

    mpd_send_idle_events_update(data, MPD_SUBSYSTEM_PLAYLIST);
}

static void playlist_on_items_removed(vlc_playlist_t *playlist, size_t index, size_t count, void *data) {
    VLC_UNUSED(playlist);

    intf_thread_t *intf = data;
    intf_sys_t *sys = intf->p_sys;

    msg_Dbg(intf, "playlist_on_items_removed");

    vlc_mutex_lock(&sys->pl_lock);
    sys->playlist_version++;
    vlc_vector_remove_slice(&sys->pl, index, count);

    for (size_t i = index; i < sys->pl.size; i++)
        sys->pl.data[i].playlist_version = sys->playlist_version;
    vlc_mutex_unlock(&sys->pl_lock);

    mpd_send_idle_events_update(data, MPD_SUBSYSTEM_PLAYLIST);
}

static void playlist_on_items_updated(vlc_playlist_t *playlist, size_t index, vlc_playlist_item_t *const items[], size_t count, void *data) {
    VLC_UNUSED(playlist);
    VLC_UNUSED(items);

    intf_thread_t *intf = data;
    intf_sys_t *sys = intf->p_sys;

    msg_Dbg(intf, "playlist_on_items_updated");

    vlc_mutex_lock(&sys->pl_lock);
    sys->playlist_version++;
    for (size_t i = 0; i < count; i++)
        sys->pl.data[index + i].playlist_version = sys->playlist_version;
    vlc_mutex_unlock(&sys->pl_lock);

    mpd_send_idle_events_update(data, MPD_SUBSYSTEM_PLAYLIST);
}

static void playlist_on_playback_repeat_changed(vlc_playlist_t *playlist, enum vlc_playlist_playback_repeat repeat, void *data) {
    VLC_UNUSED(playlist);
    VLC_UNUSED(repeat);
    mpd_send_idle_events_update(data, MPD_SUBSYSTEM_OPTIONS);
}

static void playlist_on_playback_order_changed(vlc_playlist_t *playlist, enum vlc_playlist_playback_order order, void *data) {
    VLC_UNUSED(playlist);
    VLC_UNUSED(order);
    mpd_send_idle_events_update(data, MPD_SUBSYSTEM_OPTIONS);
}

static void playlist_on_current_index_changed(vlc_playlist_t *playlist, ssize_t index, void *data) {
    VLC_UNUSED(playlist);
    VLC_UNUSED(index);
    mpd_send_idle_events_update(data, MPD_SUBSYSTEM_PLAYLIST);
}

static void playlist_on_has_prev_changed(vlc_playlist_t *playlist, bool has_prev, void *data) {
    VLC_UNUSED(playlist);
    VLC_UNUSED(has_prev);
    mpd_send_idle_events_update(data, MPD_SUBSYSTEM_PLAYLIST);
}

static void playlist_on_has_next_changed(vlc_playlist_t *playlist, bool has_next, void *data) {
    VLC_UNUSED(playlist);
    VLC_UNUSED(has_next);
    mpd_send_idle_events_update(data, MPD_SUBSYSTEM_PLAYLIST);
}

static const struct vlc_player_cbs player_cbs = {
    .on_state_changed = player_on_state_changed,
    .on_current_media_changed = player_on_current_media_changed,
    .on_error_changed = player_on_error_changed,
    .on_track_list_changed = player_on_track_list_changed,
    .on_titles_changed = player_on_titles_changed,
};

static const struct vlc_player_aout_cbs player_aout_cbs = {
    .on_device_changed = player_on_device_changed,
    .on_mute_changed = player_on_mute_changed,
    .on_volume_changed = player_on_volume_changed,
};

static const struct vlc_playlist_callbacks playlist_cbs = {
    .on_items_reset = playlist_on_items_reset,
    .on_items_added = playlist_on_items_added,
    .on_items_moved = playlist_on_items_moved,
    .on_items_removed = playlist_on_items_removed,
    .on_items_updated = playlist_on_items_updated,
    .on_playback_repeat_changed = playlist_on_playback_repeat_changed,
    .on_playback_order_changed = playlist_on_playback_order_changed,
    .on_current_index_changed = playlist_on_current_index_changed,
    .on_has_prev_changed = playlist_on_has_prev_changed,
    .on_has_next_changed = playlist_on_has_next_changed,
};

int mpd_playlist_callbacks_setup(intf_thread_t *intf) {
    intf_sys_t *sys = intf->p_sys;

    vlc_playlist_Lock(sys->playlist);

    sys->player_listener = vlc_player_AddListener(sys->player, &player_cbs, intf);
    if (!sys->player_listener) {
        msg_Err(intf, "Failed to add player listener");
        goto error_player_listener;
    }

    sys->player_aout_listener = vlc_player_aout_AddListener(sys->player, &player_aout_cbs, intf);
    if (!sys->player_aout_listener) {
        msg_Err(intf, "Failed to add player aout listener");
        goto error_player_aout_listener;
    }

    sys->playlist_listener = vlc_playlist_AddListener(sys->playlist, &playlist_cbs, intf, true);
    if (!sys->playlist_listener) {
        msg_Err(intf, "Failed to add playlist listener");
        goto error_playlist_listener;
    }

    vlc_playlist_Unlock(sys->playlist);
    return 0;

error_playlist_listener:
    vlc_player_aout_RemoveListener(sys->player, sys->player_aout_listener);
error_player_aout_listener:
    vlc_player_RemoveListener(sys->player, sys->player_listener);
error_player_listener:
    vlc_playlist_Unlock(sys->playlist);
    return -1;
}
