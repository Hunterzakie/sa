// SPDX-License-Identifier: LGPL-2.1-or-later
/*****************************************************************************
 * run.c: Functions run whisper's functions and create subtitle
 *****************************************************************************
 * Copyright © 2025 Videolabs, VideoLAN and VLC authors
 *
 * Authors: Gabriel Lafond Thenaille <gabriel@videolabs.io>
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <assert.h>
#include <stdatomic.h>

#include <whisper.h>

#include <vlc_common.h>
#include <vlc_objects.h>
#include <vlc_frame.h>
#include <vlc_codec.h>
#include <vlc_vector.h>

#include "whisper_internal.h"

static void
stt_whisper_run_ProcessSegments(struct stt_whisper_backend *backend, int n_new)
{
    assert(backend != NULL);
    assert(backend->type == stt_whisper_backend_thread);
    assert(backend->sys != NULL);
    struct stt_whisper_backend_thread *sys = backend->sys;

    int n_segments = whisper_full_n_segments(sys->whisper_ctx);
    int s0 = n_segments - n_new;

    for (int sn = s0; sn < n_segments; sn++) {
        int ntokens = whisper_full_n_tokens(sys->whisper_ctx, sn);
        if (ntokens <= 0) {
            continue;
        }

        const char *s = whisper_full_get_segment_text(sys->whisper_ctx, sn);
        size_t size = strlen(s);

        vlc_frame_t *sub = vlc_frame_Alloc(size + 1);
        if (sub == NULL) {
            continue;
        }
        assert(sub->i_buffer == size + 1);
        memset(sub->p_buffer, 0, sub->i_buffer);
        memcpy(sub->p_buffer, s, size);
        vlc_tick_t t0 = whisper_full_get_segment_t0(sys->whisper_ctx, sn);
        vlc_tick_t t1 = whisper_full_get_segment_t1(sys->whisper_ctx, sn);

        /* The whisper time is expressed in 1/100 of seconds.
         * We need to convert it to ms before using VLC_TICK_FROM_MS */
        sub->i_pts = VLC_TICK_FROM_MS(t0 * 10) + sys->pts;
        sub->i_length = VLC_TICK_FROM_MS((t1 - t0) * 10);

        int t_index = sys->params.translation_index;
        vlc_fifo_Unlock(sys->fifo);
        if (t_index > 0) {
            int lang_id = whisper_full_lang_id(sys->whisper_ctx);
            const char *lang_in = whisper_lang_str(lang_id);
            const char *lang_out = stt_whisper_translate_GetLangCode(t_index);
            if (lang_in && lang_out && strcmp(lang_in, lang_out)) {
                sub = stt_whisper_translate_Run(backend, sub);
            }
        }
        assert(backend->owner.cbs != NULL);
        assert(backend->owner.cbs->send_subtitle != NULL);
        backend->owner.cbs->send_subtitle(backend->owner.opaque, sub);
        vlc_fifo_Lock(sys->fifo);
    }
}

static void
stt_whisper_run_CallbackNewSegment(struct whisper_context *wctx,
                                   struct whisper_state *state, int n_new,
                                   void *userdata)
{
    VLC_UNUSED(wctx);
    VLC_UNUSED(state);

    assert(userdata != NULL);
    struct stt_whisper_backend *backend = userdata;
    assert(backend->type == stt_whisper_backend_thread);
    assert(backend->sys != NULL);
    struct stt_whisper_backend_thread *sys = backend->sys;

    if (atomic_load(&sys->abort)) {
        return;
    }

    vlc_fifo_Lock(sys->fifo);
    stt_whisper_run_ProcessSegments(backend, n_new);
    vlc_fifo_Unlock(sys->fifo);
}

static bool
stt_whisper_run_CallbackEncoderBegin(struct whisper_context *wctx,
                                     struct whisper_state *state,
                                     void *userdata)
{
    assert(userdata != NULL);
    struct stt_whisper_backend *backend = userdata;
    assert(backend->type == stt_whisper_backend_thread);
    assert(backend->sys != NULL);
    struct stt_whisper_backend_thread *sys = backend->sys;

    VLC_UNUSED(wctx);
    VLC_UNUSED(state);

    return !atomic_load(&sys->abort);
}

