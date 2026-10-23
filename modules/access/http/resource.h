/*****************************************************************************
 * resource.h: HTTP resource common code
 *****************************************************************************
 * Copyright (C) 2015 Rémi Denis-Courmont
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifndef VLC_HTTP_RESOURCE_H
#define VLC_HTTP_RESOURCE_H 1

#include <stdbool.h>
#include <stddef.h>

#include <vlc_tick.h>

/**
 * \defgroup http_res Resources
 * Remote HTTP resources identified by a URL
 * \ingroup http
 * @{
 */

struct vlc_http_msg;
struct vlc_http_mgr;
struct vlc_http_resource;
struct vlc_http_stream;

struct vlc_http_resource_cbs
{
    int (*request_format)(const struct vlc_http_resource *,
                          struct vlc_http_msg *, void *);
    int (*response_validate)(const struct vlc_http_resource *,
                             const struct vlc_http_msg *, void *);
};

struct vlc_http_resource
{
    const struct vlc_http_resource_cbs *cbs;
    struct vlc_http_msg *response;
    struct vlc_http_mgr *manager;
    struct vlc_http_stream *icy_stream;
    unsigned icy_bitrate; /* bits/s from icy-br, 0 if unknown */
    bool secure;
    bool negotiate;
    bool failure;
    bool want_icy;
    char *host;
    unsigned port;
    char *authority;
    char *path;
    char *username;
    char *password;
    char *agent;
    char *referrer;
};

int vlc_http_res_init(struct vlc_http_resource *,
                      const struct vlc_http_resource_cbs *cbs,
                      struct vlc_http_mgr *mgr,
                      const char *uri, const char *ua, const char *ref);

/**
 * Destroys an HTTP resource.
 *
 * Releases all underlying resources allocated or held by the HTTP resource
 * object.
 */
void vlc_http_res_destroy(struct vlc_http_resource *);

struct vlc_http_msg *vlc_http_res_open(struct vlc_http_resource *res, void *);
int vlc_http_res_get_status(struct vlc_http_resource *res);

/**
 * Gets redirection URL.
 *
 * Checks if the resource URL lead to a redirection. If so, return the redirect
 * location.
 *
 * @return Heap-allocated URL or NULL if no redirection.
 */
char *vlc_http_res_get_redirect(struct vlc_http_resource *);

/**
 * Gets MIME type.
 *
 * @return Heap-allocated MIME type string, or NULL if unknown.
 */
char *vlc_http_res_get_type(struct vlc_http_resource *);

/**
 * Reads data.
 */
block_t *vlc_http_res_read(struct vlc_http_resource *);

/**
 * Opts into ICY in-band metadata.
 *
 * Requests SHOUTcast/Icecast metadata and, if the server provides it, inserts
 * a stream filter that strips the in-band metadata from the read payload. Must
 * be called before the resource is opened.
 */
void vlc_http_res_set_icy(struct vlc_http_resource *, bool enabled);

/**
 * Gets the ICY station name.
 *
 * @return The Icy-Name header value, or NULL if absent.
 */
const char *vlc_http_res_get_icy_name(struct vlc_http_resource *);

/**
 * Gets the ICY station genre.
 *
 * @return The Icy-Genre header value, or NULL if absent.
 */
const char *vlc_http_res_get_icy_genre(struct vlc_http_resource *);

/**
 * Gets the current ICY now-playing title and the playback time at which it
 * takes effect (VLC_TICK_INVALID if the bitrate is unknown; does not consume).
 * Caller frees *titlep on success.
 */
bool vlc_http_res_get_timed_now_playing(struct vlc_http_resource *,
                                        char **restrict titlep,
                                        vlc_tick_t *restrict ptsp);

int vlc_http_res_set_login(struct vlc_http_resource *res,
                           const char *username, const char *password);
char *vlc_http_res_get_basic_realm(struct vlc_http_resource *res);

/** @} */
#endif
