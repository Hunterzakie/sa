/*****************************************************************************
 * command.c
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

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
# include <locale.h>
#endif
#ifdef HAVE_XLOCALE_H
# include <xlocale.h>
#endif

#include <vlc_common.h>
#include <vlc_url.h>
#include <vlc_input_item.h>

#include "protocol.h"
#include "command.h"
#include "medialibrary.h"
#include "filter.h"

#define PARSE_BOOL(arg, val) do {                               \
        if (!mpd_parse_bool(arg, val)) {                        \
            mpd_send_error(intf, client, MPD_ERROR_ARG,         \
                           "Boolean (0/1) expected: %s", arg);  \
            return MPD_CMD_ERROR;                               \
        }                                                       \
    } while (0)

#define PARSE_INT(arg, val) do {                            \
        if (!mpd_parse_int(arg, val)) {                     \
            mpd_send_error(intf, client, MPD_ERROR_ARG,     \
                           "Integer expected: %s", arg);    \
            return MPD_CMD_ERROR;                           \
        }                                                   \
    } while (0)

#define PARSE_UINT(arg, val) do {                                   \
        if (!mpd_parse_uint(arg, val)) {                            \
            mpd_send_error(intf, client, MPD_ERROR_ARG,             \
                           "Unsigned integer expected: %s", arg);   \
            return MPD_CMD_ERROR;                                   \
        }                                                           \
    } while (0)

#define PARSE_FLOAT(arg, val) do {                                      \
        if (!mpd_parse_float(arg, val)) {                               \
            mpd_send_error(intf, client, MPD_ERROR_ARG,                 \
                           "Floating point number expected: %s", arg);  \
            return MPD_CMD_ERROR;                                       \
        }                                                               \
    } while (0)

#define PARSE_RANGE_OR_UINT(arg, from, to) do {                         \
        if (mpd_parse_uint(arg, from)) {                                \
            *to = *from + 1;                                            \
        } else if (!mpd_parse_urange(arg, from, to)) {                  \
            mpd_send_error(intf, client, MPD_ERROR_ARG,                 \
                           "Unsigned integer or range expected: %s", arg); \
            return MPD_CMD_ERROR;                                       \
        }                                                               \
    } while (0)

#define PARSE_TAG(arg, tag) do {                            \
        if (!mpd_parse_tag(arg, tag)) {                     \
            mpd_send_error(intf, client, MPD_ERROR_ARG,     \
                           "Tag name expected: %s", arg);   \
            return MPD_CMD_ERROR;                           \
        }                                                   \
    } while (0)

static mpd_filter_t *try_parse_tag_value_filter(char **argv, int *i, bool case_sensitive) {
    mpd_filter_t *filter = NULL;

    for (int tag; argv[*i] && argv[*i + 1]; *i += 2) {
        mpd_filter_t *f = NULL;
        if (!mpd_parse_tag(argv[*i], &tag))
            goto error;
        f = mpd_filter_from_tag_value(tag, argv[*i + 1], case_sensitive);
        if (!f)
            goto error;
        if (!filter) {
            filter = f;
        } else if (filter->kind == MPD_FILTER_AND) {
            if (!vlc_vector_push(&filter->and, f))
                goto error;
        } else {
            mpd_filter_t *andf = malloc(sizeof *andf);
            if (!andf)
                goto error;
            andf->kind = MPD_FILTER_AND;
            vlc_vector_init(&andf->and);
            if (!vlc_vector_push_all(&andf->and, ((mpd_filter_t *[]){filter, f}), 2))
                goto error;
            filter = andf;
        }

        continue;
error:
        mpd_filter_free(f);
        mpd_filter_free(filter);
        return NULL;
    }

    return filter;
}

/* add {URI} [POSITION] */
static mpd_cmd_result_t cmd_add(intf_thread_t *intf, mpd_client_t *client, char **argv) {
    /* TODO: handle POSITION argument */
    intf_sys_t *sys = intf->p_sys;

    char *mrl = mpd_path2mrl(intf, argv[1]);
    if (!mrl) {
        mpd_send_error(intf, client, MPD_ERROR_NO_EXIST,
                       "%s: no such file or directory", argv[1]);
        return MPD_CMD_ERROR;
    }

    mpd_mrl_vector_t media_vec = mpd_ml_recursive_list(intf, mrl);
    free(mrl);

    vlc_playlist_Lock(sys->playlist);
    vlc_vector_foreach(mrl, &media_vec) {
        input_item_t *item = input_item_New(mrl, NULL);
        if (item) {
            vlc_playlist_AppendOne(sys->playlist, item);
            input_item_Release(item);
        }
        free(mrl);
    }
    vlc_playlist_Unlock(sys->playlist);

    vlc_vector_destroy(&media_vec);
    return MPD_CMD_OK;
}

/* addid {URI} [POSITION] */
static mpd_cmd_result_t cmd_addid(intf_thread_t *intf, mpd_client_t *client, char **argv) {
    /* TODO: handle POSITION argument */
    intf_sys_t *sys = intf->p_sys;

    char *mrl = mpd_path2mrl(intf, argv[1]);
    if (!mrl) {
        mpd_send_error(intf, client, MPD_ERROR_NO_EXIST,
                       "%s: no such file or directory", argv[1]);
        return MPD_CMD_ERROR;
    }
    input_item_t *item = vlc_ml_get_input_item_by_mrl(sys->medialibrary, mrl);
    free(mrl);

    if (!item) {
        mpd_send_error(intf, client, MPD_ERROR_NO_EXIST,
                       "%s: no such file or directory", argv[1]);
        return MPD_CMD_ERROR;
    }

    vlc_playlist_Lock(sys->playlist);
    int rc = vlc_playlist_AppendOne(sys->playlist, item);
    if (rc != VLC_SUCCESS) {
        vlc_playlist_Unlock(sys->playlist);
        input_item_Release(item);
        mpd_send_error(intf, client, MPD_ERROR_UNKNOWN_CMD,
                       "vlc_playlist_AppendOne: %s", vlc_strerror_c(rc));
        return MPD_CMD_ERROR;
    }
    size_t last = vlc_playlist_Count(sys->playlist) - 1;
    vlc_playlist_item_t *pi = vlc_playlist_Get(sys->playlist, last);
    uint64_t id = vlc_playlist_item_GetId(pi);
    vlc_playlist_Unlock(sys->playlist);

    mpd_send_kv(intf, client, "Id", "%" PRIu64, id);

    input_item_Release(item);
    return MPD_CMD_OK;
}

