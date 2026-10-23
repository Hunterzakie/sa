// SPDX-License-Identifier: LGPL-2.1-or-later
/*****************************************************************************
 * backend_thread.c: Whisper threaded backend
 *****************************************************************************
 * Copyright © 2025 Videolabs
 *
 * Authors: Gabriel Lafond Thenaille <gabriel@videolabs.io>
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <assert.h>

#include <whisper.h>
#include <llama.h>

#include <vlc_common.h>
#include <vlc_frame.h>
#include <vlc_atomic.h>

#include "whisper_internal.h"

static atomic_bool stt_whisper_busy = false;

static void
stt_whisper_log_callback(enum ggml_log_level level, const char *text,
                         void *user_data)
{
    static atomic_int prev = GGML_LOG_LEVEL_NONE;

    if (level == GGML_LOG_LEVEL_CONT) {
        level = (enum ggml_log_level)atomic_load_explicit(&prev,
                                                          memory_order_relaxed);
    } else {
        atomic_store_explicit(&prev, (int)level, memory_order_relaxed);
    }

    vlc_object_t *obj = user_data;
    enum vlc_log_type log_type = VLC_MSG_INFO;

    switch (level) {
        case GGML_LOG_LEVEL_DEBUG:
        case GGML_LOG_LEVEL_INFO:
            log_type = VLC_MSG_DBG;
            break;
        case GGML_LOG_LEVEL_WARN:
            log_type = VLC_MSG_WARN;
            break;
        case GGML_LOG_LEVEL_ERROR:
            log_type = VLC_MSG_ERR;
            break;
        default:
            vlc_assert_unreachable();
    }

    char *msg = strdup(text);
    if (msg == NULL) {
        return;
    }
    char *ptr = strrchr(msg, '\n');
    if (ptr != NULL) {
        *ptr = '\0';
    }
    vlc_object_Log(obj, log_type, vlc_module_name, __FILE__, __LINE__,
                   __func__, "%s", msg);
}

static int
stt_whisper_backend_thread_AddAudio(struct stt_whisper_backend *backend,
                                    vlc_frame_t *frame)
{
    assert(backend != NULL);
    assert(backend->sys != NULL);
    struct stt_whisper_backend_thread *sys = backend->sys;
    assert(sys->fifo != NULL);

    if (frame == NULL) {
        return VLC_EGENERIC;
    }
    vlc_fifo_Lock(sys->fifo);
    vlc_fifo_QueueUnlocked(sys->fifo, frame);
    vlc_fifo_Unlock(sys->fifo);
    return VLC_SUCCESS;
}

static int
stt_whisper_backend_thread_Drain(struct stt_whisper_backend *backend,
                                 vlc_frame_t *frame)
{
    assert(backend != NULL);
    assert(backend->sys != NULL);
    struct stt_whisper_backend_thread *sys = backend->sys;
    assert(sys->fifo != NULL);
    vlc_fifo_Lock(sys->fifo);
    sys->drain = true;
    if (frame != NULL) {
        vlc_fifo_QueueUnlocked(sys->fifo, frame);
    } else {
        vlc_fifo_Signal(sys->fifo);
    }
    vlc_fifo_Unlock(sys->fifo);
    return VLC_SUCCESS;
}

static int
stt_whisper_backend_thread_Flush(struct stt_whisper_backend *backend)
{
    assert(backend != NULL);
    assert(backend->sys != NULL);
    struct stt_whisper_backend_thread *sys = backend->sys;
    assert(sys->fifo != NULL);
    vlc_fifo_Lock(sys->fifo);
    sys->drain = false;
    vlc_frame_t *frame = vlc_fifo_DequeueAllUnlocked(sys->fifo);
    if (frame != NULL) {
        vlc_frame_ChainRelease(frame);
    }
    vlc_fifo_Unlock(sys->fifo);
    atomic_store(&sys->abort, true);
    return VLC_SUCCESS;
}

static int
stt_whisper_backend_thread_SetParams(struct stt_whisper_backend *backend,
                                     const struct stt_whisper_params *params)
{
    assert(backend != NULL);
    assert(backend->sys != NULL);
    struct stt_whisper_backend_thread *sys = backend->sys;
    assert(sys->fifo != NULL);

    char *new_lang = params->language ? strdup(params->language) : NULL;
    if (params->language != NULL && new_lang == NULL) {
        return VLC_ENOMEM;
    }

    char *new_model = params->model_name ? strdup(params->model_name) : NULL;
    if (params->model_name != NULL && new_model == NULL) {
        free(new_lang);
        return VLC_ENOMEM;
    }

    vlc_fifo_Lock(sys->fifo);

    bool changed = false;
    if (new_model != NULL) {
        if (sys->params.model_name == NULL
            || strcmp(sys->params.model_name, new_model) != 0) {
            changed = true;
        }
        free(sys->params.model_name);
        sys->params.model_name = new_model;
    }
    if (new_lang != NULL) {
        if (sys->params.language == NULL
            || strcmp(sys->params.language, new_lang) != 0) {
            changed = true;
        }
        free(sys->params.language);
        sys->params.language = new_lang;
    }
    if (sys->params.translation_index != params->translation_index) {
        changed = true;
    }
    sys->params.translation_index = params->translation_index;
    sys->params.length_ms = params->length_ms;
    sys->params.n_threads = params->n_threads;

    if (!sys->params_ready) {
        sys->params_ready = true;
        vlc_fifo_Signal(sys->fifo);
    }
    if (changed && sys->whisper_ctx != NULL) {
        atomic_store(&sys->reload_needed, true);
        vlc_fifo_Signal(sys->fifo);
    }
    vlc_fifo_Unlock(sys->fifo);
    return VLC_SUCCESS;
}

static int
stt_whisper_backend_thread_Start(struct stt_whisper_backend *backend)
{
    assert(backend != NULL);
    assert(backend->sys != NULL);
    struct stt_whisper_backend_thread *sys = backend->sys;
    assert(sys->fifo != NULL);

    vlc_fifo_Lock(sys->fifo);
    sys->waiting = false;
    sys->drain = false;
    sys->stop = false;
    sys->pts = VLC_TICK_INVALID;
    vlc_fifo_Unlock(sys->fifo);
    return VLC_SUCCESS;
}

static void
stt_whisper_backend_thread_Stop(struct stt_whisper_backend *backend)
{
    assert(backend != NULL);
    assert(backend->sys != NULL);
    struct stt_whisper_backend_thread *sys = backend->sys;
    assert(sys->fifo != NULL);

    vlc_fifo_Lock(sys->fifo);
    sys->waiting = true;
    sys->drain = false;
    sys->pts = VLC_TICK_INVALID;
    vlc_frame_t *frame = vlc_fifo_DequeueAllUnlocked(sys->fifo);
    vlc_fifo_Unlock(sys->fifo);
    atomic_store(&sys->abort, true);
    if (frame != NULL) {
        vlc_frame_ChainRelease(frame);
    }
}

static void
stt_whisper_backend_thread_RequestAbort(struct stt_whisper_backend *backend)
{
    assert(backend != NULL);
    assert(backend->sys != NULL);
    struct stt_whisper_backend_thread *sys = backend->sys;
    assert(sys->fifo != NULL);

    /* Make the in-progress load (download/hash/load) bail out at its next
     * cancellation point without tearing the backend down. */
    atomic_store(&sys->abort, true);
    vlc_fifo_Lock(sys->fifo);
    vlc_fifo_Signal(sys->fifo);
    vlc_fifo_Unlock(sys->fifo);
}

