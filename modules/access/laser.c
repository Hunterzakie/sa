/*****************************************************************************
 * laser.c: disc classification for USB optical drives on Android
 *****************************************************************************
 * Copyright (C) 2026 VLC authors and VideoLAN
 *
 * Authors: Pierre Bogdanovscky
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
 *****************************************************************************
 * Two submodules sharing one access_sys_t:
 *
 *  - classification, shortcut "laser-dir", location "<fd>": pf_readdir lists
 *    the MRL(s) that will play the disc, laser/<demux>://<fd> for media read
 *    as 2048-byte blocks, <access>://laser/<fd> for raw CD sectors. The fd is
 *    stale once the browse ends; the caller substitutes a fresh one.
 *
 *  - playback, shortcut "laser", location "<fd>": a byte stream over the same
 *    transport, read by the unmodified dvdnav, dvdread and bluray demuxers.
 *    CSS authentication is left to libdvdcss.
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>

#include <stdatomic.h>

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_access.h>
#include <vlc_input_item.h>

#include <laser.h>
#include <laser_disc.h>

/* Block size READ CAPACITY must report. */
#define LASER_SECTOR_SIZE 2048

/* Read-ahead window, in sectors. */
#define LASER_MAX_READ_SECTORS 128

/* Size of the refused-sector map, and how many refusals of one sector it
 * takes for reads there to return end of stream. */
#define LASER_REFUSAL_SLOTS 16
#define LASER_REFUSAL_MAX_HITS 3

typedef struct
{
    /* Per sector: a protected VOB must not end the stream for the IFOs. */
    struct
    {
        uint32_t i_lba;
        unsigned i_hits;   /* 0 = slot empty */
    } refusals[LASER_REFUSAL_SLOTS];

    int          token;
    uint64_t     i_size;
    uint64_t     i_pos;
    unsigned     i_refusal_count;  /* for the log only */
    vlc_thread_t readahead_thread;
    bool         b_readahead_started;
    atomic_bool  b_media_gone;
    bool         b_media_gone_reported; /* See StreamMediaGoneRead(). */
    vlc_mutex_t  refusal_lock;
    vlc_mutex_t  lock;
    vlc_cond_t   wait_worker;   /* worker waits here for a request      */
    vlc_cond_t   wait_done;     /* consumer waits here for a completion */
    bool         b_quit;
    bool         b_ahead_wanted;
    bool         b_ahead_busy;
    bool         b_ahead_ready;
    uint32_t     i_ahead_lba;
    int          i_ahead_blocks;
    int          i_ahead_status;
    uint8_t     *p_ahead;
    vlc_mutex_t  io_lock;
    uint32_t     i_cache_lba;
    uint32_t     i_cache_blocks; /* 0 = empty */
    uint8_t     *p_cache;
} access_sys_t;

/* ============================================================================
 * Module boilerplate
 * ============================================================================ */

static int  OpenDir   (vlc_object_t *);
static int  OpenStream(vlc_object_t *);
static void Close     (vlc_object_t *);

/* Capability 0: both are reached only when an MRL names them, laser-dir://<fd>
 * or laser/<demuxer>://<fd>. The playback MRL reaches the access as
 * laser://<fd>, so a listing needs a shortcut of its own - and OpenDir() must
 * check it, since VLC gives the module name "laser" to both as a shortcut. */
vlc_module_begin ()
set_shortname("laser")
set_description(N_("Android USB optical drive disc classification"))
set_capability("access", 0)
set_subcategory(SUBCAT_INPUT_ACCESS)
add_shortcut( "laser-dir" )
set_callbacks(OpenDir, Close)

add_submodule()
    set_description(N_("Android USB optical drive disc playback"))
    set_subcategory(SUBCAT_INPUT_ACCESS)
    set_capability("access", 0)
    add_shortcut( "laser" )
    set_callbacks(OpenStream, Close)
vlc_module_end ()

/* ============================================================================
 * pf_readdir
 * ============================================================================ */

struct laser_dir_entry
{
    char *psz_uri;
    char *psz_name;
};

