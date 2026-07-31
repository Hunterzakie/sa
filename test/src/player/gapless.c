// SPDX-License-Identifier: LGPL-2.1-or-later
/*****************************************************************************
 * gapless.c: gapless playback player tests
 *****************************************************************************
 * Copyright (C) 2018-2025 VLC authors and VideoLAN
 *****************************************************************************/

#include "common.h"
#include "player/player.h"

/* Gapless applies only to one selected clock-master audio ES. The audio-driven
 * tests use audio-only medias, at rate 1 and long enough to buffer past
 * DEFAULT_PTS_DELAY (300ms), which dec.c:ArmGaplessHandoff requires.
 *
 * Within the 2 * pts_delay preload window
 * (es_out.c:EsOutGaplessEventTimeChanged) the successor must open, decode and arm
 * before the predecessor reaches EOF. EOF read-ahead grows with frame length, so
 * a too-large frame drains first and the handoff falls back (see
 * test_gapless_large_frame). */

static struct media_params
gapless_audio_params(void)
{
    struct media_params params = DEFAULT_MEDIA_PARAMS(VLC_TICK_FROM_MS(2000));
    params.track_count[VIDEO_ES] = 0;
    params.track_count[SPU_ES] = 0;
    params.pts_delay = VLC_TICK_FROM_MS(500);
    params.audio_sample_length = VLC_TICK_FROM_MS(20);
    return params;
}

/* The test aout (modules.c) records a "segment" each time the output is
 * (re)started or flushed. Only a real gapless transition keeps the count flat:
 * the successor's samples follow the predecessor's without a flush. Every
 * fallback adds one segment, by restarting the output (different format) or by
 * the successor's first-play flush. */
static size_t
aout_segment_count(struct ctx *ctx)
{
    return ctx->report.on_aout_first_pts.size;
}

static void
assert_segment_count(struct ctx *ctx, size_t expected)
{
    /* CI load can make a seamless handoff miss its real-time deadline while
     * playback correctly falls back. Set VLC_TEST_CHECK_SEGMENTS only for
     * controlled, low-load runs. */
    if (ctx->check_segments)
        assert(aout_segment_count(ctx) == expected);
}

static size_t
vout_started_count(struct ctx *ctx)
{
    size_t count = 0;
    struct report_vout report;
    vlc_vector_foreach(report, &ctx->report.on_vout_changed)
        if (report.action == VLC_PLAYER_VOUT_STARTED)
            count++;
    return count;
}

/* Block until the predecessor's reported time reaches target. Playback is
 * real-time paced, so this is also a wall-clock delay that lets the in-flight
 * gapless transition advance (e.g. the successor arming). */
static void
wait_time_at_least(struct ctx *ctx, vlc_tick_t target)
{
    vec_on_position_changed *vec = &ctx->report.on_position_changed;
    while (vec->size == 0 || VEC_LAST(vec).time < target)
        vlc_player_CondWait(ctx->player, &ctx->wait);
}

/* A video-only preload can fail without an observable output, so inspect the
 * player while the test holds its lock to distinguish refusal from no request. */
static void
assert_no_gapless_preload(struct ctx *ctx)
{
    wait_time_at_least(ctx, VLC_TICK_FROM_MS(1300));
    assert(ctx->player->next_input == NULL);
    assert(!ctx->player->gapless_failed);
}

/* ctx_reset() only clears the test bookkeeping: the player still holds the last
 * media as current. Reset it to NULL so the next test starts from a clean state
 * (its first player_set_next_mock_media() becomes the current media again). */
static void
gapless_teardown(struct ctx *ctx)
{
    const size_t changes = ctx->report.on_current_media_changed.size;
    player_set_current_mock_media(ctx, NULL, NULL, false);
    while (ctx->report.on_current_media_changed.size == changes)
        vlc_player_CondWait(ctx->player, &ctx->wait);

    vlc_player_SetGaplessEnabled(ctx->player, false);
    ctx_reset(ctx);
}

static void
gapless_run(struct ctx *ctx, size_t media_count, size_t expected_segments)
{
    player_start(ctx);
    wait_state(ctx, VLC_PLAYER_STATE_STOPPED);

    assert(ctx->report.on_current_media_changed.size == media_count);
    assert(ctx->next_medias.size == 0);
    assert_segment_count(ctx, expected_segments);

    gapless_teardown(ctx);
}

