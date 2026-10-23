// SPDX-License-Identifier: LGPL-2.1-or-later
/*****************************************************************************
 * init.c: Functions Initialized the decoder and start whisper
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
#include <vlc_plugin.h>
#include <vlc_codec.h>
#include <vlc_frame.h>
#include <vlc_aout.h>
#include <vlc_variables.h>
#include <vlc_messages.h>
#include <vlc_stt.h>

#include "../substext.h"
#include "whisper_internal.h"
#include "whisper_translation.h"

static void
stt_whisper_Ready(void *opaque, enum vlc_stt_ctx_status status)
{
    assert(opaque != NULL);
    struct vlc_stt_ctx *ctx = opaque;

    if (status == vlc_stt_ctx_status_error) {
        msg_Err(ctx, "Failed to load whisper model");
    }

    vlc_mutex_lock(&ctx->lock);
    ctx->status = status;
    vlc_mutex_unlock(&ctx->lock);

    vlc_stt_ctx_Loaded(ctx, status);
}

static int
stt_whisper_SendSubtitle(void *opaque, vlc_frame_t *subtitle)
{
    assert(opaque != NULL);
    struct vlc_stt_ctx *ctx = opaque;

    vlc_mutex_lock(&ctx->lock);
    assert(ctx->sys != NULL);
    struct stt_whisper_backend *backend = ctx->sys;

    if (backend->decoder == NULL) {
        vlc_frame_Release(subtitle);
        vlc_mutex_unlock(&ctx->lock);
        return VLC_EGENERIC;
    }
    vlc_tick_t start = subtitle->i_pts;
    vlc_tick_t stop = start + subtitle->i_length;

    char *text = strndup((const char *)subtitle->p_buffer,
                         subtitle->i_buffer);
    vlc_frame_Release(subtitle);
    if (text == NULL) {
        vlc_mutex_unlock(&ctx->lock);
        return VLC_ENOMEM;
    }
    text_segment_t *tseg = text_segment_New(text);
    free(text);
    if (tseg == NULL) {
        vlc_mutex_unlock(&ctx->lock);
        return VLC_ENOMEM;
    }
    subpicture_t *spu = decoder_NewSubpictureText(backend->decoder);
    if (spu == NULL) {
        text_segment_ChainDelete(tseg);
        vlc_mutex_unlock(&ctx->lock);
        return VLC_EGENERIC;
    }

    spu->i_start = start;
    spu->i_stop = stop;
    spu->b_ephemer = true;

    msg_Info(backend->obj,
            "[%02" PRId64 ":%02" PRId64 ".%03" PRId64 " --> "
            "%02" PRId64 ":%02" PRId64 ".%03" PRId64 "] %s",
             (SEC_FROM_VLC_TICK(spu->i_start) / 60) % 60,
             SEC_FROM_VLC_TICK(spu->i_start) % 60,
             MS_FROM_VLC_TICK(spu->i_start) % 1000,
             (SEC_FROM_VLC_TICK(spu->i_stop) / 60) % 60,
             SEC_FROM_VLC_TICK(spu->i_stop) % 60,
             MS_FROM_VLC_TICK(spu->i_stop) % 1000,
             tseg->psz_text);

    subtext_updater_sys_t *spu_sys = spu->updater.sys;
    spu_sys->region.p_segments = tseg;
    spu_sys->region.align = SUBPICTURE_ALIGN_BOTTOM;
    spu_sys->region.b_absolute = false;
    spu_sys->region.inner_align = 0;
    decoder_QueueSub(backend->decoder, spu);
    vlc_mutex_unlock(&ctx->lock);
    return VLC_SUCCESS;
}

static void
stt_whisper_OnProgress(void *opaque, enum vlc_stt_ctx_status status,
                       int64_t position, int64_t total,
                       const char *description)
{
    assert(opaque != NULL);

    struct vlc_stt_ctx *ctx = opaque;
    vlc_mutex_lock(&ctx->lock);
    if (ctx->cancelled || ctx->status == vlc_stt_ctx_status_error) {
        vlc_mutex_unlock(&ctx->lock);
        return;
    }
    ctx->status = status;
    const struct vlc_stt_ctx_callbacks *cbs = ctx->owner.cbs;
    void *data = ctx->owner.data;
    vlc_mutex_unlock(&ctx->lock);

    if (cbs != NULL && cbs->on_progress != NULL) {
        cbs->on_progress(ctx, status, position, total, description, data);
    }
}

static bool
stt_whisper_Confirm(void *opaque, uint64_t download_size, const char *path)
{
    assert(opaque != NULL);
    struct vlc_stt_ctx *ctx = opaque;

    vlc_mutex_lock(&ctx->lock);
    bool cancelled = ctx->cancelled;
    const struct vlc_stt_ctx_callbacks *cbs = ctx->owner.cbs;
    void *data = ctx->owner.data;
    vlc_mutex_unlock(&ctx->lock);

    if (cancelled) {
        return false;
    }
    if (cbs == NULL || cbs->confirm == NULL) {
        return true;
    }
    return cbs->confirm(ctx, download_size, path, data);
}

static bool
stt_whisper_Abort(void *opaque)
{
    assert(opaque != NULL);
    struct vlc_stt_ctx *ctx = opaque;

    vlc_mutex_lock(&ctx->lock);
    bool cancelled = ctx->cancelled;
    const struct vlc_stt_ctx_callbacks *cbs = ctx->owner.cbs;
    void *data = ctx->owner.data;
    vlc_mutex_unlock(&ctx->lock);

    if (cancelled) {
        return true;
    }
    if (cbs == NULL || cbs->abort == NULL) {
        return false;
    }
    return cbs->abort(ctx, data);
}

static void
stt_whisper_InitParams(struct stt_whisper_backend *backend)
{
    assert(backend != NULL);
    assert(backend->obj != NULL);

    struct stt_whisper_params params;
    params.language = var_CreateGetString(backend->obj, STT_WHISPER_CFG_PREFIX "language");
    if (params.language == NULL) {
        return;
    }
    if (strcmp(params.language, "auto") != 0 && whisper_lang_id(params.language) < 0) {
        msg_Warn(backend->obj, "unknown language `%s', falling back to auto",
                 params.language);
        free(params.language);
        params.language = strdup("auto");
        if (params.language == NULL) {
            return;
        }
    }
    params.model_name = var_CreateGetString(backend->obj, STT_WHISPER_CFG_PREFIX "model");
    if (params.model_name == NULL) {
        free(params.language);
        return;
    }
    char *trans_lang = var_CreateGetString(backend->obj, STT_WHISPER_CFG_PREFIX "translation");
    if (trans_lang == NULL) {
        free(params.language);
        free(params.model_name);
        return;
    }
    params.translation_index = stt_whisper_translate_GetIndex(trans_lang);
    if (params.translation_index == -1) {
        if (trans_lang[0] != '\0') {
            msg_Err(backend->obj, "unknown language `%s'! Translation deactivated!",
                    trans_lang);
        }
    }
    free(trans_lang);

    int length_ms = var_CreateGetInteger(backend->obj, STT_WHISPER_CFG_PREFIX "length");
    params.length_ms = length_ms;
    int n_threads = var_CreateGetInteger(backend->obj, STT_WHISPER_CFG_PREFIX "nthread");
    if (1 > n_threads || n_threads > (int)vlc_GetCPUCount()) {
        n_threads = vlc_GetCPUCount();
    }
    params.n_threads = n_threads;

    assert(backend != NULL);
    assert(backend->ops != NULL);
    assert(backend->ops->set_params != NULL);
    int ret = backend->ops->set_params(backend, &params);
    if (ret == VLC_SUCCESS) {
        free(backend->params.language);
        free(backend->params.model_name);
        backend->params = params;
    } else {
        free(params.language);
        free(params.model_name);
    }
}

static void
stt_whisper_CxtClose(struct vlc_stt_ctx *ctx)
{
    assert(ctx != NULL);
    vlc_mutex_lock(&ctx->lock);
    assert(ctx->sys != NULL);
    struct stt_whisper_backend *backend = ctx->sys;

    backend->decoder = NULL;
    ctx->sys = NULL;
    vlc_mutex_unlock(&ctx->lock);

    backend->ops->close(backend);
    free(backend->params.model_name);
    free(backend->params.language);
    free(backend);
}

static void
stt_whisper_CtxAbort(struct vlc_stt_ctx *ctx)
{
    assert(ctx != NULL);
    /* Called with ctx->lock held (see vlc_stt_ctx_operations.abort), which is
     * also the lock protecting ctx->sys. */
    struct stt_whisper_backend *backend = ctx->sys;
    if (backend != NULL && backend->ops != NULL
        && backend->ops->request_abort != NULL) {
        backend->ops->request_abort(backend);
    }
}

