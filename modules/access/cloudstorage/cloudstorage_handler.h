/*****************************************************************************
 * cloudstorage_handler.h: Handles generic libcloudstorage interactions
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
#ifndef CLOUDSTORAGE_HANDLER_H
#define CLOUDSTORAGE_HANDLER_H

#include "access.h"
#include <vlc_input.h>

int HandleCloudProviderRequest(stream_t* p_access);

int ReadDir(stream_t* p_access, input_item_node_t* p_node);

#endif // CLOUDSTORAGE_HANDLER_H
