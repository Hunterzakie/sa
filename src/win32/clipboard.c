/*****************************************************************************
 * clipboard.c: win32 clipboard API implementation
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
#include <string.h>
#include <windows.h>

/* Copy image data to clipboard. On Windows, CF_DIB expects a DIB (device-
 * independent bitmap); raw PNG/JPEG is not a standard format. So we register
 * a private format for PNG data so that some apps can paste. For now we
 * support only image/png via the "PNG" clipboard format. */
int vlc_clipboard_CopyImage(vlc_object_t *obj,
                            const block_t *p_image,
                            const char *psz_mime)
{
    if (p_image->i_buffer == 0 || p_image->p_buffer == NULL)
        return VLC_EGENERIC;

    UINT cf;
    if (strcmp(psz_mime, "image/png") == 0)
        cf = RegisterClipboardFormatW(L"PNG");
    else {
        msg_Dbg(obj, "Clipboard image format not supported: %s", psz_mime);
        return VLC_ENOTSUP;
    }

    if (cf == 0) {
        msg_Err(obj, "RegisterClipboardFormat(PNG) failed");
        return VLC_EGENERIC;
    }

    if (!OpenClipboard(NULL)) {
        msg_Err(obj, "OpenClipboard failed");
        return VLC_EGENERIC;
    }

    EmptyClipboard();

    size_t i_buf = p_image->i_buffer;
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, i_buf);
    if (hMem == NULL) {
        CloseClipboard();
        return VLC_EGENERIC;
    }
    void *pMem = GlobalLock(hMem);
    if (pMem == NULL) {
        GlobalFree(hMem);
        CloseClipboard();
        return VLC_EGENERIC;
    }
    memcpy(pMem, p_image->p_buffer, i_buf);
    GlobalUnlock(hMem);

    if (SetClipboardData(cf, hMem) == NULL) {
        GlobalFree(hMem);
        CloseClipboard();
        msg_Err(obj, "SetClipboardData failed");
        return VLC_EGENERIC;
    }
    CloseClipboard();
    return VLC_SUCCESS;
}