static mpd_cmd_result_t cmd_channels(intf_thread_t *intf, mpd_client_t *client, char **argv) {
    /* TODO: implement channels */
    VLC_UNUSED(intf); VLC_UNUSED(client); VLC_UNUSED(argv);

    return MPD_CMD_OK;
}

static mpd_cmd_result_t cmd_clear(intf_thread_t *intf, mpd_client_t *client, char **argv) {
    VLC_UNUSED(client); VLC_UNUSED(argv);
    intf_sys_t *sys = intf->p_sys;

    vlc_playlist_Lock(sys->playlist);
    vlc_playlist_Clear(sys->playlist);
    vlc_playlist_Unlock(sys->playlist);

    return MPD_CMD_OK;
}

static mpd_cmd_result_t cmd_command_list_begin(intf_thread_t *intf, mpd_client_t *client, char **argv) {
    VLC_UNUSED(argv);
    if (client->in_command_list) {
        mpd_send_error(intf, client, MPD_ERROR_UNKNOWN_CMD,
                       "Already in a command list");
        return MPD_CMD_ERROR;
    }

    client->in_command_list = MPD_CL_YES;
    return MPD_CMD_SILENT_OK;
}

static mpd_cmd_result_t cmd_command_list_end(intf_thread_t *intf, mpd_client_t *client, char **argv) {
    VLC_UNUSED(argv);
    if (!client->in_command_list) {
        mpd_send_error(intf, client, MPD_ERROR_UNKNOWN_CMD,
                       "Not in a command list");
        return MPD_CMD_ERROR;
    }

    client->in_command_list = MPD_CL_NO;
    return MPD_CMD_OK;
}

static mpd_cmd_result_t cmd_command_list_ok_begin(intf_thread_t *intf, mpd_client_t *client, char **argv) {
    VLC_UNUSED(argv);
    if (client->in_command_list) {
        mpd_send_error(intf, client, MPD_ERROR_UNKNOWN_CMD,
                       "Already in a command list");
        return MPD_CMD_ERROR;
    }

    client->in_command_list = MPD_CL_OK_YES;
    return MPD_CMD_SILENT_OK;
}

static mpd_cmd_result_t cmd_commands(intf_thread_t *intf, mpd_client_t *client, char **argv);

static mpd_cmd_result_t cmd_currentsong(intf_thread_t *intf, mpd_client_t *client, char **argv) {
    VLC_UNUSED(argv);
    intf_sys_t *sys = intf->p_sys;

    vlc_playlist_Lock(sys->playlist);
    input_item_t *item = vlc_player_GetCurrentMedia(sys->player);
    if (item)
        input_item_Hold(item);
    vlc_playlist_Unlock(sys->playlist);

    if (item) {
        mpd_ml_print_item(intf, client, item);
        input_item_Release(item);
    }

    return MPD_CMD_OK;
}

static mpd_cmd_result_t cmd_decoders(intf_thread_t *intf, mpd_client_t *client, char **argv) {
    /* TODO */
    VLC_UNUSED(intf); VLC_UNUSED(client); VLC_UNUSED(argv);
    return MPD_CMD_OK;
}

/* delete [{POS} | {START:END}] */
static mpd_cmd_result_t cmd_delete(intf_thread_t *intf, mpd_client_t *client, char **argv) {
    intf_sys_t *sys = intf->p_sys;

    uint64_t from, to;
    PARSE_RANGE_OR_UINT(argv[1], &from, &to);

    vlc_playlist_Lock(sys->playlist);
    uint64_t count = vlc_playlist_Count(sys->playlist);
    if (to > count)
        to = count;
    if (from >= to) {
        vlc_playlist_Unlock(sys->playlist);
        mpd_send_error(intf, client, MPD_ERROR_ARG, "Bad song index");
        return MPD_CMD_ERROR;
    }
    vlc_playlist_Remove(sys->playlist, from, to - from);
    vlc_playlist_Unlock(sys->playlist);

    return MPD_CMD_OK;
}

/* deleteid {SONGID} */
static mpd_cmd_result_t cmd_deleteid(intf_thread_t *intf, mpd_client_t *client, char **argv) {
    intf_sys_t *sys = intf->p_sys;

    uint64_t songid;
    PARSE_UINT(argv[1], &songid);

    vlc_playlist_Lock(sys->playlist);
    ssize_t idx = vlc_playlist_IndexOfId(sys->playlist, songid);
    if (idx < 0) {
        vlc_playlist_Unlock(sys->playlist);
        mpd_send_error(intf, client, MPD_ERROR_NO_EXIST, "No such song");
        return MPD_CMD_ERROR;
    }
    vlc_playlist_RemoveOne(sys->playlist, idx);
    vlc_playlist_Unlock(sys->playlist);

    return MPD_CMD_OK;
}

static mpd_cmd_result_t find_inner(intf_thread_t *intf, mpd_client_t *client, char **argv, bool case_sensitive) {
    /* TODO: sort, window */
    mpd_filter_t *filter = try_parse_tag_value_filter(argv, &(int){1}, case_sensitive);

    if (!filter)
        filter = mpd_filter_parse(argv[1]);
    if (!filter) {
        mpd_send_error(intf, client, MPD_ERROR_ARG, "Malformed filter");
        return MPD_CMD_ERROR;
    }
    mpd_filter_optimize(&filter);

    vlc_ml_media_list_t *medias = mpd_ml_narrow_from_filter(intf, &filter);
    if (!medias) {
        mpd_filter_free(filter);
        return MPD_CMD_OK;
    }

    for (size_t i = 0; i < medias->i_nb_items; i++) {
        vlc_ml_media_t *media = &medias->p_items[i];
        if (!filter || mpd_filter(intf, filter, media, case_sensitive) == MPD_FILTER_YES)
            mpd_ml_print_media(intf, client, media);
    }

    vlc_ml_release(medias);
    mpd_filter_free(filter);
    return MPD_CMD_OK;
}