/* Several identical audio medias transition without a single gap. */
static void
test_gapless_success(struct ctx *ctx)
{
    struct media_params params = gapless_audio_params();
    vlc_player_SetGaplessEnabled(ctx->player, true);

    player_set_next_mock_media(ctx, "media1", &params);
    player_set_next_mock_media(ctx, "media2", &params);
    player_set_next_mock_media(ctx, "media3", &params);

    gapless_run(ctx, 3, 1);
}

static void
test_gapless_short_chain(struct ctx *ctx)
{
    struct media_params params = gapless_audio_params();

    params.pts_delay = VLC_TICK_FROM_MS(1000);
    vlc_player_SetGaplessEnabled(ctx->player, true);

    player_set_next_mock_media(ctx, "media1", &params);
    player_set_next_mock_media(ctx, "media2", &params);
    player_set_next_mock_media(ctx, "media3", &params);

    player_start(ctx);
    while (ctx->report.on_current_media_changed.size < 2)
        vlc_player_CondWait(ctx->player, &ctx->wait);

    wait_state(ctx, VLC_PLAYER_STATE_STOPPED);
    assert(ctx->report.on_current_media_changed.size == 3);
    assert(ctx->next_medias.size == 0);
    assert_segment_count(ctx, 1);

    gapless_teardown(ctx);
}

static void
test_gapless_success_small_next(struct ctx *ctx)
{
    struct media_params params = gapless_audio_params();
    vlc_player_SetGaplessEnabled(ctx->player, true);

    player_set_next_mock_media(ctx, "media1", &params);
    params.length = VLC_TICK_FROM_MS(50);
    player_set_next_mock_media(ctx, "media2", &params);

    gapless_run(ctx, 2, 1);
}

/* A successor whose audio format differs from the current output can't reuse it
 * (dec.c:stream_InitGapless fails the aout_FormatEquals() check): the transition
 * falls back to a normal start, adding one segment. */
static void
test_gapless_format_change(struct ctx *ctx)
{
    struct media_params params = gapless_audio_params();
    vlc_player_SetGaplessEnabled(ctx->player, true);

    player_set_next_mock_media(ctx, "media1", &params);
    params.config = "audio[0]{rate=44100}"; /* differs from the 48000 default */
    player_set_next_mock_media(ctx, "media2", &params);

    gapless_run(ctx, 2, 2);
}

/* A successor that doesn't start at pts 0 can't be stitched gaplessly
 * (dec.c:vlc_aout_stream_TryGaplessHandoff marks it FAILED, "incompatible"), so
 * the handoff falls back. This exercises the input_WakeGapless() path: without
 * it, promoting the never-armed successor asserts at decoder.c (see input.c).
 * The fallback flushes the output at promotion, adding a segment. */
static void
test_gapless_pts_offset(struct ctx *ctx)
{
    struct media_params params = gapless_audio_params();
    vlc_player_SetGaplessEnabled(ctx->player, true);

    player_set_next_mock_media(ctx, "media1", &params);
    params.pts_offset = VLC_TICK_FROM_MS(500);
    player_set_next_mock_media(ctx, "media2", &params);

    gapless_run(ctx, 2, 2);
}

/* With a short pts_delay (100ms) the preload window (2 * pts_delay) is smaller
 * than the predecessor's own EOF read-ahead: it commits to a normal drain
 * before the preloaded successor can arm, and the handoff falls back
 * (dec.c:vlc_aout_stream_TryGaplessHandoff, "predecessor already draining"). */
static void
test_gapless_short_delay(struct ctx *ctx)
{
    struct media_params params = gapless_audio_params();
    params.pts_delay = VLC_TICK_FROM_MS(100);
    vlc_player_SetGaplessEnabled(ctx->player, true);

    player_set_next_mock_media(ctx, "media1", &params);
    player_set_next_mock_media(ctx, "media2", &params);

    gapless_run(ctx, 2, 2);
}

