/*****************************************************************************
 * token_cache.cpp: Implementation for token persistence
 *****************************************************************************
 * Copyright (C) 2025 VideoLabs and VideoLAN
 *
 * Authors: Maksym Yemelianenko <max.yemelianenko@gmail.com>
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
#include "token_cache.h"

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <cstring>
#include <vlc_common.h>
#include <vlc_keystore.h>
#include <vlc_url.h>
#include <vlc_access.h>

static inline void build_credentials_url(const vlc_url_t* src, vlc_url_t* dst)
{
    memset(dst, 0, sizeof(*dst));
    dst->psz_protocol = (char*)"https";
    dst->psz_host = src ? src->psz_host : NULL;
    dst->psz_username = src ? src->psz_username : NULL;
}

static std::string keystore_get_impl(vlc_object_t* obj, const vlc_url_t* url)
{
    if (!obj || !url || !url->psz_host || !*url->psz_host)
        return {};

    const char* user = url->psz_username;
    const char* server = url->psz_host;

    vlc_keystore* ks = vlc_keystore_create(obj);
    if (!ks)
        return {};

    const char* values[KEY_MAX];
    VLC_KEYSTORE_VALUES_INIT(values);
    values[KEY_PROTOCOL] = "https";
    values[KEY_SERVER] = server;

    std::string token;
    vlc_keystore_entry* entries = nullptr;
    unsigned count = 0;

    if (user && *user) {
        values[KEY_USER] = user;
        count = vlc_keystore_find(ks, values, &entries);
        if (count > 0 && entries[0].p_secret && entries[0].i_secret_len > 0)
            token.assign(reinterpret_cast<const char*>(entries[0].p_secret), entries[0].i_secret_len);
    } else {
        count = vlc_keystore_find(ks, values, &entries);
        if (count == 1 && entries[0].p_secret && entries[0].i_secret_len > 0)
            token.assign(reinterpret_cast<const char*>(entries[0].p_secret), entries[0].i_secret_len);
    }

    if (entries)
        vlc_keystore_release_entries(entries, count);
    vlc_keystore_release(ks);

    return token;
}

static void keystore_set_impl(vlc_object_t* obj, const vlc_url_t* url, const std::string& token)
{
    if (!obj || !url || token.empty() || !url->psz_host || !*url->psz_host)
        return;

    const char* user = url->psz_username;
    const char* server = url->psz_host;

    vlc_keystore* ks = vlc_keystore_create(obj);
    if (!ks)
        return;

    const char* values[KEY_MAX];
    VLC_KEYSTORE_VALUES_INIT(values);
    values[KEY_PROTOCOL] = "https";
    values[KEY_SERVER] = server;
    values[KEY_USER] = user;

    if (vlc_keystore_store(ks, values,
                           reinterpret_cast<const uint8_t*>(token.c_str()), -1,
                           "CloudStorage Token") != VLC_SUCCESS) {
        msg_Warn(obj, "Failed to store cloudstorage token for %s@%s",
                 user ? user : "(null)", server);
    } else {
        msg_Dbg(obj, "Stored cloudstorage token for %s@%s",
                user ? user : "(null)", server);
    }

    vlc_keystore_release(ks);
}

std::string TokenCache::get(stream_t* access, const vlc_url_t* url)
{
    if (!access || !url) return {};
    msg_Dbg(access, "Keystore find: proto=https server=%s user=%s",
            url->psz_host ? url->psz_host : "(null)",
            url->psz_username ? url->psz_username : "(null)");
    return keystore_get_impl(VLC_OBJECT(access), url);
}

void TokenCache::set(stream_t* access, const vlc_url_t* url, const std::string& token)
{
    if (!access || !url || token.empty()) return;
    msg_Dbg(access, "Keystore store: proto=https server=%s user=%s",
            url->psz_host ? url->psz_host : "(null)",
            url->psz_username ? url->psz_username : "(null)");
    keystore_set_impl(VLC_OBJECT(access), url, token);
}

void TokenCache::clear(stream_t* access, const vlc_url_t* url)
{
    if (!access || !url) return;

    const char* user = url->psz_username;
    const char* server = url->psz_host;

    vlc_keystore *p_keystore = vlc_keystore_create(VLC_OBJECT(access));
    if (p_keystore == NULL)
        return;

    const char *ppsz_values[KEY_MAX] = { 0 };
    ppsz_values[KEY_PROTOCOL] = "https";
    ppsz_values[KEY_SERVER] = server;
    ppsz_values[KEY_USER] = user;

    unsigned removed = vlc_keystore_remove(p_keystore, ppsz_values);

    if (removed > 0)
        msg_Info(access, "Removed %u token entrie(s) for %s@%s", removed,
                 user ? user : "(null)", server ? server : "(null)");
    else
        msg_Dbg(access, "No token entries to remove for %s@%s",
                user ? user : "(null)", server ? server : "(null)");

    vlc_keystore_release(p_keystore);
}

std::string TokenCache::get(vlc_object_t* obj, const vlc_url_t* url)
{
    if (!obj || !url) return {};
    msg_Dbg(obj, "Keystore find: proto=https server=%s user=%s",
            url->psz_host ? url->psz_host : "(null)",
            url->psz_username ? url->psz_username : "(null)");
    return keystore_get_impl(obj, url);
}

void TokenCache::set(vlc_object_t* obj, const vlc_url_t* url, const std::string& token)
{
    if (!obj || !url || token.empty()) return;
    msg_Dbg(obj, "Keystore store: proto=https server=%s user=%s",
            url->psz_host ? url->psz_host : "(null)",
            url->psz_username ? url->psz_username : "(null)");
    keystore_set_impl(obj, url, token);
}

void TokenCache::clear(vlc_object_t* obj, const vlc_url_t* url)
{
    if (!obj || !url) return;
    const char* user = url->psz_username;
    const char* server = url->psz_host;
    vlc_keystore *p_keystore = vlc_keystore_create(obj);
    if (p_keystore == NULL)
        return;
    const char *ppsz_values[KEY_MAX] = { 0 };
    ppsz_values[KEY_PROTOCOL] = "https";
    ppsz_values[KEY_SERVER] = server;
    ppsz_values[KEY_USER] = user;
    unsigned removed = vlc_keystore_remove(p_keystore, ppsz_values);
    if (removed > 0)
        msg_Info(obj, "Removed %u token entrie(s) for %s@%s", removed,
                 user ? user : "(null)", server ? server : "(null)");
    else
        msg_Dbg(obj, "No token entries to remove for %s@%s",
                user ? user : "(null)", server ? server : "(null)");
    vlc_keystore_release(p_keystore);
}
