/*****************************************************************************
 * clipboard.m: macOS clipboard (NSPasteboard)
 *****************************************************************************
 * Copyright (C) 2026 the VideoLAN team
 *
 * Authors: Sergey Degtyar <sergeydegtyar@internet.ru>
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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_common.h>
#include <vlc_clipboard.h>
#include <TargetConditionals.h>

#if TARGET_OS_OSX

#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>
#import <CoreServices/CoreServices.h>

int vlc_clipboard_CopyImage(vlc_object_t *obj,
                            const block_t *p_image,
                            const char *psz_mime)
{
    if (!p_image->p_buffer || p_image->i_buffer == 0)
        return VLC_EGENERIC;

    NSData *data = [NSData dataWithBytes:p_image->p_buffer length:p_image->i_buffer];
    if (!data)
        return VLC_EGENERIC;

    NSPasteboard *pasteboard = [NSPasteboard generalPasteboard];
    [pasteboard clearContents];

    /* Map MIME types to pasteboard types / UTIs */
    NSDictionary *mimeMap = @{
        @"image/png":  NSPasteboardTypePNG,
        @"image/tiff": NSPasteboardTypeTIFF,
        @"image/jpeg": (__bridge NSString *)kUTTypeJPEG
    };

    NSString *type = mimeMap[[NSString stringWithUTF8String:psz_mime]];
    if (!type) {
        msg_Warn(obj, "Unsupported MIME type: %s", psz_mime);
        return VLC_ENOTSUP;
    }

    BOOL ok = [pasteboard setData:data forType:type];
    if (!ok) {
        msg_Err(obj, "Failed to write image to pasteboard");
        return VLC_EGENERIC;
    }

    return VLC_SUCCESS;
}

#endif