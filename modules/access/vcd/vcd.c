/*****************************************************************************
 * vcd.c : VCD input module for vlc
 *****************************************************************************
 * Copyright © 2000-2011 VLC authors and VideoLAN
 *
 * Author: Johan Bilien <jobi@via.ecp.fr>
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

/*****************************************************************************
 * Preamble
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_input.h>
#include <vlc_access.h>
#include <vlc_charset.h>

#include "cdrom.h"

/*****************************************************************************
 * Module descriptor
 *****************************************************************************/
static int  Open ( vlc_object_t * );
static void Close( vlc_object_t * );

#define HELP_TEXT N_("Usage hint: [vcd:][device][#[title][,[chapter]]]")

vlc_module_begin ()
    set_shortname( N_("VCD"))
    set_description( N_("VCD input") )
    set_help( HELP_TEXT )
    set_capability( "access", 0 )
    set_callbacks( Open, Close )
    set_subcategory( SUBCAT_INPUT_ACCESS )

    add_shortcut( "vcd", "svcd" )
vlc_module_end ()

/*****************************************************************************
 * Local prototypes
 *****************************************************************************/

/* how many blocks VCDRead will read in each loop */
#define VCD_BLOCKS_ONCE 20
#define VCD_DATA_ONCE   (VCD_BLOCKS_ONCE * VCD_DATA_SIZE)

#define VCD_GAP_MAX_SECTORS 300

typedef struct
{
    vcddev_t    *vcddev;                            /* vcd device descriptor */
    uint64_t    offset;

    /* Title infos */
    vcddev_toc_t *p_toc;

    struct
    {
        uint64_t *seekpoints;
        size_t    count;
        int       i_start;
        int       i_end;
    } titles[99];                        /* No more that 99 track in a vcd ? */
    int         i_current_title;
    unsigned    i_current_seekpoint;
    int         i_sector;                                  /* Current Sector */
} access_sys_t;

static block_t *Block( stream_t *, bool * restrict);
static int      Seek( stream_t *, uint64_t );
static int      Control( stream_t *, int, va_list );
static int      EntryPoints( stream_t * );

/*****************************************************************************
 * FindPackSector: Looks for a sector carrying a pack in [i_first, i_last)
 *****************************************************************************
 * The scan runs from i_first upwards, unless b_backward is set, in which case
 * it runs from i_last downwards. Sectors are read VCD_BLOCKS_ONCE at a time.
 *****************************************************************************/
static int FindPackSector( stream_t *p_access, int i_first, int i_last,
                           bool b_backward )
{
    access_sys_t *p_sys = p_access->p_sys;
    uint8_t *p_buffer = vlc_alloc( VCD_BLOCKS_ONCE, VCD_DATA_SIZE );
    int i_found = -1;

    if( unlikely(p_buffer == NULL) )
        return -1;

    for( int i_done = 0; i_done < i_last - i_first; )
    {
        int i_read = i_last - i_first - i_done;
        if( i_read > VCD_BLOCKS_ONCE )
            i_read = VCD_BLOCKS_ONCE;

        const int i_sector = b_backward ? i_last - i_done - i_read
                                        : i_first + i_done;

        if( ioctl_ReadSectors( VLC_OBJECT(p_access), p_sys->vcddev,
                               i_sector, p_buffer, i_read, VCD_TYPE ) < 0 )
            break;

        for( int i = 0; i < i_read; i++ )
        {
            const int i_index = b_backward ? i_read - 1 - i : i;
            const uint8_t *p_sector = &p_buffer[i_index * VCD_DATA_SIZE];

            if( p_sector[0] == 0x00 && p_sector[1] == 0x00 &&
                p_sector[2] == 0x01 && p_sector[3] == 0xBA )
            {
                i_found = i_sector + i_index;
                break;
            }
        }

        if( i_found >= 0 )
            break;

        i_done += i_read;
    }

    free( p_buffer );

    return i_found;
}

/*****************************************************************************
 * TitleStart: Logical start of a title: the first sector carrying a pack.
 *****************************************************************************/
