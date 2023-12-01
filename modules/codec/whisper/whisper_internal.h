// SPDX-License-Identifier: LGPL-2.1-or-later
/*****************************************************************************
 * whisper_internal.h: Internal function of the whisper module
 *****************************************************************************
 * Copyright © 2025 Videolabs, VideoLAN and VLC authors
 *
 * Authors: Gabriel Lafond Thenaille <gabriel@videolabs.io>
 *****************************************************************************/

#ifndef VLC_WHISPER_INTERNAL_H
#define VLC_WHISPER_INTERNAL_H

#include <whisper.h>

#include <vlc_aout.h>
#include <vlc_frame.h>
#include <vlc_stt.h>
#include <vlc_vector.h>

#define STT_WHISPER_CFG_PREFIX "whisper-"
#define STT_WHISPER_MODULE_NAME "whisper"
#define STT_WHISPER_NCHAR_MAX 84

#define STT_WHISPER_SAMPLE_PER_MS (WHISPER_SAMPLE_RATE / 1000)
#define STT_WHISPER_BYTE_PER_MS (STT_WHISPER_SAMPLE_PER_MS * sizeof(float))

#define STT_WHISPER_KEEP_MS 200
#define STT_WHISPER_KEEP_TICK VLC_TICK_FROM_MS(STT_WHISPER_KEEP_MS)
#define STT_WHISPER_KEEP_BYTES (STT_WHISPER_KEEP_MS * STT_WHISPER_BYTE_PER_MS)

/* Module structures */
struct stt_whisper_token {
    vlc_tick_t t0;
    vlc_tick_t t1;
    char *text;
    float p;
};

/* Whisper params */
struct stt_whisper_params {
    char *language;
    char *model_name;
    int n_threads;
    vlc_tick_t length_ms;
    int translation_index;
};

/* Downloadable model description used by the loader. */
struct stt_whisper_model_base {
    const char *file_name;
    const char *checksum;
    const char *url;
};

struct stt_whisper_speech_model {
    struct stt_whisper_model_base file;
    const char *nickname;
    const struct stt_whisper_model_base *coreml_zip;
    const char *coreml_dir_name;
};

struct stt_whisper_aux_model {
    struct stt_whisper_model_base file;
    const char *nickname;
};

/** Backend */
struct stt_whisper_backend_thread {
    struct whisper_context *whisper_ctx;
    struct whisper_vad_context *vad_ctx;

    struct llama_model *ll_model;
    struct llama_context *ll_ctx;
    struct llama_sampler *ll_smpl;

    vlc_tick_t pts;

    atomic_bool abort;
    vlc_thread_t th;

    struct stt_whisper_params params;

    /* Tracking for idempotent Load (worker-only). */
    char *loaded_model_name;
    int   loaded_translation_index;
    char *loaded_translation_lang_in;

    atomic_bool reload_needed;

    bool drain;
    bool stop;
    bool waiting;
    bool params_ready;

    vlc_fifo_t *fifo;
};

struct stt_whisper_backend_process {
    struct vlc_process *process;
    vlc_thread_t th;
    struct vlc_interrupt *interrupt;
    atomic_bool running;

    /* Serialises whole messages: the decoder thread and the receive loop
     * thread both send on this pipe, and vlc_process_fd_Write() allows a
     * single writer at a time. */
    vlc_mutex_t send_lock;
};

struct stt_whisper_backend;

enum stt_whisper_backend_type {
    stt_whisper_backend_process,
    stt_whisper_backend_thread,
};

struct stt_whisper_backend_operations {
    int (*add_audio)(struct stt_whisper_backend *backend,
                     vlc_frame_t *frame);
    int (*drain)(struct stt_whisper_backend *backend,
                 vlc_frame_t *frame);
    int (*flush)(struct stt_whisper_backend *backend);
    int (*set_params)(struct stt_whisper_backend *backend,
                      const struct stt_whisper_params *params);
    int (*start)(struct stt_whisper_backend *backend);
    void (*stop)(struct stt_whisper_backend *backend);
    void (*cancel)(struct stt_whisper_backend *backend);
    void (*close)(struct stt_whisper_backend *backend);
    void (*request_abort)(struct stt_whisper_backend *backend);
};

struct stt_whisper_backend_callbacks {
    void (*ready)(void *opaque, enum vlc_stt_ctx_status status);
    int (*send_subtitle)(void *opaque, vlc_frame_t *subtitle);
    void (*on_progress)(void *opaque, enum vlc_stt_ctx_status status,
                        int64_t position, int64_t total,
                        const char *description);
    bool (*confirm)(void *opaque, uint64_t download_size, const char *path);
    bool (*abort)(void *opaque);
    void (*confirm_reply)(void *opaque, bool accepted);
};

struct stt_whisper_backend {
    enum stt_whisper_backend_type type;
    const struct stt_whisper_backend_operations *ops;
    void *sys;

    struct {
        const struct stt_whisper_backend_callbacks *cbs;
        void *opaque;
    } owner;

    struct stt_whisper_params params;

    decoder_t *decoder;

    vlc_object_t *obj;
};

int
stt_whisper_backend_process_Init(struct stt_whisper_backend *backend);

int
stt_whisper_backend_thread_Init(struct stt_whisper_backend *backend);

void
stt_whisper_Load(struct stt_whisper_backend *backend);

const struct stt_whisper_speech_model *
stt_whisper_speech_model_Find(const char *nickname);

const struct stt_whisper_aux_model *
stt_whisper_translation_model_Find(const char *nickname);

char *
stt_whisper_aux_model_Resolve(struct stt_whisper_backend *backend,
                              const struct stt_whisper_aux_model *model);

#define STT_WHISPER_NSPEECH_MODELS 4
extern const char *const stt_whisper_speech_model_values[STT_WHISPER_NSPEECH_MODELS];
extern const char *const stt_whisper_speech_model_descriptions[STT_WHISPER_NSPEECH_MODELS];
/** End backend */

/* decoder internal struct */
struct stt_whisper_decoder_sys {
    struct vlc_stt_ctx *ctx;
    aout_filters_t *filters;
    decoder_t *decoder;
};


/* Main run loop */
void
stt_whisper_run_ProcessLoop(struct stt_whisper_backend *backend);

bool stt_whisper_translate_CallbackAbort(void *userdata);

/* Translate the input frame and return the translated frame.
 * Give the ownership of the input frame to the function. */
vlc_frame_t *
stt_whisper_translate_Run(struct stt_whisper_backend *backend,
                          vlc_frame_t *sub);

int
stt_whisper_translate_Load(struct stt_whisper_backend *backend);

void
stt_whisper_translate_Unload(struct stt_whisper_backend *backend);

int
stt_whisper_translate_GetIndex(const char *lang_txt);

const char *
stt_whisper_translate_GetLangCode(int index);

#endif
