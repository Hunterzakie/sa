/*****************************************************************************
 * bkfl.c : .BKFL camera recordings demuxer
 *****************************************************************************
 * Copyright (C) 2026 VideoLabs, VLC authors and VideoLAN
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
#include <vlc_demux.h>
#include <vlc_codec.h>

//#define BKFL_DEBUG

/*****************************************************************************
 * Local prototypes
 *****************************************************************************/
typedef struct
{
    vlc_tick_t time;
    uint64_t offset;
} index_t;

typedef struct VLC_VECTOR(index_t) vec_index_t;

typedef struct
{
    es_format_t fmt;
    es_out_id_t *p_es;
    vec_index_t index;
    vlc_tick_t pts_first, pts_last, pts;
} demux_sys_t;

static int Demux(demux_t *);
static int Control(demux_t *, int, va_list);

/*
Offset (Dec)  Size (Bytes)   Field Name             Description
================================================================================
00            04             Magic                  "SHFL" (53 48 46 4C)
04            01             version ?              0x03
05            02             ??
07            01             $CDC Flag              0x01: Codec Info / 0x00: None
08            16             ??
24            04             Total Size             Total Size (LE)
28            08             Timestamp (PTS0)       FILETIME (Presentation)
36            04             ??                     some monotonic counter
40            08             ??                     00
48            04             Video NAL Size         NALU(s) Payload Size (LE)
52            08             Timestamp (PTS1)       FILETIME (Presentation)
60            08             Timestamp (SYS)        FILETIME (Presentation)
--------------------------------------------------------------------------------
IF ($CDC == 0x01) [Codec Info Extension]
--------------------------------------------------------------------------------
68            04             ??                     Usually 01 00 00 00
72            04             FourCC                 "H264" (48 32 36 34)
76            02             Video Width            uint16 (LE)
78            02             Video Height           uint16 (LE)
================================================================================
*/

#define BKFL_HEADER_RESYNC 24 // to adjust total - nalu sizes, might be var size like codec section
#define BKFL_HEADER_CODEC 12

#define BKFL_HEADER_SIZE_MIN 68
#define BKFL_HEADER_SIZE_MAX (BKFL_HEADER_SIZE_MIN+BKFL_HEADER_CODEC)

struct header
{
    uint8_t flag;
    uint8_t size;
    uint32_t nalu_size;
    uint64_t sendtime;
    uint64_t pts;
    vlc_fourcc_t codec;
    uint16_t width, height;
};

#define BKFL_RESYNC_CHUNK 4096
#define BKFL_MAX_RESYNC   (1024*1024)

/*****************************************************************************
 * Resync:
 *****************************************************************************/
static const uint8_t * FindSyncCode(const uint8_t *data, size_t length)
{
    /* Hardcoded Boyer-Moore-Horspool for SHFL */
    for (size_t k = 3; k < length;)
    {
        if (data[k] == 'L')
            if (data[k-1] == 'F' && data[k-2] == 'H' && data[k-3] == 'S')
                return &data[k-3];

        uint8_t c = data[k];

        if (c == 'S')      k += 3;
        else if (c == 'H') k += 2;
        else if (c == 'F') k += 1;
        else               k += 4;
    }

    return NULL;
}

static int Resync(stream_t *s)
{
    uint64_t forwarded = 0;

    while (!vlc_stream_Eof(s) && forwarded <= BKFL_MAX_RESYNC)
    {
        const uint8_t *peek;
        ssize_t peek_size = vlc_stream_Peek(s, &peek, BKFL_RESYNC_CHUNK);
        if (peek_size < BKFL_HEADER_SIZE_MAX)
            break;
        forwarded += BKFL_RESYNC_CHUNK;

        const uint8_t *p = FindSyncCode(peek, peek_size);
        if(!p)
        {
            ssize_t read = vlc_stream_Read(s, NULL, peek_size - 3);
            if(read < peek_size - 3)
                break;
        }
        else
        {
            size_t offset = p - peek;
            ssize_t read = vlc_stream_Read(s, NULL, offset);
            if(read < 0 || (size_t) read < offset)
                break;
            return VLC_SUCCESS;
        }
    }
    return VLC_EGENERIC;
}

