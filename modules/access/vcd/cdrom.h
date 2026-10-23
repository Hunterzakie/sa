/****************************************************************************
 * cdrom.h: cdrom tools header
 *****************************************************************************
 * Copyright (C) 1998-2001 VLC authors and VideoLAN
 *
 * Authors: Johan Bilien <jobi@via.ecp.fr>
 *          Gildas Bazin <gbazin@netcourrier.com>
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

#ifndef VLC_CDROM_H
#define VLC_CDROM_H

#include <assert.h>

enum {
    CDDA_TYPE = 0,
    VCD_TYPE  = 1,
};

/* size of a CD sector / Red Book IEC-60908 */
#define CD_RAW_PHYSICAL_BLOCK_SIZE   2448
#define CD_RAW_SUBCHANNELS_DATA_SIZE 96
#define CD_RAW_SECTOR_SIZE (CD_RAW_PHYSICAL_BLOCK_SIZE-CD_RAW_SUBCHANNELS_DATA_SIZE) // 2352

#define CD_RAW_FRAMES_PER_SECOND 75
#define CD_RAW_PREGAP_SECONDS    2

/* Subchannels control flag */
#define CD_CHANNEL_Q_CTRL_MASK_PRE_EMPHASIS   0x01
#define CD_CHANNEL_Q_CTRL_MASK_COPY_PERMITTED 0x02
#define CD_CHANNEL_Q_CTRL_MASK_DATA           0x04
#define CD_CHANNEL_Q_CTRL_MASK_4CH            0x08

/* CDROM ISO/IEC 10149 Yellow Book */
#define CD_ROM_SYNC_PATTERN_SIZE 12
#define CD_ROM_MODE_HEADER_SIZE  4
#define CD_ROM_SECTOR_HEADER_SIZE (CD_ROM_SYNC_PATTERN_SIZE+CD_ROM_MODE_HEADER_SIZE)
#define CD_ROM_MODE1_DATA_SIZE 2048
#define CD_ROM_MODE2_DATA_SIZE 2336

/* CDROM-XA ISO/IEC 10149 Yellow Book Annex */
#define CD_ROM_XA_SECTOR_SUBHEADER_SIZE 8
#define CD_ROM_XA_SECTOR_HEADER_SIZE (CD_ROM_SECTOR_HEADER_SIZE+CD_ROM_XA_SECTOR_SUBHEADER_SIZE)
#define CD_ROM_XA_MODE2_FORM1_DATA_SIZE 2048
#define CD_ROM_XA_MODE2_FORM2_DATA_SIZE 2324

/* Orange Book / ECMA-394 ECMA-395 */
#define CD_MULTISESSION_LEADOUT_SECS 60   // Secondary session Lead-Out
#define CD_MULTISESSION_LEADIN_SECS  90   // Secondary session Lead-In


/*****************************************************************************
 * Misc. Macros
 *****************************************************************************/
static inline int MSF_TO_LBA(uint8_t min, uint8_t sec, uint8_t frame)
{
    return (int)(frame + CD_RAW_FRAMES_PER_SECOND * (sec + 60 * min));
}
static inline int MSF_TO_LBA2(uint8_t min, uint8_t sec, uint8_t frame)
{
    return (int)(frame + CD_RAW_FRAMES_PER_SECOND * (sec -CD_RAW_PREGAP_SECONDS + 60 * min));
}

/* Converts BCD to Binary data */
#define BCD_TO_BIN(i) \
    (uint8_t)((uint8_t)(0xf & (uint8_t)i)+((uint8_t)10*((uint8_t)i >> 4)))

typedef struct vlc_cd_s vlc_cd_t;
typedef struct
{
    int i_lba;
    int i_control;
} vlc_cd_sector_t;

typedef struct
{
    int i_tracks;
    vlc_cd_sector_t *p_sectors;
    int i_first_track;
    int i_last_track;
} vlc_cd_toc_t;

static inline vlc_cd_toc_t * vlc_cd_toc_New( void )
{
    return calloc(1, sizeof(vlc_cd_toc_t));
}

static inline void vlc_cd_toc_Reset( vlc_cd_toc_t *toc )
{
    free(toc->p_sectors);
    memset(toc, 0, sizeof(*toc));
}

static inline void vlc_cd_toc_Free( vlc_cd_toc_t *toc )
{
    free(toc->p_sectors);
    free(toc);
}

/*****************************************************************************
 * Prototypes
 *****************************************************************************/
vlc_cd_t *vlc_cd_Open         ( vlc_object_t *, const char * );
void      vlc_cd_Close        ( vlc_cd_t * );
vlc_cd_toc_t * vlc_cd_GetTOC  ( vlc_cd_t * );

/* Returns:
 *    0  success
 *   -1  recoverable failure, or at least not known not to be (ex: a scratch)
 *   -2  irrecoverable failure (ex: disc ejected, device unplugged) */
int       vlc_cd_ReadSectors  ( vlc_cd_t *, int, uint8_t *, int, int );

/* CDDA only
 * The track 0 is for album meta data */
int       vlc_cd_GetCdText( vlc_cd_t *, vlc_meta_t ***ppp_tracks, int *pi_tracks );

#endif /* VLC_CDROM_H */