/* Drops the row on failure rather than failing the whole browse. */
static void DirAddEntry(struct laser_dir_entry *entries, int *pi_count,
                        int i_capacity, const char *psz_uri_fmt, int token,
                        const char *psz_name)
{
    if (*pi_count >= i_capacity)
        return;

    struct laser_dir_entry *e = &entries[*pi_count];

    if (asprintf(&e->psz_uri, psz_uri_fmt, token) == -1)
        return;                 /* e->psz_uri is indeterminate: do not free */

    e->psz_name = strdup(psz_name);
    if (e->psz_name == NULL)
    {
        free(e->psz_uri);
        return;
    }

    (*pi_count)++;
}

static int DirRead(stream_t *access, input_item_node_t *p_node)
{
    access_sys_t *sys = access->p_sys;
    int token = sys->token;
    struct vlc_readdir_helper rdh;
    vlc_readdir_helper_init(&rdh, access, p_node);

    /* At most two rows per disc. */
    struct laser_dir_entry entries[2];
    const int i_capacity = ARRAY_SIZE(entries);
    int i_entries = 0;

    msg_Dbg(access, "classifying token=%d", token);

    laser_disc_t disc;
    laser_disc_identify(token, &disc);

    switch (disc.kind)
    {
        case LASER_DISC_CD_AUDIO:
            /* Red Book audio has no label. */
            DirAddEntry(entries, &i_entries, i_capacity,
                        "cdda://laser/%d", token, "CD Audio");
            break;

        case LASER_DISC_DVD_VIDEO:
        {
            const char *psz_title = disc.volume_id[0] != '\0'
                                  ? disc.volume_id : "DVD-Video";

            /* With menus, then without; the Kotlin side labels the second. */
            DirAddEntry(entries, &i_entries, i_capacity,
                        "laser/dvd://%d", token, psz_title);
            DirAddEntry(entries, &i_entries, i_capacity,
                        "laser/dvdsimple://%d", token, psz_title);
            break;
        }

        case LASER_DISC_DVD_AUDIO:
            DirAddEntry(entries, &i_entries, i_capacity,
                        "laser/dvda://%d", token,
                        disc.volume_id[0] != '\0' ? disc.volume_id
                                                  : "DVD-Audio");
            break;

        case LASER_DISC_DVD_UNIVERSAL:
        {
            const char *psz_title = disc.volume_id[0] != '\0'
                                  ? disc.volume_id : "DVD";

            /* Audio zone first; the Kotlin side tells the rows apart by
             * their icon. */
            DirAddEntry(entries, &i_entries, i_capacity,
                        "laser/dvda://%d", token, psz_title);
            DirAddEntry(entries, &i_entries, i_capacity,
                        "laser/dvd://%d", token, psz_title);
            break;
        }

        case LASER_DISC_BD_VIDEO:
            /* libbluray falls back to the first playlist without menus. */
            DirAddEntry(entries, &i_entries, i_capacity,
                        "laser/bluray://%d", token,
                        disc.volume_id[0] != '\0' ? disc.volume_id
                                                  : "Blu-ray Disc");
            break;

        case LASER_DISC_VCD:
            DirAddEntry(entries, &i_entries, i_capacity, "vcd://laser/%d", token,
                        disc.volume_id[0] != '\0' ? disc.volume_id
                                                  : "Video CD");
            break;

        case LASER_DISC_SVCD:
            DirAddEntry(entries, &i_entries, i_capacity, "svcd://laser/%d", token,
                        disc.volume_id[0] != '\0' ? disc.volume_id
                                                  : "Super Video CD");
            break;

        /* Data disc, empty drive or read failure: no row. */
        case LASER_DISC_UNKNOWN:
            break;
    }

    int i_ret = VLC_SUCCESS;
    if (i_entries == 0)
    {
        msg_Dbg(access, "no CD/VCD/DVD/BD recognized on token=%d "
                "(data disc, empty drive, or read failure)", token);
    }

    for (int i = 0; i < i_entries && i_ret == VLC_SUCCESS; i++)
    {
        msg_Dbg(access, "classified as '%s' (%s)",
                entries[i].psz_name, entries[i].psz_uri);
        i_ret = vlc_readdir_helper_additem(&rdh, entries[i].psz_uri, NULL,
                                           entries[i].psz_name,
                                           ITEM_TYPE_DISC, ITEM_LOCAL, NULL);
    }

    for (int i = 0; i < i_entries; i++)
    {
        free(entries[i].psz_uri);
        free(entries[i].psz_name);
    }

    vlc_readdir_helper_finish(&rdh, i_ret == VLC_SUCCESS);
    return i_ret;
}

