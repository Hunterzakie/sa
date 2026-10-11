/*****************************************************************************
 * access.h: Main header for cloudstorage access module
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

#ifndef VLC_CLOUDSTORAGE_ACCESS_H
#define VLC_CLOUDSTORAGE_ACCESS_H

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_common.h>
#include <vlc_access.h>
#include <vlc_url.h>
#include <vlc_tls.h>

#include <string>
#include <cloudstorage/ICloudProvider.h>
#include <cloudstorage/IItem.h>

int Open( vlc_object_t * );
void Close( vlc_object_t * );

struct access_sys_t
{
    // Parsed parameters
    vlc_url_t url;
    bool memory_keystore;
    // Allocated variables
    char *alloc_path, *alloc_username;

    // Loaded on Open
    bool authenticated;
    cloudstorage::ICloudProvider::Pointer provider;
    std::string token;
    cloudstorage::ICloudProvider::Hints hints;
    cloudstorage::IItem::Pointer current_item;
    
    // google API request handling
    bool is_google_api_request;
    struct vlc_http_mgr *http_mgr;
    struct vlc_http_msg *http_resp;
    uint64_t i_size;
    uint64_t i_pos;
    bool b_seekable;
    std::string access_token;
    std::string base_url;
};

#endif