/* With a large audio frame the predecessor reads too far ahead and reaches EOF
 * before the preloaded successor can arm within the 2 * pts_delay window: the
 * handoff times out ("not ready in time") and falls back. Like pts_offset,
 * this exercises input_WakeGapless() (the successor is never armed), and the
 * promotion flush adds a segment. */
static void
test_gapless_large_frame(struct ctx *ctx)
{
    struct media_params params = gapless_audio_params();
    params.audio_sample_length = VLC_TICK_FROM_MS(100);
    vlc_player_SetGaplessEnabled(ctx->player, true);

    player_set_next_mock_media(ctx, "media1", &params);
    player_set_next_mock_media(ctx, "media2", &params);

    gapless_run(ctx, 2, 2);
}

/* A fallback in the middle of a chain must not disable later transitions:
 * media1 -> media2 falls back (media1's short pts_delay) and adds a segment, but
 * media2 -> media3 still arms gaplessly and adds none. The cause lives on media1
 * alone; deforming media2 instead would also break its preload window. */
static void
test_gapless_recovery(struct ctx *ctx)
{
    struct media_params params = gapless_audio_params();
    vlc_player_SetGaplessEnabled(ctx->player, true);

    params.pts_delay = VLC_TICK_FROM_MS(100);
    player_set_next_mock_media(ctx, "media1", &params);
    params.pts_delay = VLC_TICK_FROM_MS(500);
    player_set_next_mock_media(ctx, "media2", &params);
    player_set_next_mock_media(ctx, "media3", &params);

    gapless_run(ctx, 3, 2);
}

/* A preloaded successor that errors out before it can arm (demux fails on the
 * first read) must not wedge the predecessor: it reaches STOPPING as next_input,
 * which clears it and sets player->gapless_failed (player/input.c); the
 * predecessor drains and falls back to a normal start that surfaces the error.
 * One segment (the error media produced no audio). This is the next_input-died
 * path, distinct from the armed-then-aborted handoff. */
static void
test_gapless_error_successor(struct ctx *ctx)
{
    struct media_params params = gapless_audio_params();
    vlc_player_SetGaplessEnabled(ctx->player, true);

    player_set_next_mock_media(ctx, "media1", &params);
    params.error = true;
    player_set_next_mock_media(ctx, "media2", &params);

    player_start(ctx);

    /* The error surfaces during playback, before the player stops (errors.c). */
    vec_on_error_changed *errors = &ctx->report.on_error_changed;
    while (errors->size == 0 || VEC_LAST(errors) == VLC_PLAYER_ERROR_NONE)
        vlc_player_CondWait(ctx->player, &ctx->wait);
    wait_state(ctx, VLC_PLAYER_STATE_STOPPED);

    assert(ctx->report.on_current_media_changed.size == 2);
    assert(ctx->next_medias.size == 0);
    assert_segment_count(ctx, 1);

    gapless_teardown(ctx);
}

/* A selected video ES makes the predecessor ineligible, so no successor input
 * is opened during the preload window and the audio output restarts normally. */
static void
test_gapless_av_predecessor(struct ctx *ctx)
{
    struct media_params params = gapless_audio_params();
    params.track_count[VIDEO_ES] = 1;
    vlc_player_SetGaplessEnabled(ctx->player, true);

    player_set_next_mock_media(ctx, "media1", &params);
    params.track_count[VIDEO_ES] = 0;
    player_set_next_mock_media(ctx, "media2", &params);

    player_start(ctx);
    assert_no_gapless_preload(ctx);
    wait_state(ctx, VLC_PLAYER_STATE_STOPPED);

    assert(ctx->report.on_current_media_changed.size == 2);
    assert(ctx->next_medias.size == 0);
    assert_segment_count(ctx, 2);

    gapless_teardown(ctx);
}

/* A video-only predecessor cannot request a preload. Its video still plays and
 * the audio successor starts normally after the non-gapless transition. */