static int
stt_whisper_CtxInit(struct vlc_stt_ctx *ctx)
{
    assert(ctx != NULL);

    struct stt_whisper_backend *backend = malloc(sizeof(*backend));
    if (backend == NULL) {
        return VLC_ENOMEM;
    }
    vlc_mutex_lock(&ctx->lock);
    ctx->sys = backend;

    static const struct stt_whisper_backend_callbacks cbs = {
        .ready = stt_whisper_Ready,
        .send_subtitle = stt_whisper_SendSubtitle,
        .on_progress = stt_whisper_OnProgress,
        .confirm = stt_whisper_Confirm,
        .abort = stt_whisper_Abort,
    };
    backend->decoder = NULL;
    backend->owner.cbs = &cbs;
    backend->owner.opaque = ctx;
    backend->obj = VLC_OBJECT(ctx);
    backend->params.language = NULL;
    backend->params.model_name = NULL;

    static const struct vlc_stt_ctx_operations ops = {
        .close = stt_whisper_CxtClose,
        .abort = stt_whisper_CtxAbort,
    };
    ctx->ops = &ops;

    int ret = stt_whisper_backend_process_Init(backend);
    if (ret != VLC_SUCCESS) {
        msg_Warn(backend->obj, "Failed to start whisper on external process");
        if (stt_whisper_backend_thread_Init(backend) != VLC_SUCCESS) {
            msg_Err(backend->obj, "Failed to start whisper!");
            free(backend);
            ctx->sys = NULL;
            vlc_mutex_unlock(&ctx->lock);
            return VLC_EGENERIC;
        }
    }
    stt_whisper_InitParams(backend);
    ctx->status = vlc_stt_ctx_status_loading;
    ctx->delay = VLC_TICK_FROM_SEC(60);
    ctx->name = STT_WHISPER_MODULE_NAME;

    vlc_mutex_unlock(&ctx->lock);

    return VLC_SUCCESS;
}