static void
stt_whisper_backend_thread_Cancel(struct stt_whisper_backend *backend)
{
    assert(backend != NULL);
    assert(backend->sys != NULL);
    struct stt_whisper_backend_thread *sys = backend->sys;
    assert(sys->fifo != NULL);

    vlc_fifo_Lock(sys->fifo);
    sys->stop = true;
    vlc_fifo_Signal(sys->fifo);
    vlc_fifo_Unlock(sys->fifo);
    atomic_store(&sys->abort, true);
}

static void
stt_whisper_backend_thread_Close(struct stt_whisper_backend *backend)
{
    assert(backend != NULL);
    assert(backend->sys != NULL);
    struct stt_whisper_backend_thread *sys = backend->sys;
    assert(sys->fifo != NULL);

    stt_whisper_backend_thread_Cancel(backend);
    vlc_join(sys->th, NULL);
    vlc_fifo_Delete(sys->fifo);
    backend->sys = NULL;
    free(sys->params.language);
    free(sys->params.model_name);
    free(sys->loaded_model_name);
    free(sys->loaded_translation_lang_in);
    if (sys->ll_smpl != NULL) {
        llama_sampler_free(sys->ll_smpl);
    }
    if (sys->ll_ctx != NULL) {
        llama_free(sys->ll_ctx);
    }
    if (sys->ll_model != NULL) {
        llama_model_free(sys->ll_model);
    }
    if (sys->vad_ctx != NULL) {
        whisper_vad_free(sys->vad_ctx);
    }
    if (sys->whisper_ctx != NULL) {
        whisper_free(sys->whisper_ctx);
    }
    llama_backend_free();
    free(sys);
    atomic_store(&stt_whisper_busy, false);
}

