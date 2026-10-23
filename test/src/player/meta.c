// SPDX-License-Identifier: LGPL-2.1-or-later
/*****************************************************************************
 * meta.c: live in-band (ICY) now-playing propagation player test
 *****************************************************************************
 * Copyright (C) 2018-2025 VLC authors and VideoLAN
 *****************************************************************************/

#include "common.h"

#define INITIAL_NOW_PLAYING "First_Artist-First_Track"
#define UPDATED_NOW_PLAYING "Second_Artist-Second_Track"

/* The mockstream access reports its timed now-playing over STREAM_GET_TIMED_META,
 * flipping from INITIAL_NOW_PLAYING to UPDATED_NOW_PLAYING at byte offset
 * now_playing_update_after; the mock demux is forced on top (demux->s != NULL)
 * and surfaces each title via es_out_SetMeta once that offset is read, exactly
 * as es.c does for prefetched ICY metadata. Asserts the player forwards the
 * mid-stream change to the item at the playback position. */
static void
test_live_now_playing(struct ctx *ctx)
{
    test_log("live_now_playing\n");

    char *url;
    int ret = asprintf(&url,
        "mockstream://audio_track_count=1;video_track_count=0;"
        "sub_track_count=0;length=%"PRId64";audio_sample_length=%"PRId64";"
        "now_playing=%s;now_playing_updated=%s;now_playing_update_after=1024;"
        "now_playing_pts=2000",
        VLC_TICK_FROM_MS(4000), VLC_TICK_FROM_MS(100),
        INITIAL_NOW_PLAYING, UPDATED_NOW_PLAYING);
    assert(ret != -1);

    input_item_t *media = input_item_New(url, "live");
    assert(media != NULL);
    free(url);

    /* Force the mock demux on top so demux->s is non-NULL and the core polls. */
    ret = input_item_AddOption(media, ":demux=mock", VLC_INPUT_OPTION_TRUSTED);
    assert(ret == VLC_SUCCESS);

    ret = vlc_player_SetCurrentMedia(ctx->player, media);
    assert(ret == VLC_SUCCESS);
    input_item_Release(media);

    player_start(ctx);

    vec_on_media_meta_changed *vec = &ctx->report.on_media_meta_changed;
    bool got_update = false;
    while (!got_update)
    {
        for (size_t i = 0; i < vec->size; ++i)
        {
            char *np = input_item_GetNowPlaying(vec->data[i]);
            assert(np == NULL || np[0] != '\0'); /* never an empty flicker */
            if (np != NULL && strcmp(np, UPDATED_NOW_PLAYING) == 0)
            {
                /* The updated title takes effect at 2s: it must not surface at
                 * the start (as untimed surfacing would). Allow for the demux
                 * running ahead of the output clock by the pipeline buffering. */
                vlc_tick_t time = vlc_player_GetTime(ctx->player);
                assert(time != VLC_TICK_INVALID);
                assert(time >= VLC_TICK_FROM_MS(2000) - VLC_TICK_FROM_MS(1000));
                got_update = true;
            }
            free(np);
        }
        if (!got_update)
            vlc_player_CondWait(ctx->player, &ctx->wait);
    }

    input_item_t *current = vlc_player_GetCurrentMedia(ctx->player);
    assert(current != NULL);
    char *np = input_item_GetNowPlaying(current);
    assert(np != NULL && strcmp(np, UPDATED_NOW_PLAYING) == 0);
    free(np);

    vlc_player_Stop(ctx->player);
    wait_state(ctx, VLC_PLAYER_STATE_STOPPED);
}

/* Without a pts (server sends no bitrate), the title has no known playback
 * position, so the core falls back to surfacing it on change. Asserts it still
 * propagates. */
