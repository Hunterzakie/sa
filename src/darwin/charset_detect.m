/*****************************************************************************
 * charset_detect.m: character set detection using Foundation
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

#include <vlc_common.h>

#include "../text/charset_detect.h"

#include <Foundation/Foundation.h>

char *charset_detect_apple( const void *data, size_t size )
{
    @autoreleasepool {
        NSData *nsdata = [NSData dataWithBytesNoCopy:(void *)data
                                             length:size
                                       freeWhenDone:NO];

        NSStringEncoding nsenc =
            [NSString stringEncodingForData:nsdata
                            encodingOptions:nil
                            convertedString:NULL
                        usedLossyConversion:NULL];
        if( nsenc == 0 )
            return NULL;

        CFStringEncoding cfenc =
            CFStringConvertNSStringEncodingToEncoding( nsenc );
        if( cfenc == kCFStringEncodingInvalidId )
            return NULL;

        NSString *iana =
            (__bridge NSString *)CFStringConvertEncodingToIANACharSetName( cfenc );
        if( iana == nil )
            return NULL;

        return strdup( iana.UTF8String );
    }
}