/* ============================================================================
 * Open / Close
 * ============================================================================ */

/* Failure is not fatal: the stream size is then unknown. */
static bool laser_read_capacity(stream_t *access, int token,
                                uint64_t *pi_size)
{
    uint8_t cdb[10] = { 0 };
    cdb[0] = 0x25; /* READ CAPACITY(10) */

    uint8_t data[8] = { 0 };
    int i_actual = 0;

    laser_status_t st = laser_scsi_cdb(token, cdb, sizeof(cdb),
                                       data, sizeof(data), 1,
                                       &i_actual);
    if (st != LASER_OK || i_actual < (int)sizeof(data))
    {
        msg_Warn(access, "READ CAPACITY failed (status %d, %d "
                 "bytes) - continuing with an unknown medium size",
                 (int)st, i_actual);
        return false;
    }

    uint32_t i_last_lba   = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16)
                          | ((uint32_t)data[2] <<  8) |  (uint32_t)data[3];
    uint32_t i_block_size = ((uint32_t)data[4] << 24) | ((uint32_t)data[5] << 16)
                          | ((uint32_t)data[6] <<  8) |  (uint32_t)data[7];

    /* READ CAPACITY(10) overflow sentinel: implausible on optical media. */
    if (i_last_lba == 0xFFFFFFFFu)
    {
        msg_Warn(access, "READ CAPACITY reports the 32-bit "
                 "overflow sentinel - ignoring it");
        return false;
    }

    /* The sector arithmetic below assumes 2048-byte blocks. */
    if (i_block_size != LASER_SECTOR_SIZE)
    {
        msg_Warn(access, "drive reports a %u-byte block size, "
                 "expected %u - ignoring the reported capacity",
                 i_block_size, (unsigned)LASER_SECTOR_SIZE);
        return false;
    }

    *pi_size = ((uint64_t)i_last_lba + 1) * (uint64_t)LASER_SECTOR_SIZE;
    return true;
}

/* Reached through laser-dir://<fd>, and through laser://<fd>, which is
 * playback's: the location is the fd, the access name says which. */
static int OpenDir(vlc_object_t *obj)
{
    stream_t *access = (stream_t *)obj;

    if (access->psz_name == NULL || strcmp(access->psz_name, "laser-dir") != 0)
        return VLC_EGENERIC;

    int fd;
    if (!laser_parse_token(access->psz_location, &fd))
        return VLC_EGENERIC;

    msg_Dbg(access, "OpenDir() accepted '%s://%s' (fd=%d)",
            access->psz_name, access->psz_location, fd);

    access_sys_t *sys = vlc_obj_malloc(obj, sizeof(*sys));
    if (unlikely(sys == NULL))
        return VLC_ENOMEM;

    memset(sys, 0, sizeof(*sys));
    sys->token = fd;
    access->p_sys = sys;

    /* The first claim also sets the drive up and waits for it to spin up. */
    if (laser_acquire(fd) != LASER_OK)
    {
        msg_Warn(access, "cannot acquire token=%d", fd);
        return VLC_EGENERIC;
    }

    access->pf_readdir = DirRead;
    access->pf_control = access_vaDirectoryControlHelper;

    return VLC_SUCCESS;
}

/* ============================================================================
 * Playback: byte-stream access over the same SCSI transport
 * ============================================================================ */

