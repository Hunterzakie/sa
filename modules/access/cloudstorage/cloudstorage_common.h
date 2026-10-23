
/*****************************************************************************
 * cloudstorage_common.h: Shared utilities for cloudstorage access/SD modules
 *****************************************************************************
 * Copyright (C) 2026 VideoLabs and VideoLAN
 *
 * Authors: Maksym Yemelianenko <max.yemelianenko@gmail.com>
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

#ifndef CLOUDSTORAGE_COMMON_H
#define CLOUDSTORAGE_COMMON_H

#include <vlc_threads.h>
#include <vlc_interrupt.h>

struct ScopedSemaphore {
    vlc_sem_t sem;
    ScopedSemaphore() { vlc_sem_init(&sem, 0); }
    ~ScopedSemaphore() { /* no-op */ }
    void post() { vlc_sem_post(&sem); }
    void wait() { vlc_sem_wait_i11e(&sem); }
};

#endif 
