#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <time.h>
#ifndef _WIN32
# include <locale.h>
#endif
#ifdef HAVE_XLOCALE_H
# include <xlocale.h>
#endif

#include <vlc_common.h>
#include <vlc_url.h>

#include "medialibrary.h"
#include "protocol.h"

void mpd_ml_print_playlist_list(
    intf_thread_t *intf,
    mpd_client_t *client,
    vlc_ml_playlist_list_t *playlist_list
) {
    if (!playlist_list)
        return;

    for (size_t i = 0; i < playlist_list->i_nb_items; i++) {
        vlc_ml_playlist_t *playlist = &playlist_list->p_items[i];
        mpd_send_kv(intf, client, "playlist", "%s", playlist->psz_mrl);
    }
}

void mpd_ml_print_folder_list(
    intf_thread_t *intf,
    mpd_client_t *client,
    vlc_ml_folder_list_t *folder_list
) {
    if (!folder_list)
        return;

    for (size_t i = 0; i < folder_list->i_nb_items; i++) {
        vlc_ml_folder_t *folder = &folder_list->p_items[i];

        char *dir_path = mpd_mrl2path(intf, folder->psz_mrl);
        mpd_send_kv(intf, client, "directory", "%s", dir_path);
        free(dir_path);
    }
}

void mpd_ml_print_media(
    intf_thread_t *intf,
    mpd_client_t *client,
    vlc_ml_media_t *media
) {
    intf_sys_t *sys = intf->p_sys;

    if (!media)
        return;

    if (media->i_type != VLC_ML_MEDIA_TYPE_AUDIO
        || media->i_subtype != VLC_ML_MEDIA_SUBTYPE_ALBUMTRACK)
        return;

    if (!media->p_files)
        return;

    vlc_ml_file_t *file = NULL;
    for (size_t i = 0; i < media->p_files->i_nb_items; i++) {
        if (media->p_files->p_items[i].i_type == VLC_ML_FILE_TYPE_MAIN) {
            file = &media->p_files->p_items[i];
            break;
        }
    }
    if (!file)
        return;

    char *file_path = mpd_mrl2path(intf, file->psz_mrl);
    if (!file_path)
        return;

    vlc_ml_artist_t *artist = vlc_ml_get_artist(sys->medialibrary, media->album_track.i_artist_id);
    vlc_ml_album_t *album = vlc_ml_get_album(sys->medialibrary, media->album_track.i_album_id);
    vlc_ml_genre_t *genre = vlc_ml_get_genre(sys->medialibrary, media->album_track.i_genre_id);

    mpd_send_kv(intf, client, "file", "%s", file_path);
    free(file_path);
    struct tm modification_date;
    if (gmtime_r(&file->i_last_modification_date, &modification_date)) {
        /* RFC-3339 format */
        char buf[21];
        strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &modification_date);
        mpd_send_kv(intf, client, "Last-Modified", "%s", buf);
    }
    mpd_send_kv(intf, client, "Title", "%s", media->psz_title);
    if (artist)
        mpd_send_kv(intf, client, "Artist", "%s", artist->psz_name);
    if (album) {
        mpd_send_kv(intf, client, "Album", "%s", album->psz_title);
        mpd_send_kv(intf, client, "AlbumArtist", "%s", album->psz_artist);
    }
    mpd_send_kv(intf, client, "Track", "%d", media->album_track.i_track_nb);
    mpd_send_kv(intf, client, "Disc", "%d", media->album_track.i_disc_nb);
    if (genre)
        mpd_send_kv(intf, client, "Genre", "%s", genre->psz_name);
    mpd_send_kv(intf, client, "Date", "%" PRIi32, media->i_year);
    mpd_send_kv(intf, client, "Time", "%ld", lround(media->i_duration / 1e3));

    locale_t loc = newlocale(LC_NUMERIC_MASK, "C", NULL);
    locale_t oldloc = uselocale(loc);
    mpd_send_kv(intf, client, "duration", "%.3f", media->i_duration / 1e3);
    if (loc != (locale_t)0) {
        uselocale(oldloc);
        freelocale(loc);
    }

    vlc_ml_release(artist);
    vlc_ml_release(album);
    vlc_ml_release(genre);
}