static bool
stt_whisper_run_CallbackAbort(void *userdata)
{
    assert(userdata != NULL);
    struct stt_whisper_backend *backend = userdata;
    assert(backend->type == stt_whisper_backend_thread);
    assert(backend->sys != NULL);
    struct stt_whisper_backend_thread *sys = backend->sys;

    return atomic_load(&sys->abort);
}

static void
stt_whisper_run_WhisperFull(struct stt_whisper_backend *backend,
                            const float *buf, size_t nsamples,
                            struct whisper_vad_params vad_params)
{
    assert(backend != NULL);
    assert(backend->obj != NULL);
    assert(backend->type == stt_whisper_backend_thread);
    assert(backend->sys != NULL);
    struct stt_whisper_backend_thread *sys = backend->sys;
    assert(buf != NULL);

    if (nsamples == 0) {
        return;
    }

    const struct stt_whisper_params *params = &sys->params;
    struct whisper_full_params wparams;
    wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);

    vlc_fifo_Lock(sys->fifo);
    char *language = params->language ? strdup(params->language) : NULL;
    vlc_fifo_Unlock(sys->fifo);

    wparams.n_threads = params->n_threads;
    wparams.language = language;
    wparams.translate = false;

    wparams.single_segment = false;

    wparams.new_segment_callback = stt_whisper_run_CallbackNewSegment;
    wparams.new_segment_callback_user_data = backend;
    wparams.abort_callback = stt_whisper_run_CallbackAbort;
    wparams.abort_callback_user_data = backend;
    wparams.encoder_begin_callback = stt_whisper_run_CallbackEncoderBegin;
    wparams.encoder_begin_callback_user_data = backend;

    wparams.print_progress = false;
    wparams.suppress_blank = true;
    wparams.suppress_nst = false;

    wparams.token_timestamps = true;

    wparams.max_len = STT_WHISPER_NCHAR_MAX;
    wparams.split_on_word = true;

    wparams.temperature = 0;
    wparams.greedy.best_of = 5;
    wparams.beam_search.beam_size = 5;
    wparams.tdrz_enable = true;
    wparams.duration_ms = nsamples / STT_WHISPER_SAMPLE_PER_MS;

    wparams.vad = true;
    wparams.vad_params = vad_params;


    int ret = whisper_full(sys->whisper_ctx, wparams, buf, nsamples);
    free(language);
    if (ret) {
        msg_Err(backend->obj, "whisper_full failed!");
    }
}

static void
stt_whisper_run_ProcessFrame(struct stt_whisper_backend *backend,
                             vlc_frame_t *frame)
{
    assert(backend != NULL);
    assert(backend->obj != NULL);
    assert(backend->type == stt_whisper_backend_thread);
    assert(backend->sys != NULL);
    struct stt_whisper_backend_thread *sys = backend->sys;
    assert(frame != NULL);

    const struct stt_whisper_params *params = &sys->params;

    bool end = true;
    size_t size = 0;
    vlc_tick_t length = 0;
    if (MS_FROM_VLC_TICK(frame->i_length) > params->length_ms) {
        length = VLC_TICK_FROM_MS(params->length_ms);
        size = params->length_ms * STT_WHISPER_BYTE_PER_MS;
        /* i_length and i_buffer can disagree, e.g. after a discontinuity. */
        if (size > frame->i_buffer) {
            size = frame->i_buffer;
        }
    } else {
        length = frame->i_length;
        size = frame->i_buffer;
    }
    const float *buf = (const float *)frame->p_buffer;
    size_t nsamples = size / sizeof(float);

    struct whisper_vad_params vparams = whisper_vad_default_params();
    vparams.threshold = 0.2;

    sys->pts = frame->i_pts;

    struct whisper_vad_segments *vad_segs = NULL;
    if (sys->vad_ctx != NULL) {
        vad_segs = whisper_vad_segments_from_samples(sys->vad_ctx, vparams,
                                                     buf, nsamples);
    }
    if (vad_segs != NULL) {
        const int nsegs = whisper_vad_segments_n_segments(vad_segs);
        if (nsegs == 0) {
            whisper_vad_free_segments(vad_segs);
            msg_Dbg(backend->obj, "whisper vad: No speech!");
            goto end;
        }
        float last_t1_ms = whisper_vad_segments_get_segment_t1(vad_segs,
                                                            nsegs - 1) * 10;
        if (nsegs > 1 && last_t1_ms > MS_FROM_VLC_TICK(length)) {
            const float t1_ms = whisper_vad_segments_get_segment_t1(vad_segs,
                                                            nsegs - 2) * 10;
            if (t1_ms > MS_FROM_VLC_TICK(length) - 10000) {
                last_t1_ms = t1_ms;
                end = false;
            }
        }
        whisper_vad_free_segments(vad_segs);

        length = VLC_TICK_FROM_MS(last_t1_ms);
        size = (size_t)last_t1_ms * STT_WHISPER_BYTE_PER_MS;
        size_t end_ns = (size_t)last_t1_ms * STT_WHISPER_SAMPLE_PER_MS;
        /* The VAD pads the last segment by speech_pad_ms, so its end can
         * land past the audio that was submitted. */
        if (end_ns > nsamples) {
            msg_Dbg(backend->obj, "whisper vad: clamping %zu to %zu samples",
                    end_ns, nsamples);
            end_ns = nsamples;
        }
        stt_whisper_run_WhisperFull(backend, buf, end_ns, vparams);
    } else {
        stt_whisper_run_WhisperFull(backend, buf, nsamples, vparams);
    }

end:
    if (frame->i_buffer < size) {
        size = frame->i_buffer;
    }
    frame->p_buffer += size;
    frame->i_buffer -= size;

    if (frame->i_length < length) {
        length = frame->i_length;
    }
    frame->i_pts += length;
    frame->i_length -= length;

    if (end) {
        if (size > STT_WHISPER_KEEP_BYTES) {
            frame->p_buffer -= STT_WHISPER_KEEP_BYTES;
            frame->i_buffer += STT_WHISPER_KEEP_BYTES;
        }
        if (length > STT_WHISPER_KEEP_TICK) {
            frame->i_pts -= STT_WHISPER_KEEP_TICK;
            frame->i_length += STT_WHISPER_KEEP_TICK;
        }
    }
}

