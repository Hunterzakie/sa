/*****************************************************************************
 * icymeta.c: SHOUTcast/Icecast ICY in-band metadata
 *****************************************************************************
 * Copyright (C) 2026 Tom Taylor
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
# include <config.h>
#endif

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <vlc_common.h>
#include <vlc_block.h>

#include "message.h"
#include "icymeta.h"

struct vlc_icy_meta
{
    size_t interval;
    size_t remain;
    size_t block_len;
    size_t block_off;
    char *block;
    uint64_t audio_total;  /* de-framed audio bytes emitted so far */
};

static struct vlc_icy_meta *vlc_icy_meta_New(size_t interval)
{
    assert(interval > 0);

    struct vlc_icy_meta *m = malloc(sizeof (*m));
    if (unlikely(m == NULL))
        return NULL;

    *m = (struct vlc_icy_meta){ .interval = interval, .remain = interval };
    return m;
}

static char *vlc_icy_meta_title(const char *block)
{
    const char *p = strcasestr(block, "StreamTitle=");
    if (p == NULL)
        return NULL;

    p += strlen("StreamTitle=");

    const char *q;
    if (*p == '\'' || *p == '"')
    {
        char end[] = { p[0], ';', '\0' };
        q = strstr(p + 1, end);
        if (q == NULL)
            q = strchr(p + 1, ';');
        p++;
    }
    else
        q = strchr(p, ';');

    size_t len = (q != NULL) ? (size_t)(q - p) : strlen(p);
    return strndup(p, len);
}

static char *vlc_icy_meta_flush(struct vlc_icy_meta *m)
{
    m->block[m->block_len] = '\0';

    char *title = vlc_icy_meta_title(m->block);

    free(m->block);
    m->block = NULL;
    m->block_len = 0;
    m->block_off = 0;
    m->remain = m->interval;

    return title;
}

/* De-frames ICY audio: audio bytes are moved to the front of data (returned
 * count), and *title is set to a newly allocated string when a metadata block
 * completes, with *offset the de-framed audio offset at which it takes effect
 * (only written together with *title). */
static size_t vlc_icy_meta_Split(struct vlc_icy_meta *m, void *data, size_t len,
                                 char **restrict title,
                                 uint64_t *restrict offset)
{
    unsigned char *in = data;
    unsigned char *out = data;
    size_t audio = 0;

    *title = NULL;

    while (len > 0)
    {
        if (m->remain > 0)
        {
            size_t copy = (len < m->remain) ? len : m->remain;

            memmove(out, in, copy);
            in += copy;
            out += copy;
            audio += copy;
            m->audio_total += copy;
            len -= copy;
            m->remain -= copy;
            continue;
        }

        if (m->block == NULL && m->block_off == 0)
        {   /* Length prefix: block size is the byte value times 16. */
            m->block_len = (size_t)*in++ << 4;
            len--;

            if (m->block_len == 0)
            {
                m->remain = m->interval;
                continue;
            }

            m->block = malloc(m->block_len + 1);
            if (unlikely(m->block == NULL))
            {
                m->remain = m->interval;
                m->block_len = 0;
                continue;
            }
        }

        size_t need = m->block_len - m->block_off;
        size_t copy = (len < need) ? len : need;

        memcpy(m->block + m->block_off, in, copy);
        in += copy;
        len -= copy;
        m->block_off += copy;

        if (m->block_off == m->block_len)
        {
            char *t = vlc_icy_meta_flush(m);
            if (t != NULL)
            {
                free(*title);
                *title = t;
                *offset = m->audio_total;
            }
        }
    }

    return audio;
}

static void vlc_icy_meta_Delete(struct vlc_icy_meta *m)
{
    free(m->block);
    free(m);
}

struct vlc_icy_stream
{
    struct vlc_http_stream stream;
    struct vlc_http_stream *parent;
    struct vlc_icy_meta *meta;
    char *title;
    uint64_t title_offset;
};

static struct vlc_http_msg *vlc_icy_stream_wait(struct vlc_http_stream *stream)
{
    struct vlc_icy_stream *s =
        container_of(stream, struct vlc_icy_stream, stream);
    return vlc_http_stream_read_headers(s->parent);
}

