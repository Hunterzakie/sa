/*****************************************************************************
 * charset_detect.c: character set detection
 *****************************************************************************
 * Copyright (C) 2026 VLC authors and VideoLAN
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_common.h>
#include <vlc_charset.h>

#include "text/charset_detect.h"

char *vlc_DetectCharset( const void *data, size_t size )
{
    char *sample = strndup( data, size );
    if( sample == NULL )
        return NULL;

    bool b_utf8 = IsUTF8( sample ) != NULL;
    free( sample );

    if( b_utf8 )
        return NULL;

#ifdef __APPLE__
    return charset_detect_apple( data, size );
#else
    return NULL;
#endif
}
