/*****************************************************************************
 * protocol.c
 *****************************************************************************
 * Copyright © 2022 VLC authors and VideoLAN
 *
 * Author: Lucas Ransan <lucas@ransan.tk>
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
# include "config.h"
#endif

#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <string.h>

#include <vlc_common.h>
#include <vlc_charset.h>
#include <vlc_strings.h>
#include <vlc_memstream.h>

#include "mpd.h"
#include "protocol.h"

static void mpd_send_kv_v(
    intf_thread_t *intf,
    mpd_client_t *client,
    const char *key,
    const char *fmt,
    va_list args
) {
    struct vlc_memstream stream;
    vlc_memstream_open(&stream);

    vlc_memstream_printf(&stream, "%s: ", key);
    vlc_memstream_vprintf(&stream, fmt, args);
    vlc_memstream_putc(&stream, '\n');

    if (!vlc_memstream_close(&stream)) {
        write_to_client_buffer(intf, client, stream.ptr);
        free(stream.ptr);
    }
}

void mpd_send_kv(intf_thread_t *intf, mpd_client_t *client, const char *key, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    mpd_send_kv_v(intf, client, key, fmt, ap);
    va_end(ap);
}

static void mpd_send_error_v(
    intf_thread_t *intf,
    mpd_client_t *client,
    int error,
    const char *fmt,
    va_list args
) {
    struct vlc_memstream stream;
    vlc_memstream_open(&stream);

    vlc_memstream_printf(&stream, "ACK [%d@%d] {%s} ", error, client->command_number_in_list,
                         client->current_command ? client->current_command : "");
    vlc_memstream_vprintf(&stream, fmt, args);
    vlc_memstream_putc(&stream, '\n');

    if (!vlc_memstream_close(&stream)) {
        write_to_client_buffer(intf, client, stream.ptr);
        free(stream.ptr);
    }
}

void mpd_send_error(intf_thread_t *intf, mpd_client_t *client, int error, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    mpd_send_error_v(intf, client, error, fmt, ap);
    va_end(ap);
}

void mpd_greet_client(intf_thread_t *intf, mpd_client_t *client) {
    write_to_client_buffer(intf, client, "OK MPD 0.24.0\n");
}

const char *mpd_subsystem_names[MPD_SUBSYSTEM_COUNT] = {
    /* Keep sorted alphabetically */
    "database",
    "message",
    "mixer",
    "mount",
    "neighbor",
    "options",
    "output",
    "partition",
    "player",
    "playlist",
    "sticker",
    "stored_playlist",
    "subscription",
    "update",
};

bool mpd_send_changed_subsystems(intf_thread_t *intf, mpd_client_t *client) {
    bool changed = false;
    for (int i = 0; i < MPD_SUBSYSTEM_COUNT; i++) {
        if (client->idle_events & (1 << i)) {
            changed = true;
            mpd_send_kv(intf, client, "changed", "%s", mpd_subsystem_names[i]);
        }
    }
    client->idle_events = 0;

    return changed;
}

void mpd_tokenize_request(
    intf_thread_t *intf,
    char *reqbuf,
    char *tokens[static MPD_MAXTOK + 1]
) {
    int nb_tok = 0;
    char *pt_start = reqbuf, *pt = reqbuf;
    bool in_quotes = false;

    for (;;) {
        pt = strpbrk(pt, " \n\t\"\\");
        if (nb_tok >= MPD_MAXTOK) {
            msg_Warn(intf, "MPD Protocol: too many tokens");
            break;
        }
        if (pt == NULL) {
            /* No special characters found, tokens spans to the end of the request */
            tokens[nb_tok++] = pt_start;
            break;
        }

        switch (*pt) {
        case ' ':
        case '\t':
            if (in_quotes) {
                pt++;
            } else {
                if (pt == reqbuf) {
                    /* Leading whitespace */
                } else {
                    /* Found the end of a token */
                    *pt++ = '\0';
                    tokens[nb_tok++] = pt_start;
                }
                /* Skip whitespace */
                pt += strspn(pt, " \t");
                if (*pt == '\0')
                    /* Trailing whitespace */
                    goto out;
                pt_start = pt;
            }
            break;

        case '\n':
            /* Should be the end of the request */
            if (in_quotes)
                msg_Warn(intf, "MPD Protocol: found newline in quotes");
            *pt++ = '\0';
            tokens[nb_tok++] = pt_start;
            goto out;

        case '"':
            in_quotes = !in_quotes;
            /* Not really efficient */
            memmove(pt, pt + 1, strlen(pt));
            break;

        case '\\':
            if (!in_quotes) {
                msg_Warn(intf, "MPD Protocol: escaping while not in quotes");
            }

            /* Not really efficient */
            memmove(pt, pt + 1, strlen(pt));
            pt++;
            break;

        default:
            assert(!"not supposed to end up here");
        }
    }
out:

    tokens[nb_tok] = NULL;
}