static int TitleStart( stream_t *p_access, int i_title )
{
    access_sys_t *p_sys = p_access->p_sys;
    const vcddev_toc_t *p_toc = p_sys->p_toc;

    if( p_sys->titles[i_title].i_start >= 0 )
        return p_sys->titles[i_title].i_start;

    const int i_track = p_toc->p_sectors[i_title + 1].i_lba;

    int i_limit = i_track + VCD_GAP_MAX_SECTORS;
    if( i_limit > p_toc->p_sectors[i_title + 2].i_lba )
        i_limit = p_toc->p_sectors[i_title + 2].i_lba;

    int i_first = FindPackSector( p_access, i_track, i_limit, false );
    if( i_first < 0 )
        i_first = i_track;

    if( i_first != i_track )
        msg_Dbg( p_access, "skipping %d pregap sector(s), first pack at %d",
                 i_first - i_track, i_first );

    p_sys->titles[i_title].i_start = i_first;

    /* EntryPoints() counted the seekpoints from the track address */
    const uint64_t i_pregap = (uint64_t)(i_first - i_track) * VCD_DATA_SIZE;
    for( size_t i = 0; i < p_sys->titles[i_title].count; i++ )
    {
        uint64_t *p_seekpoint = &p_sys->titles[i_title].seekpoints[i];

        *p_seekpoint = ( *p_seekpoint > i_pregap )
                       ? *p_seekpoint - i_pregap : 0;
    }

    return i_first;
}

/*****************************************************************************
 * TitleEnd: one sector past the last one carrying a pack.
 *****************************************************************************/
static int TitleEnd( stream_t *p_access, int i_title )
{
    access_sys_t *p_sys = p_access->p_sys;
    const vcddev_toc_t *p_toc = p_sys->p_toc;

    if( p_sys->titles[i_title].i_end >= 0 )
        return p_sys->titles[i_title].i_end;

    const int i_next = p_toc->p_sectors[i_title + 2].i_lba;
    const int i_start = TitleStart( p_access, i_title );

    int i_limit = i_next - VCD_GAP_MAX_SECTORS;
    if( i_limit < i_start )
        i_limit = i_start;

    int i_last = FindPackSector( p_access, i_limit, i_next, true );
    i_last = ( i_last < 0 ) ? i_next : i_last + 1;

    if( i_last != i_next )
        msg_Dbg( p_access, "skipping %d postgap sector(s), last pack at %d",
                 i_next - i_last, i_last - 1 );

    p_sys->titles[i_title].i_end = i_last;

    return i_last;
}

/*****************************************************************************
 * VCDOpen: open vcd
 *****************************************************************************/