void mpd_ml_print_media_list(
    intf_thread_t *intf,
    mpd_client_t *client,
    vlc_ml_media_list_t *media_list
) {
    if (!media_list)
        return;

    for (size_t i = 0; i < media_list->i_nb_items; i++)
        mpd_ml_print_media(intf, client, &media_list->p_items[i]);
}

void mpd_ml_print_item(
    intf_thread_t *intf,
    mpd_client_t *client,
    input_item_t *item
) {
    if (!item)
        return;

    vlc_mutex_lock(&item->lock);

    if (item->i_type != ITEM_TYPE_FILE)
        goto ignore;

#define META(x) input_item_GetMetaLocked(item, vlc_meta_ ## x)
    const char *title = META(Title);
    const char *artist = META(Artist);
    const char *album = META(Album);
    const char *album_artist = META(AlbumArtist);
    const char *track = META(TrackNumber);
    const char *disc = META(DiscNumber);
    const char *genre = META(Genre);
    const char *date = META(Date);
#undef META

    char *file_path = mpd_mrl2path(intf, item->psz_uri);
    if (!file_path)
        goto ignore;
    mpd_send_kv(intf, client, "file", "%s", file_path);
    free(file_path);
    if (title)
        mpd_send_kv(intf, client, "Title", "%s", title);
    if (artist)
        mpd_send_kv(intf, client, "Artist", "%s", artist);
    if (album)
        mpd_send_kv(intf, client, "Album", "%s", album);
    if (album_artist)
        mpd_send_kv(intf, client, "AlbumArtist", "%s", album_artist);
    if (track)
        mpd_send_kv(intf, client, "Track", "%s", track);
    if (disc)
        mpd_send_kv(intf, client, "Disc", "%s", disc);
    if (genre)
        mpd_send_kv(intf, client, "Genre", "%s", genre);
    if (date)
        mpd_send_kv(intf, client, "Date", "%s", date);
    vlc_tick_t duration = item->i_duration;
    if (duration != INPUT_DURATION_INDEFINITE && duration != INPUT_DURATION_UNSET) {
        double d = secf_from_vlc_tick(duration);
        mpd_send_kv(intf, client, "Time", "%ld", lround(d));

        locale_t loc = newlocale(LC_NUMERIC_MASK, "C", NULL);
        locale_t oldloc = uselocale(loc);
        mpd_send_kv(intf, client, "duration", "%.3f", d);
        if (loc != (locale_t)0) {
            uselocale(oldloc);
            freelocale(loc);
        }
    }

ignore:
    vlc_mutex_unlock(&item->lock);
}

char *mpd_ml_media_mrl(vlc_ml_media_t *media) {
    if (!media || !media->p_files)
        return NULL;

    for (size_t i = 0; i < media->p_files->i_nb_items; i++)
        if (media->p_files->p_items[i].i_type == VLC_ML_FILE_TYPE_MAIN)
            return strdup(media->p_files->p_items[i].psz_mrl);

    return NULL;
}