static int
stt_whisper_Decode(decoder_t *decoder, vlc_frame_t *frame)
{
    assert(decoder != NULL);
    assert(decoder->p_sys != NULL);
    struct stt_whisper_decoder_sys *sys = decoder->p_sys;

    assert(sys->ctx != NULL);
    vlc_mutex_lock(&sys->ctx->lock);
    assert(sys->ctx->sys != NULL);
    struct stt_whisper_backend *backend = sys->ctx->sys;

    if (frame != NULL) {
        frame = aout_FiltersPlay(sys->filters, frame, 1.0);
        backend->ops->add_audio(backend, frame);
    } else {
        frame = aout_FiltersDrain(sys->filters);
        backend->ops->drain(backend, frame);
    }
    vlc_mutex_unlock(&sys->ctx->lock);
    return VLCDEC_SUCCESS;
}

static void
stt_whisper_Flush(decoder_t *decoder)
{
    assert(decoder != NULL);
    assert(decoder->p_sys != NULL);
    struct stt_whisper_decoder_sys *sys = decoder->p_sys;

    assert(sys->ctx != NULL);
    vlc_mutex_lock(&sys->ctx->lock);
    assert(sys->ctx->sys != NULL);
    struct stt_whisper_backend *backend = sys->ctx->sys;

    aout_FiltersFlush(sys->filters);
    backend->ops->flush(backend);
    vlc_mutex_unlock(&sys->ctx->lock);
}