static int Open( vlc_object_t *p_this )
{
    stream_t     *p_access = (stream_t *)p_this;
    access_sys_t *p_sys;
    if( p_access->psz_filepath == NULL )
        return VLC_EGENERIC;

    char *psz_dup = ToLocaleDup( p_access->psz_filepath );

    char *psz;
    int i_title = 0;
    int i_chapter = 0;
    vcddev_t *vcddev;

    /* Command line: vcd://[dev_path][#title[,chapter]] */
    if( ( psz = strchr( psz_dup, '#' ) ) )
    {
        *psz++ = '\0';

        i_title = strtol( psz, &psz, 0 );
        if( *psz )
            i_chapter = strtol( psz+1, &psz, 0 );
    }

    if( *psz_dup == '\0' )
    {
        free( psz_dup );

        /* Only when selected */
        if( strcmp( p_access->psz_name, "vcd" ) &&
            strcmp( p_access->psz_name, "svcd" ) )
            return VLC_EGENERIC;

        psz_dup = var_CreateGetString( p_access, "vcd" );
        if( *psz_dup == '\0' )
        {
            free( psz_dup );
            return VLC_EGENERIC;
        }
    }

#if defined( _WIN32 ) || defined( __OS2__ )
    if( psz_dup[0] && psz_dup[1] == ':' &&
        psz_dup[2] == '\\' && psz_dup[3] == '\0' ) psz_dup[2] = '\0';
#endif

    /* Open VCD */
    vcddev = ioctl_Open( p_this, psz_dup );
    free( psz_dup );
    if( !vcddev )
        return VLC_EGENERIC;

    /* Set up p_access */
    p_access->p_sys = p_sys = calloc( 1, sizeof( access_sys_t ) );
    if( unlikely(!p_sys ))
        goto error;
    p_sys->vcddev = vcddev;
    p_sys->offset = 0;

    for( size_t i = 0; i < ARRAY_SIZE(p_sys->titles); i++ )
    {
        p_sys->titles[i].seekpoints = NULL;
        p_sys->titles[i].i_start = -1;
        p_sys->titles[i].i_end = -1;
    }

    /* We read the Table Of Content information */
    p_sys->p_toc = ioctl_GetTOC( VLC_OBJECT(p_access), p_sys->vcddev );
    if( p_sys->p_toc == NULL )
    {
        msg_Err( p_access, "unable to count tracks" );
        goto error;
    }
    else if( p_sys->p_toc->i_tracks <= 1 )
    {
        vcddev_toc_Free( p_sys->p_toc );
        p_sys->p_toc = NULL;
        msg_Err( p_access, "no movie tracks found" );
        goto error;
    }

    /* The first title isn't usable */
#define USABLE_TITLES(a) (a - 1)
    //p_sys->i_titles--;

    for( int i = 0; i < USABLE_TITLES(p_sys->p_toc->i_tracks); i++ )
    {
        msg_Dbg( p_access, "title[%d] start=%d", i, p_sys->p_toc->p_sectors[i].i_lba );
        msg_Dbg( p_access, "title[%d] end=%d", i, p_sys->p_toc->p_sectors[i+1].i_lba );
    }

    /* Map entry points into chapters */
    if( EntryPoints( p_access ) )
    {
        msg_Warn( p_access, "could not read entry points, will not use them" );
    }

    /* Starting title/chapter and sector */
    if( i_title < 0 || i_title >= USABLE_TITLES(p_sys->p_toc->i_tracks) )
        i_title = 0;
    if( (unsigned)i_chapter >= p_sys->titles[i_title].count )
        i_chapter = 0;

    const int i_start = TitleStart( p_access, i_title );

    p_sys->i_sector = i_start;
    if( i_chapter > 0 )
        p_sys->i_sector += p_sys->titles[i_title].seekpoints[i_chapter]
                           / VCD_DATA_SIZE;

    /* p_access */
    p_access->pf_read    = NULL;
    p_access->pf_block   = Block;
    p_access->pf_control = Control;
    p_access->pf_seek    = Seek;

    p_sys->i_current_title = i_title;
    p_sys->i_current_seekpoint = i_chapter;
    p_sys->offset = (uint64_t)(p_sys->i_sector - i_start) * VCD_DATA_SIZE;

    return VLC_SUCCESS;

error:
    ioctl_Close( VLC_OBJECT(p_access), vcddev );
    free( p_sys );
    return VLC_EGENERIC;
}

/*****************************************************************************
 * Close: closes vcd
 *****************************************************************************/
static void Close( vlc_object_t *p_this )
{
    stream_t     *p_access = (stream_t *)p_this;
    access_sys_t *p_sys = p_access->p_sys;

    for( size_t i = 0; i < ARRAY_SIZE(p_sys->titles); i++ )
        free( p_sys->titles[i].seekpoints );

    vcddev_toc_Free( p_sys->p_toc );

    ioctl_Close( p_this, p_sys->vcddev );
    free( p_sys );
}

/*****************************************************************************
 * Control:
 *****************************************************************************/