/* Checked before issuing, so a latched sector costs no SCSI command at all. */
static bool StreamRefusalLatched(access_sys_t *sys, uint32_t i_lba)
{
    bool b_latched = false;

    vlc_mutex_lock(&sys->refusal_lock);
    for (unsigned i = 0; i < LASER_REFUSAL_SLOTS; i++)
        if (sys->refusals[i].i_hits >= LASER_REFUSAL_MAX_HITS &&
            sys->refusals[i].i_lba == i_lba)
        {
            b_latched = true;
            break;
        }
    vlc_mutex_unlock(&sys->refusal_lock);

    return b_latched;
}

/* Logs once per distinct refused sector. */
static void StreamRefused(stream_t *access, uint32_t i_lba,
                          laser_status_t reason)
{
    access_sys_t *sys = access->p_sys;

    /* Messages are composed outside the lock. */
    vlc_mutex_lock(&sys->refusal_lock);

    for (unsigned i = 0; i < LASER_REFUSAL_SLOTS; i++)
    {
        if (sys->refusals[i].i_hits > 0 && sys->refusals[i].i_lba == i_lba)
        {
            /* Saturating: a wrap would unlatch. */
            if (sys->refusals[i].i_hits < LASER_REFUSAL_MAX_HITS)
                sys->refusals[i].i_hits++;
            vlc_mutex_unlock(&sys->refusal_lock);
            return;
        }
    }

    /* Evict the least-hit slot. */
    unsigned i_slot = 0;
    for (unsigned i = 1; i < LASER_REFUSAL_SLOTS; i++)
    {
        if (sys->refusals[i].i_hits < sys->refusals[i_slot].i_hits)
            i_slot = i;
        if (sys->refusals[i_slot].i_hits == 0)
            break;      /* an empty slot is as good as it gets */
    }

    sys->refusals[i_slot].i_lba  = i_lba;
    sys->refusals[i_slot].i_hits = 1;
    sys->i_refusal_count++;

    const unsigned i_count = sys->i_refusal_count;

    vlc_mutex_unlock(&sys->refusal_lock);

    switch (reason)
    {
    case LASER_ERR_REGION:
        msg_Err(access, "this disc's region does not match the "
                "drive's. This is NOT an authentication problem and no key "
                "exchange will fix it.");
        break;

    case LASER_ERR_SCRAMBLED:
        msg_Warn(access, "the drive refuses to read LBA %u - "
                 "CSS-scrambled content on a drive that has not been "
                 "authenticated. Reads elsewhere on the disc are unaffected. "
                 "(distinct refused sectors so far: %u)",
                 i_lba, i_count);
        break;

    default:
        msg_Warn(access, "the drive refuses to read LBA %u and "
                 "will not explain why (status %d). Reads elsewhere on the "
                 "disc are unaffected. (distinct refused sectors so far: %u)",
                 i_lba, (int)reason, i_count);
        break;
    }
}

/* A changed medium is treated like an ejection. */
static void StreamForgetMedium(stream_t *access)
{
    access_sys_t *sys = access->p_sys;

    msg_Warn(access, "the medium may have changed - dropping the "
             "refused-sector map and giving up on this medium");

    /* The cache fields belong to the consumer thread: not touched here. */

    vlc_mutex_lock(&sys->refusal_lock);
    memset(sys->refusals, 0, sizeof(sys->refusals));
    sys->i_refusal_count  = 0;
    vlc_mutex_unlock(&sys->refusal_lock);

    /* Latched, or the caller would keep reading. */
    atomic_store_explicit(&sys->b_media_gone, true, memory_order_relaxed);
}

/* -1 first, so the application sees an error rather than a title that ended,
 * then 0 for ever, so a caller that retries -1 does not spin on it. */
static ssize_t StreamMediaGoneRead(stream_t *access)
{
    access_sys_t *sys = access->p_sys;

    if (sys->b_media_gone_reported)
        return 0;

    sys->b_media_gone_reported = true;
    msg_Err(access, "the disc is no longer in the drive - "
            "stopping playback");
    return -1;
}

/* Called when the drive reports no CSS session: those sectors may become
 * readable once libdvdcss re-authenticates. */