static void mpd_ml_recursive_list_folder(vlc_medialibrary_t *ml, mpd_mrl_vector_t *vec, int64_t folder_id) {
    vlc_ml_media_list_t *media_list;
    int rc = vlc_ml_list(ml, VLC_ML_LIST_FOLDER_MEDIA, NULL, folder_id, &media_list);
    if (rc != VLC_SUCCESS)
        media_list = NULL;
    if (media_list) {
        for (size_t i = 0; i < media_list->i_nb_items; i++) {
            char *mrl = mpd_ml_media_mrl(&media_list->p_items[i]);
            if (mrl)
                vlc_vector_push(vec, mrl);
        }
    }
    vlc_ml_release(media_list);

    vlc_ml_folder_list_t *subfolder_list;
    rc = vlc_ml_list(ml, VLC_ML_LIST_SUBFOLDERS, NULL, folder_id, &subfolder_list);
    if (rc != VLC_SUCCESS)
        subfolder_list = NULL;
    if (subfolder_list) {
        for (size_t i = 0; i < subfolder_list->i_nb_items; i++) {
            mpd_ml_recursive_list_folder(ml, vec, subfolder_list->p_items[i].i_id);
        }
    }
    vlc_ml_release(subfolder_list);
}

mpd_mrl_vector_t mpd_ml_recursive_list(intf_thread_t *intf, const char *mrl) {
    intf_sys_t *sys = intf->p_sys;

    mpd_mrl_vector_t vec;
    vlc_vector_init(&vec);

    vlc_ml_media_t *media = vlc_ml_get_media_by_mrl(sys->medialibrary, mrl);
    if (media) {
        char *new_mrl = strdup(mrl);
        if (new_mrl)
            vlc_vector_push(&vec, new_mrl);
        vlc_ml_release(media);
        return vec;
    }

    vlc_ml_folder_t *folder = vlc_ml_get_folder_by_mrl(sys->medialibrary, mrl);
    if (folder) {
        mpd_ml_recursive_list_folder(sys->medialibrary, &vec, folder->i_id);
        vlc_ml_release(folder);
    }

    return vec;
}

vlc_ml_media_list_t *mpd_ml_narrow_from_filter(intf_thread_t *intf, mpd_filter_t **filter) {
    intf_sys_t *sys = intf->p_sys;

    switch ((*filter)->kind) {
    case MPD_FILTER_TAG_EQUAL:
        switch ((*filter)->tag) {
            /* Sorry, I didn't feel like writing this 3 times, so here is an ugly macro. */
#define DO(WHAT, FIELD, ...)                                            \
            {                                                           \
                vlc_ml_ ## WHAT ## _list_t *list =                      \
                    vlc_ml_list_ ## WHAT ## s(sys->medialibrary, NULL, ##__VA_ARGS__); \
                if (!list)                                              \
                    goto fallback;                                      \
                int64_t id = -1;                                        \
                for (size_t i = 0; i < list->i_nb_items; i++) {         \
                    if (strcmp((*filter)->value, list->p_items[i].FIELD) == 0) { \
                        id = list->p_items[i].i_id;                     \
                        break;                                          \
                    }                                                   \
                }                                                       \
                vlc_ml_release(list);                                   \
                mpd_filter_free(*filter);                               \
                *filter = NULL;                                         \
                if (id < 0)                                             \
                    return calloc(1, sizeof (vlc_ml_media_list_t));     \
                return vlc_ml_list_ ## WHAT ## _tracks(sys->medialibrary, NULL, id); \
            }
        case MPD_TAG_ALBUM:
            DO(album, psz_title);
        case MPD_TAG_ARTIST:
            DO(artist, psz_name, true);
        case MPD_TAG_GENRE:
            DO(genre, psz_name);
#undef DO
        default:
            break;
        }
        break;

    case MPD_FILTER_AND: {
        /* Choose the best subfilter to narrow the list from */
        union {
            ssize_t arr[3];
            struct { ssize_t album, artist, genre; };
        } indices = {{-1, -1, -1}};
        for (ssize_t i = (ssize_t)(*filter)->and.size - 1; i >= 0; i--) {
            mpd_filter_t *f = (*filter)->and.data[i];
            if (f->kind == MPD_FILTER_TAG_EQUAL) {
                switch (f->tag) {
                case MPD_TAG_ALBUM:
                    indices.album = i;
                    break;
                case MPD_TAG_ARTIST:
                    indices.artist = i;
                    break;
                case MPD_TAG_GENRE:
                    indices.genre = i;
                    break;
                default:
                    break;
                }
            }
        }
        ssize_t i;
        for (int j = 0; j < 3; j++)
            if ((i = indices.arr[j]) >= 0)
                break;
        if (i < 0)
            goto fallback;

        /* Narrow the list from this subfilter */
        mpd_filter_t *f = (*filter)->and.data[i];
        vlc_ml_media_list_t *list = mpd_ml_narrow_from_filter(intf, &f);
        if (!f)
            vlc_vector_remove(&(*filter)->and, i);
        return list;
    }

    default:
        break;
    }

fallback:
    return vlc_ml_list_audio_media(sys->medialibrary, NULL);
}