static int Control( stream_t *p_access, int i_query, va_list args )
{
    access_sys_t *p_sys = p_access->p_sys;
    input_title_t ***ppp_title;

    switch( i_query )
    {
        /* */
        case STREAM_CAN_SEEK:
        case STREAM_CAN_FASTSEEK:
        case STREAM_CAN_PAUSE:
        case STREAM_CAN_CONTROL_PACE:
            *va_arg( args, bool* ) = true;
            break;

        case STREAM_GET_SIZE:
        {
            int i = p_sys->i_current_title;

            *va_arg( args, uint64_t * ) =
                (TitleEnd( p_access, i ) -
                 TitleStart( p_access, i )) * (uint64_t)VCD_DATA_SIZE;
            break;
        }

        /* */
        case STREAM_GET_PTS_DELAY:
            *va_arg( args, vlc_tick_t * ) = VLC_TICK_FROM_MS(
                var_InheritInteger(p_access, "disc-caching") );
            break;

        /* */
        case STREAM_SET_PAUSE_STATE:
            break;

        case STREAM_GET_TITLE_INFO:
            ppp_title = va_arg( args, input_title_t*** );
            /* Duplicate title infos */
            *ppp_title = vlc_alloc( USABLE_TITLES(p_sys->p_toc->i_tracks),
                                    sizeof(input_title_t *) );
            if (!*ppp_title)
                return VLC_ENOMEM;
            *va_arg( args, int* ) = USABLE_TITLES(p_sys->p_toc->i_tracks);
            for( int i = 0; i < USABLE_TITLES(p_sys->p_toc->i_tracks); i++ )
                (*ppp_title)[i] = vlc_input_title_New();
            break;

        case STREAM_GET_TITLE:
            *va_arg( args, unsigned * ) = p_sys->i_current_title;
            break;

        case STREAM_GET_SEEKPOINT:
            *va_arg( args, unsigned * ) = p_sys->i_current_seekpoint;
            break;

        case STREAM_GET_CONTENT_TYPE:
            *va_arg( args, char ** ) = strdup("video/MP2P");
            break;

        case STREAM_SET_TITLE:
        {
            int i = va_arg( args, int );
            if( i < 0 || i >= USABLE_TITLES(p_sys->p_toc->i_tracks) )
                return VLC_EGENERIC;

            p_sys->offset = 0;
            p_sys->i_current_title = i;
            p_sys->i_current_seekpoint = 0;
            p_sys->i_sector = TitleStart( p_access, i );
            break;
        }

        case STREAM_SET_SEEKPOINT:
        {
            int i = va_arg( args, int );
            unsigned i_title = p_sys->i_current_title;

            if( p_sys->titles[i_title].count > 0 )
            {
                p_sys->i_current_seekpoint = i;

                const int i_start = TitleStart( p_access, i_title );

                p_sys->i_sector = i_start +
                    p_sys->titles[i_title].seekpoints[i] / VCD_DATA_SIZE;

                p_sys->offset = (uint64_t)(p_sys->i_sector -
                    i_start) * VCD_DATA_SIZE;
            }
            break;
        }

        default:
            return VLC_EGENERIC;
    }
    return VLC_SUCCESS;
}

/*****************************************************************************
 * Block:
 *****************************************************************************/
static block_t *Block( stream_t *p_access, bool *restrict eof )
{
    access_sys_t *p_sys = p_access->p_sys;
    const vcddev_toc_t *p_toc = p_sys->p_toc;
    int i_blocks = VCD_BLOCKS_ONCE;
    block_t *p_block;

    /* Check end of title */
    while( p_sys->i_sector >= TitleEnd( p_access, p_sys->i_current_title ) )
    {
        if( p_sys->i_current_title + 2 > USABLE_TITLES(p_toc->i_tracks) )
        {
            *eof = true;
            return NULL;
        }

        p_sys->i_current_title++;
        p_sys->i_current_seekpoint = 0;
        p_sys->offset = 0;
        p_sys->i_sector = TitleStart( p_access, p_sys->i_current_title );
    }

    /* Don't read after the end of a title */
    if( p_sys->i_sector + i_blocks >= TitleEnd( p_access, p_sys->i_current_title ) )
    {
        i_blocks = TitleEnd( p_access, p_sys->i_current_title ) - p_sys->i_sector;
    }

    /* Do the actual reading */
    if( i_blocks < 0 || !( p_block = block_Alloc( i_blocks * VCD_DATA_SIZE ) ) )
    {
        msg_Err( p_access, "cannot get a new block of size: %i",
                 i_blocks * VCD_DATA_SIZE );
        return NULL;
    }

    int i_ret = ioctl_ReadSectors( VLC_OBJECT(p_access), p_sys->vcddev,
                                   p_sys->i_sector, p_block->p_buffer, i_blocks, VCD_TYPE );
    if( i_ret < 0 )
    {
        msg_Err( p_access, "cannot read sector %i", p_sys->i_sector );
        block_Release( p_block );

        /* Nothing left to read: the disc is gone, or the drive is. */
        if( i_ret == -2 )
        {
            *eof = true;
            return NULL;
        }

        /* Try to skip one sector (in case of bad sectors) */
        p_sys->offset += VCD_DATA_SIZE;
        p_sys->i_sector++;
        return NULL;
    }

    /* Update seekpoints */
    for( int i_read = 0; i_read < i_blocks; i_read++ )
    {
        int i_title = p_sys->i_current_title;

        if( p_sys->titles[i_title].count > 0 &&
            p_sys->i_current_seekpoint + 1 < p_sys->titles[i_title].count &&
                (p_sys->offset + i_read * VCD_DATA_SIZE) >=
            p_sys->titles[i_title].seekpoints[p_sys->i_current_seekpoint + 1] )
        {
            msg_Dbg( p_access, "seekpoint change" );
            p_sys->i_current_seekpoint++;
        }
    }

    /* Update a few values */
    p_sys->offset += p_block->i_buffer;
    p_sys->i_sector += i_blocks;

    return p_block;
}