static void StreamForgetRefusals(stream_t *access)
{
    access_sys_t *sys = access->p_sys;

    vlc_mutex_lock(&sys->refusal_lock);
    const unsigned i_count = sys->i_refusal_count;
    if (i_count > 0)
    {
        memset(sys->refusals, 0, sizeof(sys->refusals));
        sys->i_refusal_count  = 0;
    }
    vlc_mutex_unlock(&sys->refusal_lock);

    if (i_count == 0)
        return;

    msg_Warn(access, "the drive reports no CSS session in force - "
             "forgetting %u refused sector(s), which may become readable "
             "after re-authentication", i_count);
}

/* Stops a read short of a known-refused sector. */
static int StreamClampBeforeRefused(access_sys_t *sys, uint32_t i_lba,
                                    int i_want)
{
    vlc_mutex_lock(&sys->refusal_lock);

    for (unsigned i = 0; i < LASER_REFUSAL_SLOTS; i++)
    {
        if (sys->refusals[i].i_hits == 0)
            continue;

        uint32_t i_bad = sys->refusals[i].i_lba;
        if (i_bad < i_lba)
            continue;
        if (i_bad == i_lba)
        {
            /* Below the threshold, let the read fail again so that the
             * counter advances. */
            int i_ret = sys->refusals[i].i_hits >= LASER_REFUSAL_MAX_HITS
                      ? 0 : 1;
            vlc_mutex_unlock(&sys->refusal_lock);
            return i_ret;
        }
        if (i_bad - i_lba < (uint32_t)i_want)
            i_want = (int)(i_bad - i_lba);
    }

    vlc_mutex_unlock(&sys->refusal_lock);

    return i_want;
}

/* Medium gone or drive unplugged: latched by StreamForgetMedium(). */
static bool StreamIsTerminal(int i_status)
{
    return i_status == (int)LASER_ERR_MEDIA_GONE ||
           i_status == (int)LASER_ERR_NO_DEVICE;
}

/* On a refusal, halves the request so that the readable prefix is kept: a
 * refusal fails the whole command. Only a single-sector refusal is recorded.
 *
 * Returns the sector count, 0 at end of medium, or a negative laser status. */
static int StreamReadSectors(stream_t *access, uint32_t i_lba, int i_want,
                             uint8_t *p_dst)
{
    access_sys_t *sys = access->p_sys;

    i_want = StreamClampBeforeRefused(sys, i_lba, i_want);
    if (i_want <= 0)
        return 0;   /* the very next sector is known-refused: end of stream */

    while (i_want > 1)
    {
        int i_read = laser_read_blocks(sys->token, i_lba, i_want, p_dst);

        if (StreamIsTerminal(i_read))
        {
            StreamForgetMedium(access);
            return i_read;
        }
        if (i_read == (int)LASER_ERR_NO_KEY)
        {
            /* The CSS session went away: no sector is to blame. */
            StreamForgetRefusals(access);
            return i_read;
        }
        if (!laser_status_is_positional(i_read))
            return i_read;
        i_want /= 2;
    }

    int i_read = laser_read_blocks(sys->token, i_lba, 1, p_dst);
    if (StreamIsTerminal(i_read))
    {
        StreamForgetMedium(access);
        return i_read;
    }
    if (i_read == (int)LASER_ERR_NO_KEY)
    {
        StreamForgetRefusals(access);
        return i_read;
    }
    if (laser_status_is_positional(i_read))
    {
        StreamRefused(access, i_lba, (laser_status_t)i_read);
        return i_read;
    }

    return i_read;
}

/* Clamped to the medium: reading past it costs a full retry budget. */
static int StreamWindowSectors(access_sys_t *sys, uint32_t i_lba)
{
    int i_want = LASER_MAX_READ_SECTORS;

    if (sys->i_size != 0)
    {
        uint64_t i_total = sys->i_size / LASER_SECTOR_SIZE;
        if ((uint64_t)i_lba >= i_total)
            return 0;
        uint64_t i_left = i_total - i_lba;
        if ((uint64_t)i_want > i_left)
            i_want = (int)i_left;
    }

    return i_want;
}