static int SyncOnMagicBytes(stream_t *s, const uint8_t **pp_peek, size_t *p_size)
{
    const uint8_t *peek;
    ssize_t peek_size;

    for(;;)
    {
        peek_size = vlc_stream_Peek(s, &peek, BKFL_HEADER_SIZE_MAX);
        if (peek_size < BKFL_HEADER_SIZE_MAX)
            return VLC_EGENERIC;

        if (!memcmp(peek, "SHFL", 4))
            break;

        msg_Dbg(s,"Resyncing from %"PRIu64, vlc_stream_Tell(s));
        if(Resync(s) == VLC_EGENERIC)
            return VLC_EGENERIC;
        msg_Dbg(s,"Resynced at %"PRIu64, vlc_stream_Tell(s));
    }

    *pp_peek = peek;
    *p_size = peek_size;

    return VLC_SUCCESS;
}

#ifdef BKFL_DEBUG
static void formattime(vlc_tick_t pts, char *time_str, uint32_t *microseconds)
{
    const uint64_t EPOCH_DIFF_SEC = UINT64_C(11644473600);

    uint64_t total_seconds = SEC_FROM_VLC_TICK(pts);
    *microseconds = (uint32_t)((pts % CLOCK_FREQ) / 10);

    time_t unix_time = (time_t)(total_seconds - EPOCH_DIFF_SEC);

    struct tm *tm_info = gmtime(&unix_time);
    strftime(time_str, 32, "%Y-%m-%d %H:%M:%S", tm_info);
}
#endif

static int ParseHeader(const uint8_t *data, size_t size, struct header *h)
{
    if(size < BKFL_HEADER_SIZE_MIN)
        return VLC_EGENERIC;

    unsigned header_size = BKFL_HEADER_SIZE_MIN;

    size_t substract = BKFL_HEADER_RESYNC;
    if(data[7] == 0x01)
    {
        header_size += BKFL_HEADER_CODEC;
        substract += BKFL_HEADER_CODEC;
    }

    if(size < header_size)
        return VLC_EGENERIC;

    const uint8_t *p = &data[0];

    uint32_t total_size = GetDWLE(&p[24]);
    uint32_t nalu_size = GetDWLE(&p[48]);
    uint64_t pts = GetQWLE(&p[60]);

    if(nalu_size > total_size)
        return VLC_EGENERIC;

    h->flag = data[7];
    h->size = header_size;
    h->nalu_size = total_size - substract;
    h->pts = VLC_TICK_FROM_MSFTIME(pts);

    if(h->flag)
    {
        h->codec = VLC_FOURCC(p[72],p[73],p[74],p[75]);
        h->width = GetWLE(&p[76]);
        h->height = GetWLE(&p[78]);
    }

#ifdef BKFL_DEBUG
    uint64_t dts = GetQWLE(&p[28]);
    uint64_t sys = GetQWLE(&p[52]);

    char time_str[32];
    uint32_t microseconds;
    formattime(pts, time_str, &microseconds);
    fprintf(stderr,"PTS %"PRIu64" UTC Time: %s.%06u\n", pts, time_str, microseconds);
    formattime(dts, time_str, &microseconds);
    fprintf(stderr,"DTS %"PRIu64" UTC Time: %s.%06u\n", dts, time_str, microseconds);
    formattime(sys, time_str, &microseconds);
    fprintf(stderr,"SYS %"PRIu64" UTC Time: %s.%06u\n", sys, time_str, microseconds);
#endif

    return VLC_SUCCESS;
}

