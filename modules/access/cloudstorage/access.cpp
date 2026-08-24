/*****************************************************************************
 * access.cpp: cloud storage access module using libcloudstorage
 *****************************************************************************
 * Copyright (C) 2025 VideoLabs and VideoLAN
 *
 * Authors: William Ung <williamung@msn.com>
 *          Diogo Silva <dbtdsilva@gmail.com>
 *          Maksym Yemelianenko <max.yemelianenko@gmail.com>
 *          Ashish Vardhan Karanam <ashishkaranam06@gmail.com>
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
#include "cloudstorage_handler.h"
#include "token_cache.h"

#include <vlc_common.h>
#include <vlc_access.h>
#include <vlc_stream.h>
#include <vlc_url.h>
#include <vlc_tick.h>
#include <vlc_variables.h>

extern "C" {
#include "../http/connmgr.h"
#include "../http/message.h"
}

#if __has_include("cloud_keys.h")
#include "cloud_keys.h"
#endif

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

    access_sys_t* p_sys = vlc_object_create<access_sys_t>(p_this);
    if (unlikely(p_sys == NULL))
        return VLC_ENOMEM;

    p_access->p_sys = p_sys;

    vlc_mutex_init(&p_sys->lock);
    vlc_cond_init(&p_sys->wait);
    p_sys->req_finished = true;
    p_sys->req_error = false;
    p_sys->aborted = false;

    int err;
    if ((err = ParseMRL(p_access)) != VLC_SUCCESS) {
        Close(p_this);
        return err;
    }

    std::string target_provider;
    if (p_sys->url.psz_host) {
        if (strcmp(p_sys->url.psz_host, "auth") == 0) {
            const char *path = p_sys->url.psz_path ? p_sys->url.psz_path : "/";
            std::string path_str = path;
            if (!path_str.empty() && path_str.front() == '/')
                path_str.erase(0, 1);
      size_t slash = path_str.find('/');
      target_provider =
          (slash == std::string::npos) ? path_str : path_str.substr(0, slash);
    } else {
      target_provider = p_sys->url.psz_host;
    }
  }

  if (!target_provider.empty()) {
    char *psz_id = nullptr;
    char *psz_secret = nullptr;

    if (target_provider == "google" || target_provider == "dropbox" ||
        target_provider == "onedrive" || target_provider == "box" ||
        target_provider == "pcloud") {
      std::string opt_id = "cloudstorage-" + target_provider + "-client-id";
      std::string opt_secret =
          "cloudstorage-" + target_provider + "-client-secret";

      psz_id = var_InheritString(p_access, opt_id.c_str());
      psz_secret = var_InheritString(p_access, opt_secret.c_str());
    }

    if (psz_id && *psz_id)
      p_sys->hints["client_id"] = psz_id;
    if (psz_secret && *psz_secret)
      p_sys->hints["client_secret"] = psz_secret;

    free(psz_id);
    free(psz_secret);

    if (p_sys->hints["client_id"].empty()) {
#ifdef HAS_GOOGLE_DRIVE_KEY
      if (target_provider == "google" || target_provider == "googledrive") {
        p_sys->hints["client_id"] = GOOGLE_DRIVE_APP_ID;
        p_sys->hints["client_secret"] = GOOGLE_DRIVE_APP_SECRET;
      }
#endif
#ifdef HAS_DROPBOX_KEY
      if (target_provider == "dropbox") {
        p_sys->hints["client_id"] = DROPBOX_APP_ID;
        p_sys->hints["client_secret"] = DROPBOX_APP_SECRET;
      }
#endif
#ifdef HAS_ONEDRIVE_KEY
      if (target_provider == "onedrive") {
        p_sys->hints["client_id"] = ONEDRIVE_APP_ID;
        p_sys->hints["client_secret"] = ONEDRIVE_APP_SECRET;
      }
#endif
#ifdef HAS_BOX_KEY
      if (target_provider == "box") {
        p_sys->hints["client_id"] = BOX_APP_ID;
        p_sys->hints["client_secret"] = BOX_APP_SECRET;
      }
#endif
#ifdef HAS_PCLOUD_KEY
      if (target_provider == "pcloud") {
        p_sys->hints["client_id"] = PCLOUD_APP_ID;
        p_sys->hints["client_secret"] = PCLOUD_APP_SECRET;
    }
#endif
    }
  }

    if (!p_sys->url.psz_protocol || strcmp(p_sys->url.psz_protocol, "cloudstorage") != 0) {
        Close(p_this);
        return VLC_EGENERIC;
    }

    return HandleCloudProviderRequest(p_access);
}

void Close(vlc_object_t* p_this) {
    stream_t* p_access = (stream_t*)p_this;
    auto p_sys = reinterpret_cast<access_sys_t*>(p_access->p_sys);
    if (p_sys) {
        vlc_mutex_lock(&p_sys->lock);
        p_sys->aborted = true;
        vlc_cond_broadcast(&p_sys->wait);
        vlc_mutex_unlock(&p_sys->lock);

        if (p_sys->current_req) {
            p_sys->current_req->cancel();
        }

        vlc_mutex_lock(&p_sys->lock);
        while (!p_sys->req_finished) {
            vlc_cond_wait(&p_sys->wait, &p_sys->lock);
        }
        p_access->p_sys = nullptr;
        vlc_mutex_unlock(&p_sys->lock);

        p_sys->current_req.reset();
        p_sys->chunks.clear();
        p_sys->hints.clear();
        p_sys->provider.reset();
        p_sys->current_item.reset();
        p_sys->token.clear();

        vlc_UrlClean(&p_sys->url);
        free(p_sys->alloc_path);
        free(p_sys->alloc_username);

        vlc_object_delete(p_sys);
    }
}