static vlc_frame_t *
stt_whisper_run_GetNextFrame(struct stt_whisper_backend_thread *sys)
{
    assert(sys != NULL);
    assert(sys->fifo != NULL);

    vlc_frame_t *frame = vlc_fifo_DequeueAllUnlocked(sys->fifo);
    if (frame == NULL) {
        return NULL;
    }
    vlc_frame_t *ret_frame = vlc_frame_ChainGather(frame);
    if (ret_frame == NULL) {
        vlc_frame_ChainRelease(frame);
    }
    return ret_frame;
}

void
stt_whisper_run_ProcessLoop(struct stt_whisper_backend *backend)
{
    assert(backend != NULL);
    assert(backend->type == stt_whisper_backend_thread);
    assert(backend->sys != NULL);
    struct stt_whisper_backend_thread *sys = backend->sys;
    assert(sys->fifo != NULL);
    vlc_fifo_t *fifo = sys->fifo;

    vlc_frame_t *frame = NULL;
    vlc_fifo_Lock(fifo);
    while (true) {
        if (sys->waiting && !sys->stop) {
            vlc_fifo_Wait(fifo);
            continue;
        }
        size_t length_bytes = sys->params.length_ms * STT_WHISPER_BYTE_PER_MS;
        if (!sys->drain) {
            if (sys->stop) {
                break;
            }
            if (atomic_load(&sys->reload_needed)) {
                vlc_fifo_Unlock(fifo);
                atomic_store(&sys->reload_needed, false);
                stt_whisper_Load(backend);
                vlc_fifo_Lock(fifo);
                continue;
            }
            if (vlc_fifo_GetBytes(fifo) < length_bytes) {
                vlc_fifo_Wait(fifo);
                continue;
            }
        }
        frame = stt_whisper_run_GetNextFrame(sys);
        vlc_fifo_Unlock(fifo);
        if (frame == NULL) {
            vlc_fifo_Lock(fifo);
            sys->drain = false;
            continue;
        }
        atomic_store(&sys->abort, false);
        stt_whisper_run_ProcessFrame(backend, frame);
        vlc_fifo_Lock(fifo);
        if (atomic_load(&sys->abort) || frame->i_buffer == 0) {
            vlc_frame_Release(frame);
        } else {
            /* Push back the remaining part of the just processed frame to
             * the begining of the FIFO */
            vlc_frame_t *frame_all = vlc_fifo_DequeueAllUnlocked(sys->fifo);
            vlc_frame_ChainAppend(&frame, frame_all);
            vlc_fifo_QueueUnlocked(sys->fifo, frame);
        }
        frame = NULL;
    }
    vlc_fifo_Unlock(fifo);
}
