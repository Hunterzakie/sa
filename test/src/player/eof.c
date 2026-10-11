// SPDX-License-Identifier: LGPL-2.1-or-later
/*****************************************************************************
 * eof.c: EOF player test
 *****************************************************************************
 * Copyright (C) 2018-2025 VLC authors and VideoLAN
 *****************************************************************************/

#include "common.h"

static void
test_play_pause(struct ctx *ctx, bool from_option)
{
    test_log("play_pause%s\n", from_option ? " (via option)" : "");

    vlc_player_t *player = ctx->player;

    static const char *media_options[] = {
        "play-and-pause",
    };

    struct media_params params = DEFAULT_MEDIA_PARAMS(VLC_TICK_FROM_MS(100));
    if (from_option)
    {
        params.options_count = ARRAY_SIZE(media_options);
        params.options = media_options;
        /* Ensure item options has higher priority than player option */
        vlc_player_SetPlayAndPause(player, false);
    }
    else
        vlc_player_SetPlayAndPause(player, true);
    player_set_next_mock_media(ctx, "media1", &params);
    player_start(ctx);

    /* Check the PlayAndPause if pausing at EOF */
    wait_state(ctx, VLC_PLAYER_STATE_PLAYING);
    wait_state(ctx, VLC_PLAYER_STATE_PAUSED);

    vlc_player_SetTime(player, 0);
    vlc_player_Resume(player);

    /* Check we can resume the playback and pause again */
    wait_state(ctx, VLC_PLAYER_STATE_PLAYING);
    wait_state(ctx, VLC_PLAYER_STATE_PAUSED);

    /* Check we stay paused */
    vlc_tick_sleep(VLC_TICK_FROM_MS(100));
    assert_state(ctx, VLC_PLAYER_STATE_PAUSED);
    test_prestop(ctx);

    vlc_player_Stop(player);

    test_end(ctx);
}

static void
test_repeat(struct ctx *ctx, bool from_option)
{
    test_log("repeat%s\n", from_option ? " (via option)" : "");

    vlc_player_t *player = ctx->player;
    #define REPEAT_COUNT 3
    #define STRINGIFY_(x) #x
    #define STRINGIFY(x) STRINGIFY_(x)

    static const char *media_options[] = {
        "input-repeat="STRINGIFY(REPEAT_COUNT),
    };

    struct media_params params = DEFAULT_MEDIA_PARAMS(VLC_TICK_FROM_MS(100));
    if (from_option)
    {
        params.options_count = ARRAY_SIZE(media_options);
        params.options = media_options;
        /* Ensure item options has higher priority than player option */
        vlc_player_SetRepeatCount(player, 99);
    }
    else
        vlc_player_SetRepeatCount(player, REPEAT_COUNT);
    player_set_next_mock_media(ctx, "media1", &params);
    player_start(ctx);

    wait_state(ctx, VLC_PLAYER_STATE_PLAYING);
    wait_state(ctx, VLC_PLAYER_STATE_STOPPED);

    /* Check buffering count match the repeat count */
    assert(get_buffering_count(ctx) ==  REPEAT_COUNT + 1 /* initial buffering */);

    test_end(ctx);
}

int
main(void)
{
    struct ctx ctx;
    ctx_init(&ctx, 0);
    test_play_pause(&ctx, false);
    test_play_pause(&ctx, true);
    test_repeat(&ctx, false);
    test_repeat(&ctx, true);
    ctx_destroy(&ctx);
    return 0;
}