/* This callback should only touch at locked fields in intf_sys_t and
 * call mpd_send_idle_events_update, because it can be called by any
 * thread.
 */
static void handle_event(void *data, const vlc_ml_event_t *event) {
    intf_thread_t *intf = data;
    intf_sys_t *sys = intf->p_sys;

    switch (event->i_type) {
    case VLC_ML_EVENT_MEDIA_ADDED:
    case VLC_ML_EVENT_MEDIA_UPDATED:
    case VLC_ML_EVENT_MEDIA_DELETED:
    case VLC_ML_EVENT_ARTIST_ADDED:
    case VLC_ML_EVENT_ARTIST_UPDATED:
    case VLC_ML_EVENT_ARTIST_DELETED:
    case VLC_ML_EVENT_ALBUM_ADDED:
    case VLC_ML_EVENT_ALBUM_UPDATED:
    case VLC_ML_EVENT_ALBUM_DELETED:
    case VLC_ML_EVENT_GROUP_ADDED:
    case VLC_ML_EVENT_GROUP_UPDATED:
    case VLC_ML_EVENT_GROUP_DELETED:
    case VLC_ML_EVENT_PLAYLIST_ADDED:
    case VLC_ML_EVENT_PLAYLIST_UPDATED:
    case VLC_ML_EVENT_PLAYLIST_DELETED:
    case VLC_ML_EVENT_GENRE_ADDED:
    case VLC_ML_EVENT_GENRE_UPDATED:
    case VLC_ML_EVENT_GENRE_DELETED:
    case VLC_ML_EVENT_BOOKMARKS_ADDED:
    case VLC_ML_EVENT_BOOKMARKS_UPDATED:
    case VLC_ML_EVENT_BOOKMARKS_DELETED:
    case VLC_ML_EVENT_FOLDER_ADDED:
    case VLC_ML_EVENT_FOLDER_UPDATED:
    case VLC_ML_EVENT_FOLDER_DELETED:
        mpd_send_idle_events_update(intf, MPD_SUBSYSTEM_DATABASE);
        break;

    case VLC_ML_EVENT_DISCOVERY_STARTED:
    case VLC_ML_EVENT_DISCOVERY_COMPLETED:
    case VLC_ML_EVENT_DISCOVERY_FAILED:
        mpd_send_idle_events_update(intf, MPD_SUBSYSTEM_UPDATE);
        break;

    case VLC_ML_EVENT_ENTRY_POINT_ADDED:
    case VLC_ML_EVENT_ENTRY_POINT_REMOVED:
    case VLC_ML_EVENT_ENTRY_POINT_BANNED:
    case VLC_ML_EVENT_ENTRY_POINT_UNBANNED: {
        ssize_t written = write(sys->pipe[1], (char[]){MPD_GEN_ML_ENTRY_POINTS}, 1);
        VLC_UNUSED(written);
        break;
    }

    default:
        break;
    }
}

int mpd_ml_callbacks_setup(intf_thread_t *intf) {
    intf_sys_t *sys = intf->p_sys;

    sys->ml_event_callback = vlc_ml_event_register_callback(
        sys->medialibrary, &handle_event, intf
    );
    if (!sys->ml_event_callback) {
        msg_Err(intf, "Failed to add medialibrary listener");
        return -1;
    }

    return 0;
}