/* find {FILTER} [sort {TYPE}] [window {START:END}]
   find [{TYPE} {VALUE}]... [sort {TYPE}] [window {START:END}] */
static mpd_cmd_result_t cmd_find(intf_thread_t *intf, mpd_client_t *client, char **argv) {
    /* TODO: handle sort and window */
    return find_inner(intf, client, argv, true);
}

static mpd_cmd_result_t findadd_inner(intf_thread_t *intf, mpd_client_t *client, char **argv, bool case_sensitive) {
    intf_sys_t *sys = intf->p_sys;

    /* TODO: sort, window, position */
    mpd_filter_t *filter = try_parse_tag_value_filter(argv, &(int){1}, case_sensitive);

    if (!filter)
        filter = mpd_filter_parse(argv[1]);
    if (!filter) {
        mpd_send_error(intf, client, MPD_ERROR_ARG, "Malformed filter");
        return MPD_CMD_ERROR;
    }
    mpd_filter_optimize(&filter);

    vlc_ml_media_list_t *medias = mpd_ml_narrow_from_filter(intf, &filter);
    if (!medias) {
        mpd_filter_free(filter);
        return MPD_CMD_OK;
    }

    vlc_playlist_Lock(sys->playlist);
    for (size_t i = 0; i < medias->i_nb_items; i++) {
        vlc_ml_media_t *media = &medias->p_items[i];
        if (!filter || mpd_filter(intf, filter, media, case_sensitive) == MPD_FILTER_YES) {
            char *mrl = mpd_ml_media_mrl(media);
            if (mrl) {
                input_item_t *item = input_item_New(mrl, NULL);
                if (item) {
                    vlc_playlist_AppendOne(sys->playlist, item);
                    input_item_Release(item);
                }
                free(mrl);
            }
        }
    }
    vlc_playlist_Unlock(sys->playlist);

    vlc_ml_release(medias);
    mpd_filter_free(filter);
    return MPD_CMD_OK;
}

/* findadd {FILTER} [sort {TYPE}] [window {START:END}] [position POS]
   findadd [{TYPE} {VALUE}]... [sort {TYPE}] [window {START:END}] [position POS] */
static mpd_cmd_result_t cmd_findadd(intf_thread_t *intf, mpd_client_t *client, char **argv) {
    /* TODO: handle sort, window and position */
    return findadd_inner(intf, client, argv, true);
}