static void *
stt_whisper_backend_thread_Thread(void *data)
{
    assert(data != NULL);
    struct stt_whisper_backend *backend = data;

    assert(backend->sys != NULL);
    struct stt_whisper_backend_thread *sys = backend->sys;

    vlc_fifo_Lock(sys->fifo);
    while (!sys->params_ready && !sys->stop) {
        vlc_fifo_Wait(sys->fifo);
    }
    if (sys->stop) {
        vlc_fifo_Unlock(sys->fifo);
        return NULL;
    }
    vlc_fifo_Unlock(sys->fifo);

    stt_whisper_Load(backend);
    if (atomic_load(&sys->abort)) {
        return NULL;
    }

    stt_whisper_run_ProcessLoop(backend);

    return NULL;
}

int
stt_whisper_backend_thread_Init(struct stt_whisper_backend *backend)
{
    assert(backend != NULL);

    if (atomic_exchange(&stt_whisper_busy, true)) {
        msg_Err(backend->obj, "whisper already running");
        return VLC_EGENERIC;
    }

    whisper_log_set(stt_whisper_log_callback, backend->obj);
    llama_log_set(stt_whisper_log_callback, backend->obj);

    struct stt_whisper_backend_thread *sys = malloc(sizeof(*sys));
    if (sys == NULL) {
        atomic_store(&stt_whisper_busy, false);
        return VLC_ENOMEM;
    }
    backend->sys = sys;
    backend->type = stt_whisper_backend_thread;

    sys->fifo = vlc_fifo_New();
    if (sys->fifo == NULL) {
        free(sys);
        backend->sys = NULL;
        atomic_store(&stt_whisper_busy, false);
        return VLC_ENOMEM;
    }

    sys->whisper_ctx = NULL;
    sys->vad_ctx = NULL;
    sys->ll_model = NULL;
    sys->ll_ctx = NULL;
    sys->ll_smpl = NULL;

    sys->loaded_model_name = NULL;
    sys->loaded_translation_index = -1;
    sys->loaded_translation_lang_in = NULL;
    atomic_init(&sys->reload_needed, false);

    atomic_init(&sys->abort, false);
    sys->drain = false;
    sys->stop = false;
    sys->waiting = true;
    sys->pts = VLC_TICK_INVALID;
    sys->params_ready = false;

    sys->params.language = NULL;
    sys->params.model_name = strdup("base");
    sys->params.length_ms = VLC_TICK_INVALID;
    sys->params.n_threads = 0;
    sys->params.translation_index = -1;

    static const struct stt_whisper_backend_operations ops = {
        .add_audio = stt_whisper_backend_thread_AddAudio,
        .drain = stt_whisper_backend_thread_Drain,
        .flush = stt_whisper_backend_thread_Flush,
        .set_params = stt_whisper_backend_thread_SetParams,
        .start = stt_whisper_backend_thread_Start,
        .stop = stt_whisper_backend_thread_Stop,
        .cancel = stt_whisper_backend_thread_Cancel,
        .close = stt_whisper_backend_thread_Close,
        .request_abort = stt_whisper_backend_thread_RequestAbort,
    };
    backend->ops = &ops;
    backend->type = stt_whisper_backend_thread;

    int ret = vlc_clone(&sys->th, stt_whisper_backend_thread_Thread, backend);
    if (ret != VLC_SUCCESS) {
        msg_Err(backend->obj, "whisper thread failed to start");
        backend->ops = NULL;
        free(sys->params.model_name);
        vlc_fifo_Delete(sys->fifo);
        free(sys);
        backend->sys = NULL;
        atomic_store(&stt_whisper_busy, false);
        return VLC_EGENERIC;
    }

    return VLC_SUCCESS;
}