/* The only access to the transport, shared by both threads. */
static int StreamReadLocked(stream_t *access, uint32_t i_lba, int i_want,
                            uint8_t *p_dst)
{
    access_sys_t *sys = access->p_sys;

    vlc_mutex_lock(&sys->io_lock);
    int i_read = StreamReadSectors(access, i_lba, i_want, p_dst);
    vlc_mutex_unlock(&sys->io_lock);

    return i_read;
}

/* Caller holds sys->lock and has established that no fetch is in flight. */
static void StreamRequestAhead(access_sys_t *sys, uint32_t i_lba)
{
    if (StreamWindowSectors(sys, i_lba) <= 0)
        return;                 /* past the end - nothing to fetch */

    sys->i_ahead_lba    = i_lba;
    sys->b_ahead_wanted = true;
    sys->b_ahead_ready  = false;
    vlc_cond_signal(&sys->wait_worker);
}

static void *StreamReadaheadThread(void *p_data)
{
    stream_t     *access = p_data;
    access_sys_t *sys    = access->p_sys;

    vlc_mutex_lock(&sys->lock);
    for (;;)
    {
        while (!sys->b_quit && !sys->b_ahead_wanted)
            vlc_cond_wait(&sys->wait_worker, &sys->lock);
        if (sys->b_quit)
            break;

        uint32_t i_lba = sys->i_ahead_lba;
        sys->b_ahead_wanted = false;
        sys->b_ahead_busy   = true;

        /* Not held across the transfer. */
        vlc_mutex_unlock(&sys->lock);

        int i_want = atomic_load_explicit(&sys->b_media_gone,
                                          memory_order_relaxed)
                   ? 0 : StreamWindowSectors(sys, i_lba);
        int i_read = i_want > 0
                   ? StreamReadLocked(access, i_lba, i_want, sys->p_ahead)
                   : 0;

        vlc_mutex_lock(&sys->lock);
        sys->i_ahead_blocks = i_read > 0 ? i_read : 0;
        sys->i_ahead_status = i_read;
        sys->b_ahead_busy   = false;
        sys->b_ahead_ready  = true;
        vlc_cond_signal(&sys->wait_done);
    }
    vlc_mutex_unlock(&sys->lock);

    return NULL;
}

/* Returns the sector count, 0 at end of medium, or -1, logging why. */
static int StreamClassifyRead(stream_t *access, int i_read,
                              uint32_t i_lba, int i_want,
                              const char *psz_what)
{
    if (laser_status_is_positional(i_read) || i_read == (int)LASER_ERR_NO_KEY)
    {
        msg_Dbg(access, "%s read of %d sectors at LBA %u refused (%d)",
                psz_what, i_want, i_lba, i_read);
        return -1;
    }

    if (i_read < 0)
    {
        msg_Warn(access, "%s read of %d sectors at LBA %u failed (%d)",
                 psz_what, i_want, i_lba, i_read);
        return -1;
    }

    return i_read; /* 0: end of medium */
}

/* Leaves a fetch of the following window in flight. Returns the sector count
 * now valid in the cache, 0 at end of medium, or -1. */
static int StreamRefillCache(stream_t *access, uint32_t i_lba)
{
    access_sys_t *sys = access->p_sys;
    int i_read;

    sys->i_cache_blocks = 0;    /* invalid until refilled */

    vlc_mutex_lock(&sys->lock);

    /* A transfer in flight cannot be recalled, and writes into p_ahead. */
    while (sys->b_ahead_busy)
        vlc_cond_wait(&sys->wait_done, &sys->lock);

    if (sys->b_ahead_ready && sys->i_ahead_lba == i_lba)
    {
        assert(!sys->b_ahead_busy);

        uint8_t *p_tmp = sys->p_cache;
        sys->p_cache   = sys->p_ahead;
        sys->p_ahead   = p_tmp;

        i_read              = sys->i_ahead_status;
        sys->i_cache_lba    = i_lba;
        sys->i_cache_blocks = sys->i_ahead_blocks;
        sys->b_ahead_ready  = false;

        if (i_read > 0)
            StreamRequestAhead(sys, i_lba + (uint32_t)i_read);
        vlc_mutex_unlock(&sys->lock);

        if (i_read < 0)
            return -1;
        return i_read;
    }

    /* Miss: first read, or a seek away from the prefetched run. */
    sys->b_ahead_ready = false;
    vlc_mutex_unlock(&sys->lock);

    int i_want = StreamWindowSectors(sys, i_lba);
    if (i_want <= 0)
        return 0;

    i_read = StreamClassifyRead(access,
                                StreamReadLocked(access, i_lba, i_want,
                                                 sys->p_cache),
                                i_lba, i_want, "window");
    if (i_read <= 0)
        return i_read;

    sys->i_cache_lba    = i_lba;
    sys->i_cache_blocks = i_read;

    /* Start prefetching the next window. */
    vlc_mutex_lock(&sys->lock);
    StreamRequestAhead(sys, i_lba + (uint32_t)i_read);
    vlc_mutex_unlock(&sys->lock);

    return i_read;
}

