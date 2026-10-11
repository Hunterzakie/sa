/*****************************************************************************
 * access.cpp: cloud storage access module using libcloudstorage
 *****************************************************************************
 * Copyright (C) 2025 VideoLabs and VideoLAN
 *
 * Authors: William Ung <williamung@msn.com>
 *          Diogo Silva <dbtdsilva@gmail.com>
 *          Maksym Yemelianenko <max.yemelianenko@gmail.com>
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

#include "access.h"
#include "google_api_handler.h"
#include "cloudstorage_handler.h"
#include "token_cache.h"

#include <vlc_common.h>
#include <vlc_access.h>
#include <vlc_stream.h>
#include <vlc_url.h>
#include <vlc_tick.h>

extern "C" {
#include "../http/connmgr.h"
#include "../http/message.h"
}

#include <memory>
#include <cstring>

static int ParseMRL(stream_t* p_access) {
    auto p_sys = reinterpret_cast<access_sys_t*>(p_access->p_sys);
    char* fixed = vlc_uri_fixup(p_access->psz_url);
    int err = vlc_UrlParse(&p_sys->url, fixed);
    free(fixed);
    if (err != VLC_SUCCESS) return err;
    if (!p_sys->url.psz_path) {
        p_sys->alloc_path = strdup("/");
        if (unlikely(p_sys->alloc_path == NULL))
            return VLC_ENOMEM;
        p_sys->url.psz_path = p_sys->alloc_path;
    }
    if (!p_sys->url.psz_username) {
        const char* user = getenv("USER");
        p_sys->alloc_username = strdup(user ? user : "vlc-user");
        if (unlikely(p_sys->alloc_username == NULL))
            return VLC_ENOMEM;
        p_sys->url.psz_username = p_sys->alloc_username;
    }
    return VLC_SUCCESS;
}

int Open(vlc_object_t* p_this) {
    stream_t* p_access = (stream_t*)p_this;

    access_sys_t* p_sys = (access_sys_t*)vlc_obj_malloc(p_this, sizeof(*p_sys));
    if (unlikely(p_sys == NULL))
        return VLC_ENOMEM;

    new (p_sys) access_sys_t{};
    p_access->p_sys = p_sys;

    int err;
    if ((err = ParseMRL(p_access)) != VLC_SUCCESS) {
        Close(p_this);
        return err;
    }

    if (p_sys->url.psz_host && strstr(p_sys->url.psz_host, "googleapis.com")) {
        return HandleGoogleApiRequest(p_access);
    }

    if (p_sys->url.psz_protocol && strcmp(p_sys->url.psz_protocol, "https") == 0) {
        Close(p_this);
        return VLC_EGENERIC;
    }

    return HandleCloudProviderRequest(p_access);
}

void Close(vlc_object_t* p_this) {
    stream_t* p_access = (stream_t*)p_this;
    auto p_sys = reinterpret_cast<access_sys_t*>(p_access->p_sys);
    if (p_sys) {
        if (p_sys->is_google_api_request) {
            if (p_sys->http_resp) {
                vlc_http_msg_destroy(p_sys->http_resp);
                p_sys->http_resp = NULL;
            }
            if (p_sys->http_mgr) {
                vlc_http_mgr_destroy(p_sys->http_mgr);
                p_sys->http_mgr = NULL;
            }
        }

        vlc_UrlClean(&p_sys->url);
        free(p_sys->alloc_path);
        free(p_sys->alloc_username);
        p_sys->~access_sys_t();
        p_access->p_sys = nullptr;
    }
}