static int ProbeAndBuildIndex(stream_t *s, demux_sys_t *sys)
{
    uint64_t pos, startpos = pos = vlc_stream_Tell(s);

    for(;;)
    {
        const uint8_t *peek;
        size_t peek_size;

        if(SyncOnMagicBytes(s, &peek, &peek_size) != VLC_SUCCESS)
            goto end;

        struct header header = {};
        if(ParseHeader(peek, peek_size, &header)!= VLC_SUCCESS)
            break;

        if(sys->pts_first == VLC_TICK_INVALID)
            sys->pts_first = VLC_TICK_0 + header.pts;
        sys->pts_last = VLC_TICK_0 + header.pts;

        pos = vlc_stream_Tell(s);
        if(header.flag)
        {
            if(!sys->fmt.i_codec)
            {
                sys->fmt.i_codec = header.codec;
                sys->fmt.video.i_visible_width = header.width;
                sys->fmt.video.i_visible_height = header.height;
            }
            index_t entry = {VLC_TICK_0 + header.pts, pos};
            if(!vlc_vector_push(&sys->index, entry))
                break;
#ifdef BKFL_DEBUG
            msg_Dbg(s, "Adding seek point %"PRId64" --> %"PRId64, entry.time, entry.offset);
#endif
        }

        pos += header.size + header.nalu_size;
        if(vlc_stream_Seek(s, pos) != VLC_SUCCESS)
            break;
    }

end:
    if(vlc_stream_Seek(s, startpos) != VLC_SUCCESS)
        return VLC_EGENERIC;

    return VLC_SUCCESS;
}

/*****************************************************************************
 * Demux: reads and demuxes data packets
 *****************************************************************************/

static int Demux(demux_t *demux)
{
    demux_sys_t *sys = demux->p_sys;

    const uint8_t *peek;
    size_t peek_size;

    if(SyncOnMagicBytes(demux->s, &peek, &peek_size)!= VLC_SUCCESS)
        return VLC_DEMUXER_EOF;

    struct header header = {};
    if(ParseHeader(peek, peek_size, &header)!= VLC_SUCCESS)
        return VLC_DEMUXER_EOF;

    ssize_t read = vlc_stream_Read(demux->s, NULL, header.size);
    if(read < header.size)
        return VLC_DEMUXER_EOF;

    if(!sys->fmt.i_codec && header.flag && !sys->p_es)
    {
        sys->fmt.i_codec = header.codec;
        sys->fmt.video.i_visible_width = header.width;
        sys->fmt.video.i_visible_height = header.height;
        sys->p_es = es_out_Add(demux->out, &sys->fmt);
    }

    sys->pts = VLC_TICK_0 + header.pts;
    if(sys->pts_first == VLC_TICK_INVALID)
        sys->pts_first = sys->pts;
    if(sys->pts_last < sys->pts)
        sys->pts_last = sys->pts;

    block_t *block = vlc_stream_Block(demux->s, header.nalu_size);
    if(!block)
        return VLC_DEMUXER_EOF;
    block->i_pts = sys->pts;

    es_out_SetPCR(demux->out, sys->pts);
    if(sys->p_es)
        es_out_Send(demux->out, sys->p_es, block);

    return VLC_DEMUXER_SUCCESS;
}

static int SeekTo(demux_t *demux, vlc_tick_t ts, bool precise)
{
    demux_sys_t *sys = demux->p_sys;

    if(sys->index.size == 0)
        return VLC_EGENERIC;

    index_t *entry = NULL;
    for(size_t i=0; i<sys->index.size; i++)
    {
        if(sys->index.data[i].time > ts)
            break;
        entry = &sys->index.data[i];
    }

    if(!entry)
        return VLC_EGENERIC;

    sys->pts = entry->time;
    int ret = vlc_stream_Seek(demux->s, entry->offset);
    if(ret == VLC_SUCCESS && precise)
        ret = es_out_SetNextDisplayTime(demux->out, ts);
    return ret;
}

/*****************************************************************************
 * Control:
 *****************************************************************************/