static ssize_t StreamRead(stream_t *access, void *p_buffer, size_t i_len)
{
    access_sys_t *sys = access->p_sys;

    if (i_len == 0)
        return 0;

    if (atomic_load_explicit(&sys->b_media_gone, memory_order_relaxed))
        return StreamMediaGoneRead(access);

    /* End of medium is a clean EOF, not an error. */
    if (sys->i_size != 0 && sys->i_pos >= sys->i_size)
        return 0;

    if (sys->i_size != 0 && sys->i_pos + i_len > sys->i_size)
        i_len = (size_t)(sys->i_size - sys->i_pos);

    uint32_t i_lba    = (uint32_t)(sys->i_pos / LASER_SECTOR_SIZE);
    size_t   i_offset = (size_t)(sys->i_pos % LASER_SECTOR_SIZE);

    /* A repeatedly refused sector reads as end of stream, at that position
     * only. Scratches (LASER_ERR_IO) are never recorded. */
    if (StreamRefusalLatched(sys, i_lba))
        return 0;

    /* Large aligned reads go straight to the caller's buffer. */
    if (i_offset == 0 &&
        i_len >= (size_t)LASER_MAX_READ_SECTORS * LASER_SECTOR_SIZE)
    {
        int i_blocks = (int)(i_len / LASER_SECTOR_SIZE);

        int i_read = StreamClassifyRead(access,
                                        StreamReadLocked(access, i_lba,
                                                         i_blocks, p_buffer),
                                        i_lba, i_blocks, "direct");
        if (i_read <= 0)
            return i_read;

        sys->i_pos += (uint64_t)i_read * LASER_SECTOR_SIZE;
        return (ssize_t)i_read * LASER_SECTOR_SIZE;
    }

    if (sys->i_cache_blocks == 0 ||
        i_lba < sys->i_cache_lba ||
        i_lba >= sys->i_cache_lba + sys->i_cache_blocks)
    {
        int i_read = StreamRefillCache(access, i_lba);
        if (i_read < 0)
            return -1;
        if (i_read == 0)
            return 0;
    }

    const uint32_t i_cached_lba    = sys->i_cache_lba;
    const uint32_t i_cached_blocks = sys->i_cache_blocks;

    /* A short read is allowed by stream_t. */
    size_t i_cache_offset = (size_t)(i_lba - i_cached_lba)
                          * LASER_SECTOR_SIZE + i_offset;
    size_t i_cache_valid  = (size_t)i_cached_blocks * LASER_SECTOR_SIZE;

    /* Would otherwise wrap and overrun the cache. */
    if (i_cache_offset >= i_cache_valid)
        return 0;

    size_t i_avail = i_cache_valid - i_cache_offset;

    if (i_len > i_avail)
        i_len = i_avail;

    memcpy(p_buffer, sys->p_cache + i_cache_offset, i_len);
    sys->i_pos += i_len;
    return (ssize_t)i_len;
}

/* The cache survives seeks: navigation often stays within one window. */
static int StreamSeek(stream_t *access, uint64_t i_pos)
{
    access_sys_t *sys = access->p_sys;
    sys->i_pos = i_pos;
    return VLC_SUCCESS;
}