static block_t *vlc_icy_stream_read(struct vlc_http_stream *stream)
{
    struct vlc_icy_stream *s =
        container_of(stream, struct vlc_icy_stream, stream);

    block_t *block = vlc_http_stream_read(s->parent);
    if (block == NULL || block == vlc_http_error)
        return block;

    char *title;
    uint64_t offset;
    block->i_buffer = vlc_icy_meta_Split(s->meta, block->p_buffer,
                                         block->i_buffer, &title, &offset);
    if (title != NULL)
    {
        free(s->title);
        s->title = title;
        s->title_offset = offset;
    }
    return block;
}

static void vlc_icy_stream_close(struct vlc_http_stream *stream, bool abort)
{
    struct vlc_icy_stream *s =
        container_of(stream, struct vlc_icy_stream, stream);

    vlc_http_stream_close(s->parent, abort);
    vlc_icy_meta_Delete(s->meta);
    free(s->title);
    free(s);
}

static struct vlc_http_stream_cbs vlc_icy_stream_callbacks =
{
    vlc_icy_stream_wait,
    NULL,
    vlc_icy_stream_read,
    vlc_icy_stream_close,
};

struct vlc_http_stream *vlc_icy_open(struct vlc_http_stream *parent,
                                     size_t interval)
{
    struct vlc_icy_stream *s = malloc(sizeof (*s));
    if (unlikely(s == NULL))
        return NULL;

    struct vlc_icy_meta *meta = vlc_icy_meta_New(interval);
    if (unlikely(meta == NULL))
    {
        free(s);
        return NULL;
    }

    *s = (struct vlc_icy_stream){
        .stream.cbs = &vlc_icy_stream_callbacks,
        .parent = parent,
        .meta = meta,
    };
    return &s->stream;
}

bool vlc_icy_stream_get_timed_title(struct vlc_http_stream *stream,
                                    char **restrict titlep,
                                    uint64_t *restrict offsetp)
{
    struct vlc_icy_stream *s =
        container_of(stream, struct vlc_icy_stream, stream);

    if (s->title == NULL)
        return false;

    char *dup = strdup(s->title);
    if (unlikely(dup == NULL))
        return false;

    *titlep = dup;
    *offsetp = s->title_offset;
    return true;
}

#ifdef ICYMETA_TEST
# undef NDEBUG
# include <stdio.h>

const char vlc_module_name[] = "test_icymeta";

/* vlc_http_error is defined in message.c; stub it so the test links against
 * libvlccore alone (the parent stream is mocked and never returns it). */
static char icy_test_error_sentinel;
void *const vlc_http_error = &icy_test_error_sentinel;

static void test_single(void)
{
    struct vlc_icy_meta *m = vlc_icy_meta_New(4);
    assert(m != NULL);

    char buf[4 + 1 + 32] = "abcd" "\x02" "StreamTitle='Foo';";
    char *title = (char *)0x1;
    uint64_t offset = 0;

    size_t audio = vlc_icy_meta_Split(m, buf, sizeof (buf), &title, &offset);
    assert(audio == 4);
    assert(!memcmp(buf, "abcd", 4));
    assert(title != NULL);
    assert(!strcmp(title, "Foo"));
    /* the title takes effect after the first interval (4 de-framed bytes) */
    assert(offset == 4);
    free(title);

    vlc_icy_meta_Delete(m);
}

static void test_split(void)
{
    struct vlc_icy_meta *m = vlc_icy_meta_New(4);
    assert(m != NULL);

    char part1[] = "abcd" "\x02" "StreamTitle='Ba";
    char *title = (char *)0x1;
    uint64_t offset = 0;

    size_t audio = vlc_icy_meta_Split(m, part1, sizeof (part1) - 1, &title,
                                      &offset);
    assert(audio == 4);
    assert(title == NULL);

    char part2[17] = "r';";
    audio = vlc_icy_meta_Split(m, part2, sizeof (part2), &title, &offset);
    assert(audio == 0);
    assert(title != NULL);
    assert(!strcmp(title, "Bar"));
    free(title);

    vlc_icy_meta_Delete(m);
}

static void test_double_quote(void)
{
    struct vlc_icy_meta *m = vlc_icy_meta_New(2);
    assert(m != NULL);

    char buf[2 + 1 + 32] = "hi" "\x02" "StreamTitle=\"Quux\";";
    char *title = NULL;
    uint64_t offset = 0;

    size_t audio = vlc_icy_meta_Split(m, buf, sizeof (buf), &title, &offset);
    assert(audio == 2);
    assert(title != NULL);
    assert(!strcmp(title, "Quux"));
    free(title);

    vlc_icy_meta_Delete(m);
}

