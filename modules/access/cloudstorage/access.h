/*****************************************************************************
 * access.h: Main header for cloudstorage access module
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

#ifndef VLC_CLOUDSTORAGE_ACCESS_H
#define VLC_CLOUDSTORAGE_ACCESS_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <vlc_access.h>
#include <vlc_common.h>
#include <vlc_dialog.h>
#include <vlc_threads.h>
#include <vlc_tls.h>
#include <vlc_url.h>

#include <cloudstorage/ICloudProvider.h>
#include <cloudstorage/IItem.h>
#include <string>
#include <deque>

int Open(vlc_object_t *);
void Close(vlc_object_t *);

struct access_sys_t {
  struct vlc_object_t obj;
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

  vlc_dialog_id *auth_dialog = nullptr;
  bool auth_cancelled = false;

  // Stream state
  uint64_t i_pos;

  // Async streaming state
  vlc_mutex_t lock;
  vlc_cond_t wait;
  std::deque<std::string> chunks;
  size_t current_chunk_offset;
  size_t total_queued_bytes;
  bool req_finished;
  bool req_error;
  bool aborted;
  std::unique_ptr<cloudstorage::IGenericRequest> current_req;
};

#endif