static int StreamControl(stream_t *access, int i_query, va_list args)
{
    access_sys_t *sys = access->p_sys;

    switch (i_query)
    {
        case STREAM_CAN_SEEK:
        case STREAM_CAN_FASTSEEK:
        case STREAM_CAN_PAUSE:
        case STREAM_CAN_CONTROL_PACE:
            *va_arg(args, bool *) = true;
            break;

        case STREAM_GET_SIZE:
            if (sys->i_size == 0)
                return VLC_EGENERIC; /* unknown - READ CAPACITY declined */
            *va_arg(args, uint64_t *) = sys->i_size;
            break;

        case STREAM_GET_PTS_DELAY:
            *va_arg(args, vlc_tick_t *) =
                    VLC_TICK_FROM_MS(var_InheritInteger(access, "disc-caching"));
            break;

        case STREAM_SET_PAUSE_STATE:
            /* Nothing to suspend between reads. */
            break;

        default:
            return VLC_EGENERIC;
    }

    return VLC_SUCCESS;
}

/* Only reached through laser/<demux>://<fd>: the location is the fd. */
static int OpenStream(vlc_object_t *obj)
{
    stream_t *access = (stream_t *)obj;

    int fd;
    if (!laser_parse_token(access->psz_location, &fd))
        return VLC_EGENERIC;

    msg_Dbg(access, "OpenStream() accepted '%s://%s' (fd=%d)",
            access->psz_name, access->psz_location, fd);

    access_sys_t *sys = vlc_obj_malloc(obj, sizeof(*sys));
    if (unlikely(sys == NULL))
        return VLC_ENOMEM;

    memset(sys, 0, sizeof(*sys));

    sys->token         = fd;

    const size_t i_window = (size_t)LASER_MAX_READ_SECTORS
                          * LASER_SECTOR_SIZE;
    sys->p_cache = vlc_obj_malloc(obj, i_window);
    sys->p_ahead = vlc_obj_malloc(obj, i_window);
    if (unlikely(sys->p_cache == NULL || sys->p_ahead == NULL))
        return VLC_ENOMEM;

    vlc_mutex_init(&sys->lock);
    vlc_mutex_init(&sys->io_lock);
    vlc_mutex_init(&sys->refusal_lock);
    vlc_cond_init(&sys->wait_worker);
    vlc_cond_init(&sys->wait_done);

    access->p_sys = sys;

    /* Held for the stream's lifetime; libdvdcss may hold the same token. */
    if (laser_acquire(sys->token) != LASER_OK)
    {
        msg_Err(access, "cannot acquire token=%d", sys->token);
        return VLC_EGENERIC;
    }

    if (!laser_read_capacity(access, fd, &sys->i_size))
        sys->i_size = 0;
    else
        msg_Dbg(access, "medium size %" PRIu64 " bytes (%" PRIu64
                " sectors)", sys->i_size,
                sys->i_size / LASER_SECTOR_SIZE);

    if (vlc_clone(&sys->readahead_thread, StreamReadaheadThread, access) == 0)
        sys->b_readahead_started = true;
    else
        msg_Warn(access, "no read-ahead thread; reads will be "
                 "synchronous and playback of high-bitrate discs may stall");

    access->pf_read    = StreamRead;
    access->pf_block   = NULL;
    access->pf_seek    = StreamSeek;
    access->pf_readdir = NULL;
    access->pf_control = StreamControl;

    return VLC_SUCCESS;
}

static void Close(vlc_object_t *obj)
{
    /* Shared by both submodules. The fd belongs to the caller. */
    stream_t *access = (stream_t *)obj;
    access_sys_t *sys = access->p_sys;

    /* Stop the worker before releasing the token it reads from. */
    if (sys->b_readahead_started)
    {
        vlc_mutex_lock(&sys->lock);
        sys->b_quit = true;
        vlc_cond_signal(&sys->wait_worker);
        vlc_mutex_unlock(&sys->lock);
        vlc_join(sys->readahead_thread, NULL);
        sys->b_readahead_started = false;
    }

    /* Torn down only once no other consumer holds a claim. */
    laser_release(sys->token);
}
