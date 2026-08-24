/*****************************************************************************
 * token_cache.h: Handles persistence of authentication tokens
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

#ifndef TOKEN_CACHE_H
#define TOKEN_CACHE_H

#include <string>

struct stream_t;
struct vlc_url_t;
struct vlc_object_t;

class TokenCache
{
public:
    static std::string get(stream_t* access, const vlc_url_t* url);
    static void set(stream_t* access, const vlc_url_t* url, const std::string& token);
    static void clear(stream_t* access, const vlc_url_t* url);

    static std::string get(vlc_object_t* obj, const vlc_url_t* url);
    static void set(vlc_object_t* obj, const vlc_url_t* url, const std::string& token);
    static void clear(vlc_object_t* obj, const vlc_url_t* url);
};

#endif // TOKEN_CACHE_H