static void test_empty(void)
{
    struct vlc_icy_meta *m = vlc_icy_meta_New(2);
    assert(m != NULL);

    char buf[2 + 1 + 16] = "hi" "\x01" "StreamTitle='';";
    char *title = (char *)0x1;
    uint64_t offset = 0;

    size_t audio = vlc_icy_meta_Split(m, buf, sizeof (buf), &title, &offset);
    assert(audio == 2);
    assert(title != NULL);
    assert(title[0] == '\0');
    free(title);

    vlc_icy_meta_Delete(m);
}

static void test_no_title(void)
{
    struct vlc_icy_meta *m = vlc_icy_meta_New(2);
    assert(m != NULL);

    char buf[2 + 1 + 32] = "hi" "\x02" "StreamUrl='http://x';";
    char *title = (char *)0x1;
    uint64_t offset = 0;

    size_t audio = vlc_icy_meta_Split(m, buf, sizeof (buf), &title, &offset);
    assert(audio == 2);
    assert(title == NULL);

    vlc_icy_meta_Delete(m);
}

static void test_no_meta(void)
{
    struct vlc_icy_meta *m = vlc_icy_meta_New(4);
    assert(m != NULL);

    char buf[] = "wxyz" "\x00" "abcd";
    char *title = (char *)0x1;
    uint64_t offset = 0;

    size_t audio = vlc_icy_meta_Split(m, buf, 4 + 1 + 4, &title, &offset);
    assert(audio == 8);
    assert(!memcmp(buf, "wxyzabcd", 8));
    assert(title == NULL);

    vlc_icy_meta_Delete(m);
}

static void test_offset_to_pts(void)
{
    assert(vlc_icy_meta_OffsetToPts(16000, 128000) == VLC_TICK_FROM_MS(1000));
    assert(vlc_icy_meta_OffsetToPts(40000, 320000) == VLC_TICK_FROM_MS(1000));
    assert(vlc_icy_meta_OffsetToPts(0, 128000) == 0);
    assert(vlc_icy_meta_OffsetToPts(16000, 0) == VLC_TICK_INVALID);
}

/* A mock body stream returning one ICY frame: 16 audio bytes, then a 16-byte
 * metadata block carrying a StreamTitle. */
static const char icy_frame[] =
    "AAAAAAAAAAAAAAAA" "\x01" "StreamTitle='H';";
static bool icy_parent_eof;

static block_t *icy_parent_read(struct vlc_http_stream *stream)
{
    (void) stream;
    if (icy_parent_eof)
        return NULL;
    icy_parent_eof = true;

    block_t *b = block_Alloc(sizeof (icy_frame) - 1);
    assert(b != NULL);
    memcpy(b->p_buffer, icy_frame, sizeof (icy_frame) - 1);
    return b;
}

static void icy_parent_close(struct vlc_http_stream *stream, bool abort)
{
    (void) stream; (void) abort;
}

static const struct vlc_http_stream_cbs icy_parent_cbs =
{
    .read = icy_parent_read,
    .close = icy_parent_close,
};

static struct vlc_http_stream icy_parent = { &icy_parent_cbs };

static void test_stream_filter(void)
{
    icy_parent_eof = false;

    struct vlc_http_stream *s = vlc_icy_open(&icy_parent, 16);
    assert(s != NULL);

    block_t *b = vlc_http_stream_read(s);
    assert(b != NULL && b != vlc_http_error);
    assert(b->i_buffer == 16); /* metadata stripped, only audio remains */
    assert(!memcmp(b->p_buffer, "AAAAAAAAAAAAAAAA", 16));
    block_Release(b);

    char *title;
    uint64_t offset;
    assert(vlc_icy_stream_get_timed_title(s, &title, &offset));
    assert(!strcmp(title, "H"));
    assert(offset == 16); /* takes effect after the 16 de-framed audio bytes */
    free(title);

    vlc_http_stream_close(s, false);
}

int main(void)
{
    test_single();
    test_split();
    test_double_quote();
    test_empty();
    test_no_title();
    test_no_meta();
    test_offset_to_pts();
    test_stream_filter();

    return 0;
}
#endif /* ICYMETA_TEST */