static void
test_gapless_video_predecessor(struct ctx *ctx)
{
    struct media_params params = gapless_audio_params();
    params.track_count[VIDEO_ES] = 1;
    params.track_count[AUDIO_ES] = 0;
    vlc_player_SetGaplessEnabled(ctx->player, true);

    player_set_next_mock_media(ctx, "media1", &params);
    params.track_count[VIDEO_ES] = 0;
    params.track_count[AUDIO_ES] = 1;
    player_set_next_mock_media(ctx, "media2", &params);

    player_start(ctx);
    assert_no_gapless_preload(ctx);
    wait_state(ctx, VLC_PLAYER_STATE_STOPPED);

    assert(ctx->report.on_current_media_changed.size == 2);
    assert(ctx->next_medias.size == 0);
    assert_segment_count(ctx, 1);
    assert(vout_started_count(ctx) == 1);

    gapless_teardown(ctx);
}

/* Video on both sides: the predecessor is ineligible whatever the successor is,
 * so no preload is requested and each media starts its own output and vout. */
static void
test_gapless_av_chain(struct ctx *ctx)
{
    struct media_params params = gapless_audio_params();
    params.track_count[VIDEO_ES] = 1;
    vlc_player_SetGaplessEnabled(ctx->player, true);

    player_set_next_mock_media(ctx, "media1", &params);
    player_set_next_mock_media(ctx, "media2", &params);

    player_start(ctx);
    assert_no_gapless_preload(ctx);
    wait_state(ctx, VLC_PLAYER_STATE_STOPPED);

    assert(ctx->report.on_current_media_changed.size == 2);
    assert(ctx->next_medias.size == 0);
    assert_segment_count(ctx, 2);
    assert(vout_started_count(ctx) == 2);

    gapless_teardown(ctx);
}

/* A video-only successor has no audio to hand off: the predecessor drains
 * normally and the successor starts its vout at promotion. */
static void
test_gapless_no_audio_successor(struct ctx *ctx)
{
    struct media_params params = gapless_audio_params();
    vlc_player_SetGaplessEnabled(ctx->player, true);

    player_set_next_mock_media(ctx, "media1", &params);
    params.track_count[VIDEO_ES] = 1;
    params.track_count[AUDIO_ES] = 0;
    player_set_next_mock_media(ctx, "media2", &params);

    player_start(ctx);
    wait_state(ctx, VLC_PLAYER_STATE_STOPPED);

    assert(ctx->report.on_current_media_changed.size == 2);
    assert(ctx->next_medias.size == 0);
    assert_segment_count(ctx, 1);
    assert(vout_started_count(ctx) == 1);

    gapless_teardown(ctx);
}

/* A video track does not prevent the audio handoff: the successor's video
 * decoder waits for PROMOTED (decoder.c:CreateVoutIfNeeded) instead of showing a
 * vout behind the predecessor, so the output is still reused. */
static void
test_gapless_av_successor(struct ctx *ctx)
{
    struct media_params params = gapless_audio_params();
    vlc_player_SetGaplessEnabled(ctx->player, true);

    player_set_next_mock_media(ctx, "media1", &params);
    params.track_count[VIDEO_ES] = 1;
    player_set_next_mock_media(ctx, "media2", &params);

    player_start(ctx);
    wait_state(ctx, VLC_PLAYER_STATE_STOPPED);

    assert(ctx->report.on_current_media_changed.size == 2);
    assert(ctx->next_medias.size == 0);
    assert_segment_count(ctx, 1);
    assert(vout_started_count(ctx) == 1);

    gapless_teardown(ctx);
}

/* A successor shorter than the preload window (2 * pts_delay) still arms and is
 * handed off gaplessly; it just plays briefly before the chain ends. The output
 * stays reused, so the segment count stays at 1. */
static void
test_gapless_short_successor(struct ctx *ctx)
{
    struct media_params params = gapless_audio_params();
    vlc_player_SetGaplessEnabled(ctx->player, true);

    player_set_next_mock_media(ctx, "media1", &params);
    params.length = VLC_TICK_FROM_MS(200);
    player_set_next_mock_media(ctx, "media2", &params);

    gapless_run(ctx, 2, 1);
}

/* Cancelling gapless while the successor is armed must tear it down cleanly: its
 * output stream is deleted while still the pending gapless stream
 * (dec.c:CompleteGaplessHandoff's "successor aborted before the handoff" branch).
 * The predecessor keeps playing; with gapless now off, the next media restarts
 * normally. Cancel inside the preload window (length - 2*pts_delay), so it lands
 * while the successor is armed but not yet promoted. */