#ifdef PROTOCOL_TEST
void test_tokenize_request(void) {
    char req[] = "find \"(Artist == \\\"foo\\\\'bar\\\\\\\"\\\")\"";
    char *tokens[MPD_MAXTOK + 1] = {NULL};
    mpd_tokenize_request(NULL, req, tokens);
    assert(tokens[0] != NULL);
    assert(strcmp(tokens[0], "find") == 0);
    assert(tokens[1] != NULL);
    assert(strcmp(tokens[1], "(Artist == \"foo\\'bar\\\"\")") == 0);
    assert(tokens[2] == NULL);
}
#endif

bool mpd_parse_bool(const char *str, bool *val) {
    char *end;
    long x = strtol(str, &end, 2);
    if (end == str || *end != '\0' || (x != 0 && x != 1))
        return false;

    *val = !!x;
    return true;
}

bool mpd_parse_int(const char *str, int64_t *val) {
    char *end;
    intmax_t x = strtoimax(str, &end, 10);
    if (end == str || *end != '\0')
        return false;
    if (x < INT64_MIN || INT64_MAX < x)
        return false;

    *val = (int64_t)x;
    return true;
}

bool mpd_parse_uint(const char *str, uint64_t *val) {
    char *end;
    uintmax_t x = strtoumax(str, &end, 10);
    if (end == str || *end != '\0')
        return false;
    if (UINT64_MAX < x)
        return false;

    *val = (uint64_t)x;
    return true;
}

bool mpd_parse_float(const char *str, double *val) {
    char *end;
    double x = vlc_strtod_c(str, &end);
    if (end == str || *end != '\0')
        return false;
    if (!isfinite(x))
        return false;

    *val = x;
    return true;
}

bool mpd_parse_range(const char *str, int64_t *from, int64_t *to) {
    char *end;
    intmax_t a = strtoimax(str, &end, 10);
    if (end == str || *end != ':')
        return false;
    if (a < INT64_MIN || INT64_MAX < a)
        return false;

    str = end + 1;
    if (*str != '\0') {
        intmax_t b = strtoimax(str, &end, 10);
        if (*end != '\0')
            return false;
        if (b < a || b < INT64_MIN || INT64_MAX < b)
            return false;
        *to = (int64_t)b;
    } else {
        *to = INT64_MAX;
    }
    *from = (int64_t)a;

    return true;
}

bool mpd_parse_urange(const char *str, uint64_t *from, uint64_t *to) {
    char *end;
    uintmax_t a = strtoumax(str, &end, 10);
    if (end == str || *end != ':')
        return false;
    if (UINT64_MAX < a)
        return false;

    str = end + 1;
    if (*str != '\0') {
        uintmax_t b = strtoumax(str, &end, 10);
        if (*end != '\0')
            return false;
        if (b < a || UINT64_MAX < b)
            return false;
        *to = (uint64_t)b;
    } else {
        *to = UINT64_MAX;
    }
    *from = (uint64_t)a;

    return true;
}

bool mpd_parse_tag(const char *str, int *tag) {
    for (int i = 0; i < MPD_TAG_COUNT; i++) {
        if (vlc_ascii_strcasecmp(str, mpd_tag_names[i]) == 0) {
            *tag = i;
            return true;
        }
    }

    return false;
}

#ifdef PROTOCOL_TEST
int main(void) {
    test_tokenize_request();

    return 0;
}
#endif