static int Control(demux_t *demux, int i_query, va_list args)
{
    demux_sys_t *sys  = demux->p_sys;

    switch(i_query)
    {
        case DEMUX_GET_LENGTH:
        {
            if(sys->pts_first >= sys->pts_last)
                return VLC_EGENERIC;
            *va_arg(args, vlc_tick_t *) = sys->pts_last - sys->pts_first;
            break;
        }
        case DEMUX_GET_TIME:
        {
            if(sys->pts != VLC_TICK_INVALID)
                return VLC_EGENERIC;
            *va_arg(args, vlc_tick_t *) = sys->pts;
            break;
        }
        case DEMUX_SET_TIME:
        {
            if(sys->index.size == 0)
                return VLC_EGENERIC;
            vlc_tick_t ts = va_arg(args, vlc_tick_t);
            bool precise = va_arg(args, int);
            return SeekTo(demux, sys->pts_first + ts, precise);
        }
        case DEMUX_GET_NORMAL_TIME:
        {
            if(sys->pts_first >= sys->pts_last)
                return VLC_EGENERIC;
            *va_arg(args, vlc_tick_t *) = sys->pts_first;
            break;
        }
        case DEMUX_GET_POSITION:
        {
            if(sys->pts_first >= sys->pts_last || sys->pts == VLC_TICK_INVALID)
                return demux_vaControlHelper(demux->s, 0, -1, 0, 1, i_query, args);

            *va_arg(args, double *) = (sys->pts - sys->pts_first) / (double) (sys->pts_last - sys->pts_first);
            return VLC_SUCCESS;
        }
        case DEMUX_SET_POSITION:
        {
            if(sys->index.size == 0 || sys->pts_first >= sys->pts_last)
                return demux_vaControlHelper(demux->s, 0, -1, 0, 1, i_query, args);
            double f = va_arg(args, double);
            bool precise = va_arg(args, int);
            return SeekTo(demux, sys->pts_first + (sys->pts_last - sys->pts_first) * f, precise);
        }

        default:
            return demux_vaControlHelper(demux->s,0, -1, 0, 1, i_query, args);
    }

    return VLC_SUCCESS;
}

/*****************************************************************************
 * Close: frees unused data
 *****************************************************************************/
static void Close(vlc_object_t * p_this)
{
    demux_t *demux = (demux_t*)p_this;
    demux_sys_t *sys = demux->p_sys;
    vlc_vector_clear(&sys->index);
    es_format_Clean(&sys->fmt);
    free(sys);
}

/*****************************************************************************
 * Open: initializes demux structures
 *****************************************************************************/

static int OpenBKFL(vlc_object_t * p_this)
{
    demux_t *demux = (demux_t*)p_this;
    demux_sys_t *sys;

    /* Restrict by type first */
    if(!demux->obj.force)
    {
        const char *name = (demux->psz_filepath != NULL) ? demux->psz_filepath
                                                         : demux->psz_location;
        const char *psz_ext = strrchr(name, '.');
        if(!psz_ext || strcasecmp(++psz_ext, "bkfl"))
            return VLC_EGENERIC;
    }

    demux->pf_demux = Demux;
    demux->pf_control= Control;
    demux->p_sys = sys = malloc(sizeof(demux_sys_t));
    if(!sys)
        return VLC_ENOMEM;
    es_format_Init(&sys->fmt, VIDEO_ES, 0);
    sys->fmt.b_packetized = false;
    sys->p_es = NULL;
    vlc_vector_init(&sys->index);
    sys->pts_first = VLC_TICK_INVALID;
    sys->pts_last = VLC_TICK_INVALID;
    sys->pts = VLC_TICK_INVALID;

    bool b_fastseekable;
    if(vlc_stream_Control(demux->s, STREAM_CAN_FASTSEEK, &b_fastseekable) != VLC_SUCCESS)
        b_fastseekable = false;

    if(b_fastseekable)
    {
        if(ProbeAndBuildIndex(demux->s, sys) != VLC_SUCCESS)
        {
            Close(p_this);
            return VLC_EGENERIC;
        }
    }

    if(sys->fmt.i_codec)
        sys->p_es = es_out_Add(demux->out, &sys->fmt);

    return VLC_SUCCESS;
}

/*****************************************************************************
 * Module descriptor
 *****************************************************************************/
vlc_module_begin ()
    set_shortname("bkfl")
    set_subcategory(SUBCAT_INPUT_DEMUX)
    set_description(N_("bkfl video demuxer"))
    set_capability("demux", 8)
    set_callbacks(OpenBKFL, Close)

vlc_module_end ()
