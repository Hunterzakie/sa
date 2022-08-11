#ifndef VLC_MPD_MEDIALIBRARY_H
#define VLC_MPD_MEDIALIBRARY_H

#include "mpd.h"
#include "filter.h"

typedef struct VLC_VECTOR(char *) mpd_mrl_vector_t;

void mpd_ml_print_media(intf_thread_t *intf, mpd_client_t *client, vlc_ml_media_t *media);
void mpd_ml_print_item(intf_thread_t *intf, mpd_client_t *client, input_item_t *item);

void mpd_ml_print_playlist_list(intf_thread_t *intf, mpd_client_t *client, vlc_ml_playlist_list_t *playlist_list);
void mpd_ml_print_folder_list(intf_thread_t *intf, mpd_client_t *client, vlc_ml_folder_list_t *folder_list);
void mpd_ml_print_media_list(intf_thread_t *intf, mpd_client_t *client, vlc_ml_media_list_t *media_list);

char *mpd_ml_media_mrl(vlc_ml_media_t *media);

mpd_mrl_vector_t mpd_ml_recursive_list(intf_thread_t *intf, const char *mrl);

vlc_ml_media_list_t *mpd_ml_narrow_from_filter(intf_thread_t *intf, mpd_filter_t **filter);

int mpd_ml_gen_entry_points(intf_thread_t *intf);

int mpd_ml_callbacks_setup(intf_thread_t *intf);

#endif