/**
 * Create the audio conversion chain feeding the backend.
 *
 * The playback filters and visualizations configured by the user must not be
 * inherited here: the visualization modules request a vout from a clock
 * source that this chain does not have.
 */
static aout_filters_t *
stt_whisper_FiltersNew(decoder_t *decoder, const audio_format_t *fmt_in,
                       const audio_format_t *fmt_out)
{
    assert(decoder != NULL);
    assert(fmt_in != NULL);
    assert(fmt_out != NULL);

    var_Create(decoder, "audio-time-stretch", VLC_VAR_BOOL);
    var_Create(decoder, "audio-filter", VLC_VAR_STRING);
    var_Create(decoder, "audio-visual", VLC_VAR_STRING);

    aout_filters_t *filters = aout_FiltersNew(VLC_OBJECT(decoder), fmt_in,
                                              fmt_out, NULL);

    var_Destroy(decoder, "audio-visual");
    var_Destroy(decoder, "audio-filter");
    var_Destroy(decoder, "audio-time-stretch");
    return filters;
}

static int
stt_whisper_DecoderOpen(decoder_t *decoder)
{
    assert(decoder != NULL);

    if (VLC_CODEC_STT != decoder->fmt_in->i_codec) {
        return VLC_EGENERIC;
    }

    struct vlc_stt_extra *extra = decoder->fmt_in->p_extra;
    if (extra == NULL) {
        return VLC_EGENERIC;
    } else if (sizeof(*extra) != decoder->fmt_in->i_extra) {
        return VLC_EGENERIC;
    } else if (!AOUT_FMT_LINEAR(&extra->fmt)) {
        return VLC_EGENERIC;
    } else if (extra->ctx == NULL || extra->ctx->name == NULL) {
        return VLC_EGENERIC;
    } else if (extra->ctx->sys == NULL) {
        return VLC_EGENERIC;
    } else if (strcmp(extra->ctx->name, "whisper") != 0) {
        return VLC_EGENERIC;
    }

    struct stt_whisper_decoder_sys *sys = malloc(sizeof(*sys));
    if (sys == NULL) {
        return VLC_ENOMEM;
    }
    sys->decoder = decoder;

    audio_format_t afmt_in = extra->fmt;
    audio_format_t afmt_out = afmt_in;
    afmt_out.i_format = VLC_CODEC_FL32;
    afmt_out.i_rate = WHISPER_SAMPLE_RATE;
    afmt_out.i_physical_channels = AOUT_CHAN_CENTER;
    aout_FormatPrepare(&afmt_out);

    sys->filters = stt_whisper_FiltersNew(decoder, &afmt_in, &afmt_out);
    if (sys->filters == NULL) {
        free(sys);
        return VLC_EGENERIC;
    }

    assert(extra->ctx != NULL);
    sys->ctx = extra->ctx;
    assert(sys->ctx->sys != NULL);
    struct stt_whisper_backend *backend = sys->ctx->sys;
    vlc_mutex_lock(&sys->ctx->lock);
    if (backend->decoder != NULL) {
        vlc_mutex_unlock(&sys->ctx->lock);
        msg_Warn(VLC_OBJECT(decoder),
                 "Whisper decoder already attached to this context");
        aout_FiltersDelete(VLC_OBJECT(decoder), sys->filters);
        free(sys);
        return VLC_EGENERIC;
    }
    backend->decoder = decoder;
    vlc_mutex_unlock(&sys->ctx->lock);

    stt_whisper_InitParams(backend);
    backend->ops->start(backend);

    decoder->fmt_out.i_cat = SPU_ES;
    decoder->fmt_out.i_codec = VLC_CODEC_TEXT;
    decoder->pf_decode = stt_whisper_Decode;
    decoder->pf_flush = stt_whisper_Flush;
    decoder->p_sys = sys;
    return VLC_SUCCESS;
}