static int bstrcmp(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static mpd_cmd_result_t cmd_idle(intf_thread_t *intf, mpd_client_t *client, char **argv) {
    client->idle_events_waiting_for = 0;
    for (int i = 1; argv[i]; i++) {
        const char **subsystem = bsearch(&argv[i], mpd_subsystem_names, MPD_SUBSYSTEM_COUNT,
                                         sizeof *mpd_subsystem_names, bstrcmp);
        if (!subsystem) {
            mpd_send_error(intf, client, MPD_ERROR_ARG, "Unknown subsystem %s", argv[i]);
            return MPD_CMD_ERROR;
        }
        client->idle_events_waiting_for |= 1 << (subsystem - mpd_subsystem_names);
    }
    if (client->idle_events_waiting_for == 0)
        client->idle_events_waiting_for = MPD_SUBSYSTEM_ALL;
    bool changed = mpd_send_changed_subsystems(intf, client);
    client->idle = !changed;
    return changed ? MPD_CMD_OK : MPD_CMD_SILENT_OK;
}

/* list {TYPE} {FILTER} [group {GROUPTYPE}]
   list {TYPE} [{TYPE} {VALUE}]... [group {GROUPTYPE}] */
static mpd_cmd_result_t cmd_list(intf_thread_t *intf, mpd_client_t *client, char **argv) {
    /* TODO: handle group */
    intf_sys_t *sys = intf->p_sys;

    int tag;
    PARSE_TAG(argv[1], &tag);

    mpd_filter_t *filter = NULL;
    if (argv[2]) {
        filter = try_parse_tag_value_filter(argv, &(int){2}, false);
        if (!filter)
            filter = mpd_filter_parse(argv[2]);
        if (!filter) {
            mpd_send_error(intf, client, MPD_ERROR_ARG, "Malformed filter");
            return MPD_CMD_ERROR;
        }
        mpd_filter_optimize(&filter);
    }

    switch (tag) {
#define DO(WHAT, FIELD, ...)                                            \
        {                                                               \
            vlc_ml_ ## WHAT ## _list_t *list =                          \
                vlc_ml_list_ ## WHAT ## s(sys->medialibrary, NULL, ##__VA_ARGS__); \
            if (!list) {                                                \
                mpd_filter_free(filter);                                \
                return MPD_CMD_OK;                                      \
            }                                                           \
            for (size_t i = 0; i < list->i_nb_items; i++)               \
                if (!filter || mpd_filter(intf, filter, &list->p_items[i], false) == MPD_FILTER_YES) \
                    mpd_send_kv(intf, client, mpd_tag_names[tag], "%s", list->p_items[i].FIELD); \
            vlc_ml_release(list);                                       \
        }
    case MPD_TAG_ALBUM:
        DO(album, psz_title);
        break;
    case MPD_TAG_ARTIST:
        DO(artist, psz_name, true);
        break;
    case MPD_TAG_GENRE:
        DO(genre, psz_name);
        break;
#undef DO
    default:
        break;
    }

    mpd_filter_free(filter);

    return MPD_CMD_OK;
}

static mpd_cmd_result_t cmd_listpartitions(intf_thread_t *intf, mpd_client_t *client, char **argv) {
    /* TODO: implement partitions */
    VLC_UNUSED(argv);

    mpd_send_kv(intf, client, "partition", "default");
    return MPD_CMD_OK;
}

static mpd_cmd_result_t cmd_listplaylists(intf_thread_t *intf, mpd_client_t *client, char **argv) {
    /* TODO: implement playlists */
    VLC_UNUSED(intf); VLC_UNUSED(client); VLC_UNUSED(argv);

    return MPD_CMD_OK;
}

/* lsinfo [URI] */
static mpd_cmd_result_t cmd_lsinfo(intf_thread_t *intf, mpd_client_t *client, char **argv) {
    intf_sys_t *sys = intf->p_sys;

    const char *path = argv[1] ? argv[1] : "";
    if (strcmp(path, "/") == 0)
        path = "";

    if (strlen(path) == 0) {
        vlc_ml_folder_list_t *entry_points = vlc_ml_list_entry_points(sys->medialibrary, NULL);
        mpd_ml_print_folder_list(intf, client, entry_points);
        vlc_ml_release(entry_points);
    } else {
        char *mrl = mpd_path2mrl(intf, path);
        if (!mrl) {
            mpd_send_error(intf, client, MPD_ERROR_NO_EXIST,
                           "%s: no such file or directory", argv[1]);
            return MPD_CMD_ERROR;
        }
        vlc_ml_folder_t *folder = vlc_ml_get_folder_by_mrl(sys->medialibrary, mrl);
        free(mrl);

        if (!folder) {
            mpd_send_error(intf, client, MPD_ERROR_NO_EXIST, "the directory does not exist");
            return MPD_CMD_ERROR;
        }

        vlc_ml_folder_list_t *subfolder_list;
        int rc = vlc_ml_list(sys->medialibrary, VLC_ML_LIST_SUBFOLDERS, NULL, folder->i_id, &subfolder_list);
        if (rc != VLC_SUCCESS)
            subfolder_list = NULL;
        mpd_ml_print_folder_list(intf, client, subfolder_list);
        vlc_ml_release(subfolder_list);

        vlc_ml_media_list_t *media_list;
        rc = vlc_ml_list(sys->medialibrary, VLC_ML_LIST_FOLDER_MEDIA, NULL, folder->i_id, &media_list);
        if (rc != VLC_SUCCESS)
            media_list = NULL;
        mpd_ml_print_media_list(intf, client, media_list);
        vlc_ml_release(media_list);

        vlc_ml_release(folder);
    }

    return MPD_CMD_OK;
}

/* move [{FROM} | {START:END}] {TO} */
static mpd_cmd_result_t cmd_move(intf_thread_t *intf, mpd_client_t *client, char **argv) {
    intf_sys_t *sys = intf->p_sys;

    uint64_t from, to;
    PARSE_RANGE_OR_UINT(argv[1], &from, &to);
    /* TODO: relative destination */
    uint64_t dest;
    PARSE_UINT(argv[2], &dest);
    vlc_playlist_Lock(sys->playlist);
    uint64_t count = vlc_playlist_Count(sys->playlist);
    if (to > count)
        to = count;
    if (from >= to || dest > count - (to - from)) {
        vlc_playlist_Unlock(sys->playlist);
        mpd_send_error(intf, client, MPD_ERROR_ARG, "Bad song index");
        return MPD_CMD_ERROR;
    }
    vlc_playlist_Move(sys->playlist, from, to - from, dest);
    vlc_playlist_Unlock(sys->playlist);

    return MPD_CMD_OK;
}

/* moveid {SONGID} {TO} */
static mpd_cmd_result_t cmd_moveid(intf_thread_t *intf, mpd_client_t *client, char **argv) {
    intf_sys_t *sys = intf->p_sys;

    uint64_t songid;
    PARSE_UINT(argv[1], &songid);
    /* TODO: relative destination */
    uint64_t dest;
    PARSE_UINT(argv[2], &dest);

    vlc_playlist_Lock(sys->playlist);
    ssize_t idx = vlc_playlist_IndexOfId(sys->playlist, songid);
    if (idx < 0) {
        vlc_playlist_Unlock(sys->playlist);
        mpd_send_error(intf, client, MPD_ERROR_NO_EXIST, "No such song");
        return MPD_CMD_ERROR;
    }
    if (dest >= (uint64_t)vlc_playlist_Count(sys->playlist)) {
        vlc_playlist_Unlock(sys->playlist);
        mpd_send_error(intf, client, MPD_ERROR_ARG, "Bad song index");
        return MPD_CMD_ERROR;
    }
    vlc_playlist_MoveOne(sys->playlist, idx, dest);
    vlc_playlist_Unlock(sys->playlist);

    return MPD_CMD_OK;
}

static mpd_cmd_result_t cmd_next(intf_thread_t *intf, mpd_client_t *client, char **argv) {
    VLC_UNUSED(argv);
    intf_sys_t *sys = intf->p_sys;

    vlc_playlist_Lock(sys->playlist);
    int rc = vlc_playlist_Next(sys->playlist);

    switch (rc) {
    case VLC_EGENERIC:
        /* There is no next, imitate MPD's behavior in this case by stopping */
        vlc_playlist_Stop(sys->playlist);
        /* fallthrough */
    case VLC_SUCCESS:
        vlc_playlist_Unlock(sys->playlist);
        return MPD_CMD_OK;
    default:
        vlc_playlist_Unlock(sys->playlist);
        mpd_send_error(intf, client, MPD_ERROR_UNKNOWN_CMD,
                       "vlc_playlist_Next: %s", vlc_strerror_c(rc));
        return MPD_CMD_ERROR;
    }
}

static mpd_cmd_result_t cmd_noidle(intf_thread_t *intf, mpd_client_t *client, char **argv) {
    VLC_UNUSED(argv);
    if (client->in_command_list) {
        mpd_send_error(intf, client, MPD_ERROR_UNKNOWN_CMD,
                       "noidle in a command list");
        return MPD_CMD_ERROR;
    }

    if (client->idle) {
        client->idle = false;
        mpd_send_changed_subsystems(intf, client);
        return MPD_CMD_OK;
    } else {
        return MPD_CMD_SILENT_OK;
    }
}

static mpd_cmd_result_t cmd_outputs(intf_thread_t *intf, mpd_client_t *client, char **argv) {
    /* TODO: implement outputs */
    VLC_UNUSED(argv);
    mpd_send_kv(intf, client, "outputid", "0");
    mpd_send_kv(intf, client, "outputname", "VLC");
    mpd_send_kv(intf, client, "outputenabled", "1");
    return MPD_CMD_OK;
}

/* pause {STATE} */
static mpd_cmd_result_t cmd_pause(intf_thread_t *intf, mpd_client_t *client, char **argv) {
    intf_sys_t *sys = intf->p_sys;

    bool state;
    if (argv[1])
        PARSE_BOOL(argv[1], &state);

    vlc_playlist_Lock(sys->playlist);
    if (argv[1]) {
        if (state)
            vlc_playlist_Pause(sys->playlist);
        else
            vlc_playlist_Resume(sys->playlist);
    } else {
        if (vlc_player_IsStarted(sys->player))
            vlc_player_TogglePause(sys->player);
        else
            vlc_playlist_Start(sys->playlist);
    }
    vlc_playlist_Unlock(sys->playlist);

    return MPD_CMD_OK;
}

static mpd_cmd_result_t cmd_ping(intf_thread_t *intf, mpd_client_t *client, char **argv) {
    VLC_UNUSED(intf); VLC_UNUSED(client); VLC_UNUSED(argv);
    return MPD_CMD_OK;
}

/* play [SONGPOS] */
static mpd_cmd_result_t cmd_play(intf_thread_t *intf, mpd_client_t *client, char **argv) {
    intf_sys_t *sys = intf->p_sys;

    uint64_t songpos;
    if (argv[1])
        PARSE_UINT(argv[1], &songpos);

    vlc_playlist_Lock(sys->playlist);
    if (argv[1]) {
        if (songpos >= (uint64_t)vlc_playlist_Count(sys->playlist)) {
            vlc_playlist_Unlock(sys->playlist);
            mpd_send_error(intf, client, MPD_ERROR_ARG, "Bad song index");
            return MPD_CMD_ERROR;
        }
        vlc_playlist_GoTo(sys->playlist, songpos);
    }
    vlc_player_Start(sys->player);
    vlc_playlist_Unlock(sys->playlist);

    return MPD_CMD_OK;
}

/* playid [SONGID] */
static mpd_cmd_result_t cmd_playid(intf_thread_t *intf, mpd_client_t *client, char **argv) {
    intf_sys_t *sys = intf->p_sys;

    uint64_t songid;
    if (argv[1])
        PARSE_UINT(argv[1], &songid);

    vlc_playlist_Lock(sys->playlist);
    if (argv[1]) {
        ssize_t songpos = vlc_playlist_IndexOfId(sys->playlist, songid);
        if (songpos < 0) {
            vlc_playlist_Unlock(sys->playlist);
            mpd_send_error(intf, client, MPD_ERROR_NO_EXIST,
                           "Song id %" PRIu64 " not present in playlist", songid);
            return MPD_CMD_ERROR;
        }
        vlc_playlist_GoTo(sys->playlist, songpos);
    }
    vlc_player_Start(sys->player);
    vlc_playlist_Unlock(sys->playlist);

    return MPD_CMD_OK;
}

static mpd_cmd_result_t playlistfind_inner(intf_thread_t *intf, mpd_client_t *client, char **argv, bool case_sensitive) {
    intf_sys_t *sys = intf->p_sys;

    mpd_filter_t *filter = mpd_filter_parse(argv[1]);
    if (!filter) {
        mpd_send_error(intf, client, MPD_ERROR_ARG, "Malformed filter");
        return MPD_CMD_ERROR;
    }
    mpd_filter_optimize(&filter);

    vlc_playlist_Lock(sys->playlist);
    size_t count = vlc_playlist_Count(sys->playlist);
    for (size_t i = 0; i < count; i++) {
        input_item_t *item = vlc_playlist_item_GetMedia(vlc_playlist_Get(sys->playlist, i));
        if (mpd_filter(intf, filter, item, case_sensitive) == MPD_FILTER_YES)
            mpd_ml_print_item(intf, client, item);
    }
    vlc_playlist_Unlock(sys->playlist);

    return MPD_CMD_OK;
}

/* playlistfind {FILTER} [sort {TYPE}] [window {START:END}]
   playlistfind [{TYPE} {VALUE}]... [sort {TYPE}] [window {START:END}] */
static mpd_cmd_result_t cmd_playlistfind(intf_thread_t *intf, mpd_client_t *client, char **argv) {
    /* TODO: handle sort and window */
    return playlistfind_inner(intf, client, argv, true);
}

/* playlistinfo [[SONGPOS] | [START:END]] */
static mpd_cmd_result_t cmd_playlistinfo(intf_thread_t *intf, mpd_client_t *client, char **argv) {
    /* TODO: handle arguments */
    VLC_UNUSED(argv);
    intf_sys_t *sys = intf->p_sys;

    vlc_playlist_Lock(sys->playlist);
    size_t count = vlc_playlist_Count(sys->playlist);
    for (size_t i = 0; i < count; i++) {
        vlc_playlist_item_t *playlist_item = vlc_playlist_Get(sys->playlist, i);
        input_item_t *item = vlc_playlist_item_GetMedia(playlist_item);
        mpd_ml_print_item(intf, client, item);
        mpd_send_kv(intf, client, "Pos", "%zu", i);
        mpd_send_kv(intf, client, "Id", "%" PRIu64, vlc_playlist_item_GetId(playlist_item));
    }
    vlc_playlist_Unlock(sys->playlist);

    return MPD_CMD_OK;
}

/* playlistsearch {FILTER} [sort {TYPE}] [window {START:END}]
   playlistsearch [{TYPE} {VALUE}]... [sort {TYPE}] [window {START:END}] */
static mpd_cmd_result_t cmd_playlistsearch(intf_thread_t *intf, mpd_client_t *client, char **argv) {
    /* TODO: handle sort and window */
    return playlistfind_inner(intf, client, argv, false);
}

/* plchanges {VERSION} [START:END] */
static mpd_cmd_result_t cmd_plchanges(intf_thread_t *intf, mpd_client_t *client, char **argv) {
    /* TODO: handle range */
    intf_sys_t *sys = intf->p_sys;

    int64_t ver;
    PARSE_INT(argv[1], &ver);

    vlc_mutex_lock(&sys->pl_lock);
    vlc_playlist_Lock(sys->playlist);
    for (size_t i = 0; i < sys->pl.size; i++) {
        if (sys->pl.data[i].playlist_version > ver) {
            vlc_playlist_item_t *playlist_item = vlc_playlist_Get(sys->playlist, i);
            input_item_t *item = vlc_playlist_item_GetMedia(playlist_item);
            mpd_ml_print_item(intf, client, item);
            mpd_send_kv(intf, client, "Pos", "%zu", i);
            mpd_send_kv(intf, client, "Id", "%" PRIu64, sys->pl.data[i].id);
        }
    }
    vlc_playlist_Unlock(sys->playlist);
    vlc_mutex_unlock(&sys->pl_lock);

    return MPD_CMD_OK;
}

static mpd_cmd_result_t cmd_previous(intf_thread_t *intf, mpd_client_t *client, char **argv) {
    VLC_UNUSED(argv);
    intf_sys_t *sys = intf->p_sys;

    vlc_playlist_Lock(sys->playlist);
    int rc = vlc_playlist_Prev(sys->playlist);
    vlc_playlist_Unlock(sys->playlist);

    switch (rc) {
    case VLC_EGENERIC:
    case VLC_SUCCESS:
        return MPD_CMD_OK;
    default:
        mpd_send_error(intf, client, MPD_ERROR_UNKNOWN_CMD,
                       "vlc_playlist_Previous: %s", vlc_strerror_c(rc));
        return MPD_CMD_ERROR;
    }
}

/* random {STATE} */
static mpd_cmd_result_t cmd_random(intf_thread_t *intf, mpd_client_t *client, char **argv) {
    intf_sys_t *sys = intf->p_sys;

    bool random;
    PARSE_BOOL(argv[1], &random);

    vlc_playlist_Lock(sys->playlist);
    vlc_playlist_SetPlaybackOrder(sys->playlist, random
                                  ? VLC_PLAYLIST_PLAYBACK_ORDER_RANDOM
                                  : VLC_PLAYLIST_PLAYBACK_ORDER_NORMAL);
    vlc_playlist_Unlock(sys->playlist);

    return MPD_CMD_OK;
}

/* search {FILTER} [sort {TYPE}] [window {START:END}]
   search [{TYPE} {VALUE}]... [sort {TYPE}] [window {START:END}] */
static mpd_cmd_result_t cmd_search(intf_thread_t *intf, mpd_client_t *client, char **argv) {
    /* TODO: handle sort and window */
    return find_inner(intf, client, argv, false);
}

/* searchadd {FILTER} [sort {TYPE}] [window {START:END}] [position POS]
   searchadd [{TYPE} {VALUE}]... [sort {TYPE}] [window {START:END}] [position POS] */
static mpd_cmd_result_t cmd_searchadd(intf_thread_t *intf, mpd_client_t *client, char **argv) {
    /* TODO: handle sort, window and position */
    return findadd_inner(intf, client, argv, false);
}

/* seek {SONGPOS} {TIME} */
static mpd_cmd_result_t cmd_seek(intf_thread_t *intf, mpd_client_t *client, char **argv) {
    intf_sys_t *sys = intf->p_sys;

    uint64_t songpos;
    PARSE_UINT(argv[1], &songpos);

    double t;
    PARSE_FLOAT(argv[2], &t);
    vlc_tick_t time = vlc_tick_from_sec(t);
    bool relative = argv[2][0] == '+' || argv[2][0] == '-';

    vlc_playlist_Lock(sys->playlist);
    if (songpos >= (uint64_t)vlc_playlist_Count(sys->playlist)) {
        vlc_playlist_Unlock(sys->playlist);
        mpd_send_error(intf, client, MPD_ERROR_ARG, "Bad song index");
        return MPD_CMD_ERROR;
    }
    if ((uint64_t)vlc_playlist_GetCurrentIndex(sys->playlist) != songpos)
        vlc_playlist_GoTo(sys->playlist, songpos);
    vlc_player_Resume(sys->player);
    vlc_player_SeekByTime(sys->player, time, VLC_PLAYER_SEEK_PRECISE,
                          relative ? VLC_PLAYER_WHENCE_RELATIVE : VLC_PLAYER_WHENCE_ABSOLUTE);
    vlc_playlist_Unlock(sys->playlist);

    return MPD_CMD_OK;
}

/* seekcur {TIME} */
static mpd_cmd_result_t cmd_seekcur(intf_thread_t *intf, mpd_client_t *client, char **argv) {
    intf_sys_t *sys = intf->p_sys;

    double t;
    PARSE_FLOAT(argv[1], &t);
    vlc_tick_t time = vlc_tick_from_sec(t);
    bool relative = argv[1][0] == '+' || argv[1][0] == '-';

    vlc_playlist_Lock(sys->playlist);
    vlc_player_SeekByTime(sys->player, time, VLC_PLAYER_SEEK_PRECISE,
                          relative ? VLC_PLAYER_WHENCE_RELATIVE : VLC_PLAYER_WHENCE_ABSOLUTE);
    vlc_playlist_Unlock(sys->playlist);

    return MPD_CMD_OK;
}

/* seekid {SONGID} {TIME} */
static mpd_cmd_result_t cmd_seekid(intf_thread_t *intf, mpd_client_t *client, char **argv) {
    intf_sys_t *sys = intf->p_sys;

    uint64_t songid;
    PARSE_UINT(argv[1], &songid);

    double t;
    PARSE_FLOAT(argv[2], &t);
    vlc_tick_t time = vlc_tick_from_sec(t);
    bool relative = argv[2][0] == '+' || argv[2][0] == '-';

    vlc_playlist_Lock(sys->playlist);
    ssize_t songpos = vlc_playlist_IndexOfId(sys->playlist, songid);
    if (songpos < 0) {
        vlc_playlist_Unlock(sys->playlist);
        mpd_send_error(intf, client, MPD_ERROR_NO_EXIST,
                       "Song id %" PRIu64 " not present in playlist", songid);
        return MPD_CMD_ERROR;
    }
    if (vlc_playlist_GetCurrentIndex(sys->playlist) != songpos)
        vlc_playlist_GoTo(sys->playlist, songpos);
    vlc_player_Resume(sys->player);
    vlc_player_SeekByTime(sys->player, time, VLC_PLAYER_SEEK_PRECISE,
                          relative ? VLC_PLAYER_WHENCE_RELATIVE : VLC_PLAYER_WHENCE_ABSOLUTE);
    vlc_playlist_Unlock(sys->playlist);

    return MPD_CMD_OK;
}

/* setvol {VOL} */
static mpd_cmd_result_t cmd_setvol(intf_thread_t *intf, mpd_client_t *client, char **argv) {
    intf_sys_t *sys = intf->p_sys;

    double volume;
    PARSE_FLOAT(argv[1], &volume);
    if (volume < 0)
        volume = 0;
    if (volume > 200)
        volume = 200;

    vlc_playlist_Lock(sys->playlist);
    vlc_player_aout_SetVolume(sys->player, volume / 100);
    vlc_playlist_Unlock(sys->playlist);

    return MPD_CMD_OK;
}

static mpd_cmd_result_t cmd_stats(intf_thread_t *intf, mpd_client_t *client, char **argv) {
    VLC_UNUSED(argv);

    intf_sys_t *sys = intf->p_sys;

    size_t artists = vlc_ml_count_artists(sys->medialibrary, NULL, false);
    size_t albums = vlc_ml_count_albums(sys->medialibrary, NULL);
    size_t songs = vlc_ml_count_audio_media(sys->medialibrary, NULL);

    mpd_send_kv(intf, client, "artists", "%zu", artists);
    mpd_send_kv(intf, client, "albums", "%zu", albums);
    mpd_send_kv(intf, client, "songs", "%zu", songs);

    return MPD_CMD_OK;
}

static mpd_cmd_result_t cmd_status(intf_thread_t *intf, mpd_client_t *client, char **argv) {
    VLC_UNUSED(argv);
    intf_sys_t *sys = intf->p_sys;

    vlc_mutex_lock(&sys->pl_lock);
    vlc_playlist_Lock(sys->playlist);
    int volume = lround(vlc_player_aout_GetVolume(sys->player) * 100);

    enum vlc_playlist_playback_repeat repeat = vlc_playlist_GetPlaybackRepeat(sys->playlist);
    enum vlc_playlist_playback_order order = vlc_playlist_GetPlaybackOrder(sys->playlist);
    size_t playlist_length = vlc_playlist_Count(sys->playlist);
    enum vlc_player_state state = vlc_player_GetState(sys->player);
    ssize_t song_index = vlc_playlist_GetCurrentIndex(sys->playlist);
    vlc_tick_t elapsed_time = vlc_player_GetTime(sys->player);
    vlc_tick_t total_time = vlc_player_GetLength(sys->player);

    if (volume >= 0)
        mpd_send_kv(intf, client, "volume", "%d", volume);
    mpd_send_kv(intf, client, "repeat", "%d", repeat != VLC_PLAYLIST_PLAYBACK_REPEAT_NONE);
    mpd_send_kv(intf, client, "random", "%d", order == VLC_PLAYLIST_PLAYBACK_ORDER_RANDOM);
    mpd_send_kv(intf, client, "single", "0");
    mpd_send_kv(intf, client, "consume", "0");
    mpd_send_kv(intf, client, "playlist", "%" PRIu32, sys->playlist_version);
    mpd_send_kv(intf, client, "playlistlength", "%zu", playlist_length);
    mpd_send_kv(intf, client, "state", "%s", state == VLC_PLAYER_STATE_PLAYING ? "play"
                : state == VLC_PLAYER_STATE_PAUSED ? "pause" : "stop");
    if (song_index >= 0) {
        mpd_send_kv(intf, client, "song", "%zd", song_index);
        mpd_send_kv(intf, client, "songid", "%" PRIu64, sys->pl.data[song_index].id);
    }
    if (elapsed_time != VLC_TICK_INVALID && total_time != VLC_TICK_INVALID) {
        double elapsed = secf_from_vlc_tick(elapsed_time);
        double duration = secf_from_vlc_tick(total_time);
        mpd_send_kv(intf, client, "time", "%ld:%ld", lround(elapsed), lround(duration));

        locale_t loc = newlocale(LC_NUMERIC_MASK, "C", NULL);
        locale_t oldloc = uselocale(loc);
        mpd_send_kv(intf, client, "elapsed", "%.3f", elapsed);
        mpd_send_kv(intf, client, "duration", "%.3f", duration);
        if (loc != (locale_t)0) {
            uselocale(oldloc);
            freelocale(loc);
        }
    }
    vlc_playlist_Unlock(sys->playlist);
    vlc_mutex_unlock(&sys->pl_lock);

    return MPD_CMD_OK;
}

static mpd_cmd_result_t cmd_stop(intf_thread_t *intf, mpd_client_t *client, char **argv) {
    VLC_UNUSED(client); VLC_UNUSED(argv);
    intf_sys_t *sys = intf->p_sys;

    vlc_playlist_Lock(sys->playlist);
    vlc_playlist_Stop(sys->playlist);
    vlc_playlist_Unlock(sys->playlist);

    return MPD_CMD_OK;
}

/* tagtypes [{SUBCOMMAND} [ARGUMENT...]] */
static mpd_cmd_result_t cmd_tagtypes(intf_thread_t *intf, mpd_client_t *client, char **argv) {
    /* TODO: handle SUBCOMMAND */
    VLC_UNUSED(argv);

    for (int i = MPD_TAG_ARTIST; i < MPD_TAG_COUNT; i++)
        mpd_send_kv(intf, client, "tagtype", "%s", mpd_tag_names[i]);

    return MPD_CMD_OK;
}

static mpd_cmd_result_t cmd_urlhandlers(intf_thread_t *intf, mpd_client_t *client, char **argv) {
    /* TODO */
    VLC_UNUSED(intf); VLC_UNUSED(client); VLC_UNUSED(argv);

    return MPD_CMD_OK;
}

/* volume {CHANGE} */
static mpd_cmd_result_t cmd_volume(intf_thread_t *intf, mpd_client_t *client, char **argv) {
    intf_sys_t *sys = intf->p_sys;

    double inc;
    PARSE_FLOAT(argv[1], &inc);

    vlc_playlist_Lock(sys->playlist);
    float volume = vlc_player_aout_GetVolume(sys->player);
    volume += inc / 100;
    if (volume < 0)
        volume = 0;
    if (volume > 2)
        volume = 2;
    vlc_player_aout_SetVolume(sys->player, volume);
    vlc_playlist_Unlock(sys->playlist);

    return MPD_CMD_OK;
}

typedef struct {
    const char *name;
    /* Number of arguments (-1: unbound) */
    int min, max;
    mpd_command_t command;
    bool hidden;
} mpd_name_command_t;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
static const mpd_name_command_t mpd_commands[] = {
    /* Keep sorted alphabetically
       TODO: handle more commands */
    { "add", 1, 2, cmd_add },
    { "addid", 1, 2, cmd_addid },
    { "channels", 0, 0, cmd_channels },
    { "clear", 0, 0, cmd_clear },
    { "command_list_begin", 0, 0, cmd_command_list_begin, true },
    { "command_list_end", 0, 0, cmd_command_list_end, true },
    { "command_list_ok_begin", 0, 0, cmd_command_list_ok_begin, true },
    { "commands", 0, 0, cmd_commands },
    { "currentsong", 0, 0, cmd_currentsong },
    { "decoders", 0, 0, cmd_decoders },
    { "delete", 1, 1, cmd_delete },
    { "deleteid", 1, 1, cmd_deleteid },
    { "find", 1, -1, cmd_find },
    { "findadd", 1, -1, cmd_findadd },
    { "idle", 0, -1, cmd_idle },
    { "list", 1, -1, cmd_list },
    { "listpartitions", 0, 0, cmd_listpartitions },
    { "listplaylists", 0, 0, cmd_listplaylists },
    { "lsinfo", 0, 1, cmd_lsinfo },
    { "move", 2, 2, cmd_move },
    { "moveid", 2, 2, cmd_moveid },
    { "next", 0, 0, cmd_next },
    { "noidle", 0, 0, cmd_noidle, true },
    { "outputs", 0, 0, cmd_outputs },
    { "pause", 0, 1, cmd_pause },
    { "ping", 0, 0, cmd_ping },
    { "play", 0, 1, cmd_play },
    { "playid", 0, 1, cmd_playid },
    { "playlistfind", 1, 1, cmd_playlistfind },
    { "playlistinfo", 0, 1, cmd_playlistinfo },
    { "playlistsearch", 1, 1, cmd_playlistsearch },
    { "plchanges", 1, 2, cmd_plchanges },
    { "previous", 0, 0, cmd_previous },
    { "random", 1, 1, cmd_random },
    { "search", 1, -1, cmd_search },
    { "searchadd", 1, -1, cmd_searchadd },
    { "seek", 2, 2, cmd_seek },
    { "seekcur", 1, 1, cmd_seekcur },
    { "seekid", 2, 2, cmd_seekid },
    { "setvol", 1, 1, cmd_setvol },
    { "stats", 0, 0, cmd_stats },
    { "status", 0, 0, cmd_status },
    { "stop", 0, 0, cmd_stop },
    { "tagtypes", 0, -1, cmd_tagtypes },
    { "urlhandlers", 0, 0, cmd_urlhandlers },
    { "volume", 1, 1, cmd_volume },
};
#pragma GCC diagnostic pop

static mpd_cmd_result_t cmd_commands(intf_thread_t *intf, mpd_client_t *client, char **argv) {
    VLC_UNUSED(argv);
    for (size_t i = 0; i < ARRAY_SIZE(mpd_commands); i++)
        if (!mpd_commands[i].hidden)
            mpd_send_kv(intf, client, "command", "%s", mpd_commands[i].name);

    return MPD_CMD_OK;
}

int mpd_command_find_and_call(intf_thread_t *intf, mpd_client_t *client, char **argv) {
    static_assert(offsetof(mpd_name_command_t, name) == 0, "Cast assumption failure");
    mpd_name_command_t *res = bsearch(
        argv, mpd_commands, ARRAY_SIZE(mpd_commands),
        sizeof *mpd_commands, bstrcmp
    );
    if (!res) {
        client->current_command = "";
        mpd_send_error(intf, client, MPD_ERROR_UNKNOWN_CMD,
                       "unknown command \"%s\"", argv[0]);
        msg_Warn(intf, "could not find command %s", argv[0]);
        return MPD_CMD_ERROR;
    }
    if (client->idle && res->command != &cmd_noidle) {
        msg_Warn(intf, "client sent command %s while idling, ignoring", argv[0]);
        return MPD_CMD_SILENT_OK;
    }
    client->current_command = argv[0];

    /* Not counting the command itself */
    int argc = 0;
    while (argv[1 + argc])
        argc++;

    if (argc < res->min || (res->max != -1 && res->max < argc)) {
        mpd_send_error(intf, client, MPD_ERROR_ARG,
                       "wrong number of arguments");
        msg_Warn(intf, "%s called with %d arguments", argv[0], argc);
        return MPD_CMD_ERROR;
    }

    return res->command(intf, client, argv);
}

void mpd_assert_commands_sorted(intf_thread_t *intf) {
    for (size_t i = 1; i < ARRAY_SIZE(mpd_commands); i++) {
        const char *a = mpd_commands[i - 1].name;
        const char *b = mpd_commands[i].name;
        if (strcmp(a, b) >= 0) {
            msg_Err(intf, "Commands %s and %s are not sorted", a, b);
            abort();
        }
    }
}
