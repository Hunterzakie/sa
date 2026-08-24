/*****************************************************************************
 * cloudstorage.cpp: cloud access module using libcloudstorage for VLC
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include <vlc_plugin.h>

#include "cloudstorage/access.h"
#include "cloudstorage/services_discovery.h"

static int vlc_sd_probe_Open( vlc_object_t * );
VLC_SD_PROBE_HELPER( "cloudstorage", N_("Cloud Storage"), SD_CAT_INTERNET )

vlc_module_begin()
    set_shortname(N_("cloudstorage"))
    set_capability("access", 3)
    add_shortcut("cloudstorage", "https")
    set_description(N_("Cloud storage services (such as Drive, Dropbox) input"
        " using libcloudstorage"))
    set_subcategory(SUBCAT_INPUT_ACCESS)
    add_string("cloudstorage-google-client-id", "", N_("Google Client ID"), N_("OAuth Client ID for Google Drive"))
    add_password("cloudstorage-google-client-secret", "", N_("Google Client Secret"), N_("OAuth Client Secret for Google Drive"))
    add_string("cloudstorage-dropbox-client-id", "", N_("Dropbox Client ID"), N_("OAuth Client ID for Dropbox"))
    add_password("cloudstorage-dropbox-client-secret", "", N_("Dropbox Client Secret"), N_("OAuth Client Secret for Dropbox"))
    add_string("cloudstorage-onedrive-client-id", "", N_("OneDrive Client ID"), N_("OAuth Client ID for OneDrive"))
    add_password("cloudstorage-onedrive-client-secret", "", N_("OneDrive Client Secret"), N_("OAuth Client Secret for OneDrive"))
    add_string("cloudstorage-box-client-id", "", N_("Box Client ID"), N_("OAuth Client ID for Box"))
    add_password("cloudstorage-box-client-secret", "", N_("Box Client Secret"), N_("OAuth Client Secret for Box"))
    add_string("cloudstorage-pcloud-client-id", "", N_("pCloud Client ID"), N_("OAuth Client ID for pCloud"))
    add_password("cloudstorage-pcloud-client-secret", "", N_("pCloud Client Secret"), N_("OAuth Client Secret for pCloud"))
    // callbacks are defined under cloudstorage/access.h
    set_callbacks(Open, Close)

    add_submodule()
        set_shortname("Cloud Storage")
        set_description(N_("Cloud Storage"))
        set_subcategory(SUBCAT_PLAYLIST_SD)
        set_capability("services_discovery", 0)
        set_callbacks(SDOpen, SDClose)
        add_shortcut("cloudstorage")

        VLC_SD_PROBE_SUBMODULE
vlc_module_end()