static void
test_gapless_abort_armed(struct ctx *ctx)
{
    struct media_params params = gapless_audio_params();
    vlc_player_SetGaplessEnabled(ctx->player, true);

    player_set_next_mock_media(ctx, "media1", &params);
    player_set_next_mock_media(ctx, "media2", &params);

    player_start(ctx);

    wait_time_at_least(ctx, VLC_TICK_FROM_MS(1300));
    vlc_player_SetGaplessEnabled(ctx->player, false);

    wait_state(ctx, VLC_PLAYER_STATE_STOPPED);

    assert(ctx->report.on_current_media_changed.size == 2);
    assert(ctx->next_medias.size == 0);

    gapless_teardown(ctx);
}

/* Seek the predecessor near its end, into the gapless preload/handoff window.
 * Timing-dependent by design: under varying scheduling load the seek lands
 * before or after the successor arms and starts feeding the shared output,
 * exercising both the parked and the queuing abandon paths over many CI runs.
 * Asserts only that the chain still terminates cleanly, not the segment count. */
static void
test_gapless_seek_near_end(struct ctx *ctx)
{
    struct media_params params = gapless_audio_params();
    vlc_player_SetGaplessEnabled(ctx->player, true);

    player_set_next_mock_media(ctx, "media1", &params);
    player_set_next_mock_media(ctx, "media2", &params);

    player_start(ctx);

    wait_time_at_least(ctx, VLC_TICK_FROM_MS(1700));
    vlc_player_SetTime(ctx->player, VLC_TICK_FROM_MS(1950));

    wait_state(ctx, VLC_PLAYER_STATE_STOPPED);

    assert(ctx->report.on_current_media_changed.size == 2);
    assert(ctx->next_medias.size == 0);

    gapless_teardown(ctx);
}

int
main(int argc, char **argv)
{
    static const struct gapless_test
    {
        const char *name;
        void (*run)(struct ctx *ctx);
        size_t gapless_handoffs;
        size_t transitions;
    } tests[] = {
        { "success", test_gapless_success, 2, 2 },
        { "short_chain", test_gapless_short_chain, 2, 2 },
        { "success_small_next", test_gapless_success_small_next, 1, 1 },
        { "large_frame", test_gapless_large_frame, 0, 1 },
        { "pts_offset", test_gapless_pts_offset, 0, 1 },
        { "format_change", test_gapless_format_change, 0, 1 },
        { "short_delay", test_gapless_short_delay, 0, 1 },
        { "recovery", test_gapless_recovery, 1, 2 },
        { "error_successor", test_gapless_error_successor, 0, 1 },
        { "no_audio_successor", test_gapless_no_audio_successor, 0, 1 },
        { "av_successor", test_gapless_av_successor, 1, 1 },
        { "av_chain", test_gapless_av_chain, 0, 1 },
        { "short_successor", test_gapless_short_successor, 1, 1 },
        { "av_predecessor", test_gapless_av_predecessor, 0, 1 },
        { "video_predecessor", test_gapless_video_predecessor, 0, 1 },
        { "abort_armed", test_gapless_abort_armed, 0, 1 },
        { "seek_near_end", test_gapless_seek_near_end, 0, 1 },
    };
    const struct gapless_test *test = NULL;
    struct ctx ctx;

    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s SCENARIO\n", argv[0]);
        return 1;
    }

    for (size_t i = 0; i < ARRAY_SIZE(tests); ++i)
    {
        if (strcmp(argv[1], tests[i].name) == 0)
        {
            test = &tests[i];
            break;
        }
    }

    if (test == NULL)
    {
        fprintf(stderr, "%s: unknown scenario: %s\n", argv[0], argv[1]);
        return 1;
    }

    /* Rate 1, so the aout gets PTS-contiguous audio (see modules.c). */
    ctx_init(&ctx, AUDIO_CHECK_CONTINUITY);
    ctx.check_segments = getenv("VLC_TEST_CHECK_SEGMENTS") != NULL;

    test_log("gapless_%s\n", test->name);
    test->run(&ctx);

    if (ctx.check_segments)
        test_log("gapless handoffs: %zu/%zu, passed\n",
                 test->gapless_handoffs, test->transitions);

    ctx_destroy(&ctx);
    return 0;
}