static int entry_point_cmp(const void *a, const void *b) {
    const mpd_entry_point_t *c = a, *d = b;
    return strcmp(c->name, d->name);
}

static int mpd_gen_entry_points_names(intf_thread_t *intf) {
    intf_sys_t *sys = intf->p_sys;

    vlc_mutex_lock(&sys->ep_lock);
    mpd_entry_point_vec_t *eps = &sys->entry_points;

    for (size_t i = 0; i < eps->size; i++) {
        mpd_entry_point_t *ep = &eps->data[i];

        free(ep->name);
        ep->name = NULL;

        /* Generate the name naively (directory name) */
        const char *slash = strrchr(ep->mrl, '/');
        if (!slash) {
            vlc_mutex_unlock(&sys->ep_lock);
            msg_Err(intf, "mrl without slash");
            return VLC_EINVAL;
        }
        if (slash[1] == '\0') {
            vlc_mutex_unlock(&sys->ep_lock);
            msg_Err(intf, "mrl with trailing slash");
            return VLC_EINVAL;
        }

        ep->name = vlc_uri_decode_duplicate(slash + 1);
        if (!ep->name) {
            vlc_mutex_unlock(&sys->ep_lock);
            return -errno;
        }
    }

    qsort(eps->data, eps->size, sizeof *eps->data, entry_point_cmp);

    /* Check if some mount points have the same name */
    for (size_t i = 0; i < eps->size; i++) {
        size_t j = 1;
        while (i + j < eps->size && strcmp(eps->data[i].name, eps->data[i + j].name) == 0)
            j++;

        if (j == 1)
            continue;

        /* Generate unique names */
        for (size_t k = i; k < i + j; k++) {
            mpd_entry_point_t *ep = &eps->data[k];

            char *path = vlc_uri_decode_duplicate(ep->mrl);
            if (!path) {
                vlc_mutex_unlock(&sys->ep_lock);
                return VLC_ENOMEM;
            }
            for (char *s = path; (s = strchr(s, '/')); s++)
                *s = '|';

            char *newname = NULL;
            int rc = asprintf(&newname, "%s<%s>", ep->name, path);
            free(path);
            if (rc < 0) {
                vlc_mutex_unlock(&sys->ep_lock);
                return VLC_ENOMEM;
            }
            free(ep->name);
            ep->name = newname;
        }
    }
    vlc_mutex_unlock(&sys->ep_lock);

    return 0;
}

int mpd_ml_gen_entry_points(intf_thread_t *intf) {
    intf_sys_t *sys = intf->p_sys;

    vlc_mutex_lock(&sys->ep_lock);
    for (size_t i = 0; i < sys->entry_points.size; i++) {
        free(sys->entry_points.data[i].mrl);
        free(sys->entry_points.data[i].name);
    }
    vlc_vector_clear(&sys->entry_points);

    vlc_ml_folder_list_t *eps = vlc_ml_list_entry_points(sys->medialibrary, NULL);
    if (!eps) {
        vlc_mutex_unlock(&sys->ep_lock);
        return -1;
    }

    for (size_t i = 0; i < eps->i_nb_items; i++) {
        char *mrl = strdup(eps->p_items[i].psz_mrl);
        if (!mrl) {
            vlc_mutex_unlock(&sys->ep_lock);
            vlc_ml_release(eps);
            return VLC_ENOMEM;
        }

        /* Remove trailing slash */
        if (mrl[strlen(mrl) - 1] == '/')
            mrl[strlen(mrl) - 1] = '\0';

        vlc_vector_push(&sys->entry_points, (mpd_entry_point_t){ .mrl = mrl });
    }
    vlc_mutex_unlock(&sys->ep_lock);

    vlc_ml_release(eps);

    return mpd_gen_entry_points_names(intf);
}