static void
stt_whisper_DecoderClose(decoder_t *decoder)
{
    assert(decoder != NULL);

    stt_whisper_Flush(decoder);

    assert(decoder->p_sys != NULL);
    struct stt_whisper_decoder_sys *sys = decoder->p_sys;
    assert(sys->ctx != NULL);
    struct vlc_stt_ctx *ctx = sys->ctx;
    vlc_mutex_lock(&ctx->lock);
    assert(sys->ctx->sys != NULL);
    struct stt_whisper_backend *backend = sys->ctx->sys;
    backend->decoder = NULL;
    backend->ops->stop(backend);
    aout_FiltersDelete(VLC_OBJECT(decoder), sys->filters);
    decoder->p_sys = NULL;
    free(sys);
    vlc_mutex_unlock(&ctx->lock);
}

/**
 * Module Setup
 */
#define STT_WHISPER_THREAD_TEXT N_("Number of thread")
#define STT_WHISPER_THREAD_LONGTEXT N_("Number of thread used by whisper.cpp "\
                                    "(default 0)")

#define STT_WHISPER_LEN_TEXT N_("Length in milliseconde")
#define STT_WHISPER_LEN_LONGTEXT N_("Duration of the sample send to "\
                                 "whisper.cpp in ms (default 30000)")

#define STT_WHISPER_MODEL_TEXT N_("Model name")
#define STT_WHISPER_MODEL_LONGTEXT N_("The model file name: "\
                                   "base, small, "\
                                   "medium, large "\
                                   "(default base)")

#define STT_WHISPER_LANGUAGE_TEXT N_("Language")
#define STT_WHISPER_LANGUAGE_LONGTEXT N_("Language spoken in the audio, "\
                                      "should be conform to ISO 639-1 "\
                                      "(default auto)")

#define STT_WHISPER_TRANSLATION_TEXT N_("Translation Language")
#define STT_WHISPER_TRANSLATION_LONGTEXT N_("Language of the translation "\
                                         "(default none)")

#define MADLAD_LANG_NAME(name, iso) name,
static const char * const translation_lang[] = {
    MADLAD_400_LANGUAGES(MADLAD_LANG_NAME)
    NULL
};
#undef MADLAD_LANG_NAME

#define WHISPER_LANG_NAME(name, iso) name,
static const char * const whisper_lang_names[] = {
    WHISPER_LANGUAGES(WHISPER_LANG_NAME)
    NULL
};
#undef WHISPER_LANG_NAME

#define WHISPER_LANG_ISO(name, iso) iso,
static const char * const whisper_lang_isos[] = {
    WHISPER_LANGUAGES(WHISPER_LANG_ISO)
    NULL
};
#undef WHISPER_LANG_ISO

vlc_module_begin()
    /* stt decoder module */
    set_description(N_("Stt decoder"))
    set_shortname( N_("Whisper"))
    set_subcategory( SUBCAT_INPUT_SCODEC )

    /* config var */
    add_integer_with_range(STT_WHISPER_CFG_PREFIX "nthread", 0, 0, 999,
                           STT_WHISPER_THREAD_TEXT, STT_WHISPER_THREAD_LONGTEXT)
    add_integer_with_range(STT_WHISPER_CFG_PREFIX "length", 30000, 5000, 40000,
                           STT_WHISPER_LEN_TEXT, STT_WHISPER_LEN_LONGTEXT)
    add_string(STT_WHISPER_CFG_PREFIX "model", "base", STT_WHISPER_MODEL_TEXT,
               STT_WHISPER_MODEL_LONGTEXT)
        change_string_list(stt_whisper_speech_model_values,
                           stt_whisper_speech_model_descriptions)
    add_string(STT_WHISPER_CFG_PREFIX "language", "auto",
               STT_WHISPER_LANGUAGE_TEXT, STT_WHISPER_LANGUAGE_LONGTEXT)
        change_string_list(whisper_lang_isos, whisper_lang_names)

    add_string(STT_WHISPER_CFG_PREFIX "translation", NULL,
               STT_WHISPER_TRANSLATION_TEXT, STT_WHISPER_TRANSLATION_LONGTEXT)
        change_string_list(translation_lang, translation_lang)

    set_capability("spu decoder", 100)
    set_callbacks(stt_whisper_DecoderOpen, stt_whisper_DecoderClose)

    add_submodule()
        set_description(N_("Whisper"))
        set_callback_stt_ctx_loader(stt_whisper_CtxInit, 50)

vlc_module_end()
