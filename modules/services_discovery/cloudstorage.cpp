/*****************************************************************************
 * cloudstorage.cpp: cloud storage services discovery module
 *****************************************************************************
 * Copyright (C) 2017 VideoLabs and VideoLAN
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_services_discovery.h>

#include "../access/cloudstorage/services_discovery.h"

VLC_SD_PROBE_HELPER("cloudstorage", N_("Cloud Storage"), SD_CAT_INTERNET);

vlc_module_begin()
    set_shortname("Cloud Storage")
    set_description(N_("Cloud Storage"))
    set_subcategory(SUBCAT_PLAYLIST_SD)
    set_capability("services_discovery", 0)
    set_callbacks(SDOpen, SDClose)
    add_shortcut("cloudstorage")
    
    VLC_SD_PROBE_SUBMODULE
    
vlc_module_end()