/*****************************************************************************
 * Seek:
 *****************************************************************************/
static int Seek( stream_t *p_access, uint64_t i_pos )
{
    access_sys_t *p_sys = p_access->p_sys;
    int i_title = p_sys->i_current_title;
    unsigned i_seekpoint;

    /* Next sector to read */
    p_sys->offset = i_pos;
    p_sys->i_sector = i_pos / VCD_DATA_SIZE + TitleStart( p_access, i_title );

    /* Update current seekpoint */
    for( i_seekpoint = 0; i_seekpoint < p_sys->titles[i_title].count; i_seekpoint++ )
    {
        if( i_seekpoint + 1 >= p_sys->titles[i_title].count ) break;
        if( 0 < p_sys->titles[i_title].seekpoints[i_seekpoint + 1] &&
            i_pos < p_sys->titles[i_title].seekpoints[i_seekpoint + 1] ) break;
    }

    if( i_seekpoint != p_sys->i_current_seekpoint )
    {
        msg_Dbg( p_access, "seekpoint change" );
        p_sys->i_current_seekpoint = i_seekpoint;
    }

    return VLC_SUCCESS;
}

/*****************************************************************************
 * EntryPoints: Reads the information about the entry points on the disc.
 *****************************************************************************/
static int EntryPoints( stream_t *p_access )
{
    access_sys_t *p_sys = p_access->p_sys;
    const vcddev_toc_t *p_toc = p_sys->p_toc;
    uint8_t      sector[VCD_DATA_SIZE];

    entries_sect_t entries;
    int i_nb;

    /* Read the entry point sector */
    if( ioctl_ReadSectors( VLC_OBJECT(p_access), p_sys->vcddev,
        VCD_ENTRIES_SECTOR, sector, 1, VCD_TYPE ) < 0 )
    {
        msg_Err( p_access, "could not read entry points sector" );
        return VLC_EGENERIC;
    }
    memcpy( &entries, sector, CD_SECTOR_SIZE );

    i_nb = GetWBE( &entries.i_entries_nb );
    if( i_nb == 0 || i_nb > 500 /* See 6.3.2 */ )
    {
        msg_Err( p_access, "invalid entry points number" );
        return VLC_EGENERIC;
    }

    if( strncmp( entries.psz_id, "ENTRYVCD", sizeof( entries.psz_id ) ) &&
        strncmp( entries.psz_id, "ENTRYSVD", sizeof( entries.psz_id ) ) )
    {
        msg_Err( p_access, "unrecognized entry points format" );
        return VLC_EGENERIC;
    }

    for( int i = 0; i < i_nb; i++ )
    {
        const int i_title = BCD_TO_BIN(entries.entry[i].i_track) - 2;
        const int i_sector =
            (MSF_TO_LBA2( BCD_TO_BIN( entries.entry[i].msf.minute ),
                          BCD_TO_BIN( entries.entry[i].msf.second ),
                          BCD_TO_BIN( entries.entry[i].msf.frame  ) ));
        if( i_title < 0 ) continue;   /* Should not occur */
        if( i_title >= USABLE_TITLES(p_toc->i_tracks) ) continue;

        msg_Dbg( p_access, "Entry[%d] title=%d sector=%d",
                 i, i_title, i_sector );

        p_sys->titles[i_title].seekpoints = xrealloc(
            p_sys->titles[i_title].seekpoints,
            sizeof( uint64_t ) * (p_sys->titles[i_title].count + 1) );
        p_sys->titles[i_title].seekpoints[p_sys->titles[i_title].count++] =
            (i_sector - p_toc->p_sectors[i_title+1].i_lba) * VCD_DATA_SIZE;
    }

    return VLC_SUCCESS;
}