static void
test_live_now_playing_untimed(struct ctx *ctx)
{
    test_log("live_now_playing_untimed\n");

    char *url;
    int ret = asprintf(&url,
        "mockstream://audio_track_count=1;video_track_count=0;"
        "sub_track_count=0;length=%"PRId64";audio_sample_length=%"PRId64";"
        "now_playing=%s;now_playing_updated=%s;now_playing_update_after=1024",
        VLC_TICK_FROM_MS(4000), VLC_TICK_FROM_MS(100),
        INITIAL_NOW_PLAYING, UPDATED_NOW_PLAYING);
    assert(ret != -1);

    input_item_t *media = input_item_New(url, "live");
    assert(media != NULL);
    free(url);

    ret = input_item_AddOption(media, ":demux=mock", VLC_INPUT_OPTION_TRUSTED);
    assert(ret == VLC_SUCCESS);

    ret = vlc_player_SetCurrentMedia(ctx->player, media);
    assert(ret == VLC_SUCCESS);
    input_item_Release(media);

    player_start(ctx);

    vec_on_media_meta_changed *vec = &ctx->report.on_media_meta_changed;
    bool got_update = false;
    while (!got_update)
    {
        for (size_t i = 0; i < vec->size; ++i)
        {
            char *np = input_item_GetNowPlaying(vec->data[i]);
            assert(np == NULL || np[0] != '\0');
            if (np != NULL && strcmp(np, UPDATED_NOW_PLAYING) == 0)
                got_update = true;
            free(np);
        }
        if (!got_update)
            vlc_player_CondWait(ctx->player, &ctx->wait);
    }

    vlc_player_Stop(ctx->player);
    wait_state(ctx, VLC_PLAYER_STATE_STOPPED);
}

/* An empty in-band title (StreamTitle='') is reported as-is: it clears the
 * now-playing so the shown value keeps matching the stream. */
static void
test_live_now_playing_empty(struct ctx *ctx)
{
    test_log("live_now_playing_empty\n");

    char *url;
    int ret = asprintf(&url,
        "mockstream://audio_track_count=1;video_track_count=0;"
        "sub_track_count=0;length=%"PRId64";audio_sample_length=%"PRId64";"
        "now_playing=%s;now_playing_updated=%s;now_playing_update_after=1024;"
        "now_playing_update_empty=1;now_playing_pts=2000",
        VLC_TICK_FROM_MS(4000), VLC_TICK_FROM_MS(100),
        INITIAL_NOW_PLAYING, INITIAL_NOW_PLAYING);
    assert(ret != -1);

    input_item_t *media = input_item_New(url, "live");
    assert(media != NULL);
    free(url);

    ret = input_item_AddOption(media, ":demux=mock", VLC_INPUT_OPTION_TRUSTED);
    assert(ret == VLC_SUCCESS);

    ret = vlc_player_SetCurrentMedia(ctx->player, media);
    assert(ret == VLC_SUCCESS);
    input_item_Release(media);

    player_start(ctx);

    /* The empty StreamTitle is reported (not dropped): timed to 2s, it surfaces
     * as an empty now-playing. Under the previous "ignore empty" behaviour it
     * would never surface and this would time out. */
    vec_on_media_meta_changed *vec = &ctx->report.on_media_meta_changed;
    bool got_empty = false;
    while (!got_empty)
    {
        for (size_t i = 0; i < vec->size; ++i)
        {
            char *np = input_item_GetNowPlaying(vec->data[i]);
            if (np != NULL && np[0] == '\0')
                got_empty = true;
            free(np);
        }
        if (!got_empty)
            vlc_player_CondWait(ctx->player, &ctx->wait);
    }

    vlc_player_Stop(ctx->player);
    wait_state(ctx, VLC_PLAYER_STATE_STOPPED);
}

int
main(void)
{
    struct ctx ctx;
    ctx_init(&ctx, DISABLE_VIDEO_OUTPUT);
    test_live_now_playing(&ctx);
    ctx_destroy(&ctx);

    ctx_init(&ctx, DISABLE_VIDEO_OUTPUT);
    test_live_now_playing_untimed(&ctx);
    ctx_destroy(&ctx);

    ctx_init(&ctx, DISABLE_VIDEO_OUTPUT);
    test_live_now_playing_empty(&ctx);
    ctx_destroy(&ctx);
    return 0;
}
