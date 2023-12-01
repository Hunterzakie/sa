// SPDX-License-Identifier: LGPL-2.1-or-later
/*****************************************************************************
 * translate.c: Translate related function
 *****************************************************************************
 * Copyright © 2025 Videolabs, VideoLAN and VLC authors
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
#include <vlc_process.h>
#include <vlc_tick.h>
#include <vlc_vector.h>

#include "whisper_internal.h"
#include "whisper_translation.h"

#define STT_WHISPER_TRANSLATE_MAX_TOKENS 256

int
stt_whisper_translate_GetIndex(const char *lang_txt)
{
    int i = 0;
    for (; i < madlad_400_languages_count; i++) {
        const struct madlad_400_language *lang = NULL;
        lang = &madlad_400_languages[i];
        if (!strcmp(lang->name, lang_txt)) {
            break;
        } else if (!strcmp(lang->iso_639, lang_txt)) {
            break;
        }
    }
    if (i == madlad_400_languages_count) {
        return -1;
    }
    return i;
}

const char *
stt_whisper_translate_GetLangCode(int index)
{
    if (index < 0 || index >= madlad_400_languages_count) {
        return NULL;
    }
    return madlad_400_languages[index].iso_639;
}

bool
stt_whisper_translate_CallbackAbort(void *userdata)
{
    assert(userdata != NULL);
    struct stt_whisper_backend *backend = userdata;
    assert(backend->type == stt_whisper_backend_thread);
    assert(backend->sys != NULL);
    struct stt_whisper_backend_thread *sys = backend->sys;

    return atomic_load(&sys->abort);
}

void
stt_whisper_translate_Unload(struct stt_whisper_backend *backend)
{
    assert(backend != NULL);
    assert(backend->type == stt_whisper_backend_thread);
    assert(backend->sys != NULL);
    struct stt_whisper_backend_thread *sys = backend->sys;

    if (sys->ll_smpl != NULL) {
        llama_sampler_free(sys->ll_smpl);
        sys->ll_smpl = NULL;
    }
    if (sys->ll_ctx != NULL) {
        llama_free(sys->ll_ctx);
        sys->ll_ctx = NULL;
    }
    if (sys->ll_model != NULL) {
        llama_model_free(sys->ll_model);
        sys->ll_model = NULL;
    }
    sys->loaded_translation_index = -1;
    free(sys->loaded_translation_lang_in);
    sys->loaded_translation_lang_in = NULL;
}

int
stt_whisper_translate_Load(struct stt_whisper_backend *backend)
{
    assert(backend != NULL);
    assert(backend->type == stt_whisper_backend_thread);
    assert(backend->sys != NULL);
    struct stt_whisper_backend_thread *sys = backend->sys;

    vlc_fifo_Lock(sys->fifo);
    int t_index = sys->params.translation_index;
    char *lang_in = NULL;
    if (sys->params.language != NULL) {
        lang_in = strdup(sys->params.language);
    }
    vlc_fifo_Unlock(sys->fifo);

    const char *lang_out = NULL;
    if (t_index > 0) {
        lang_out = stt_whisper_translate_GetLangCode(t_index);
    }

    bool lang_in_auto = (lang_in != NULL && strcmp(lang_in, "auto") == 0);
    bool lang_in_differs = (lang_in != NULL && lang_out != NULL
                            && strcmp(lang_in, lang_out) != 0);
    bool needs_translation = (lang_out != NULL)
                             && (lang_in_auto || lang_in_differs);
    if (!needs_translation) {
        free(lang_in);
        return VLC_SUCCESS;
    }

    bool same_index = (sys->loaded_translation_index == t_index);
    bool same_lang_in = (sys->loaded_translation_lang_in != NULL
                         && strcmp(sys->loaded_translation_lang_in, lang_in) == 0);
    bool already_loaded = (sys->ll_model != NULL && same_index && same_lang_in);
    if (already_loaded) {
        free(lang_in);
        return VLC_SUCCESS;
    }

    stt_whisper_translate_Unload(backend);

    const struct stt_whisper_aux_model *model =
        stt_whisper_translation_model_Find("madlad400");
    if (model == NULL) {
        free(lang_in);
        return VLC_EGENERIC;
    }

    char *path = stt_whisper_aux_model_Resolve(backend, model);
    if (path == NULL) {
        free(lang_in);
        return VLC_EGENERIC;
    }

    struct llama_model *ll_model = NULL;
    struct llama_context *ll_ctx = NULL;
    struct llama_sampler *ll_smpl = NULL;

    struct llama_model_params ll_model_params = llama_model_default_params();
    ll_model_params.n_gpu_layers = INT32_MAX;
    if (backend->owner.cbs->on_progress != NULL) {
        backend->owner.cbs->on_progress(backend->owner.opaque,
                                        vlc_stt_ctx_status_loading,
                                        0, -1, "madlad400");
    }
    ll_model = llama_model_load_from_file(path, ll_model_params);
    free(path);
    if (ll_model == NULL) {
        free(lang_in);
        return VLC_EGENERIC;
    }
    if (backend->owner.cbs->on_progress != NULL) {
        backend->owner.cbs->on_progress(backend->owner.opaque,
                                        vlc_stt_ctx_status_loading,
                                        1, 1, "madlad400");
    }

    struct llama_sampler_chain_params smp_params =
        llama_sampler_chain_default_params();
    ll_smpl = llama_sampler_chain_init(smp_params);
    if (ll_smpl == NULL) {
        llama_model_free(ll_model);
        free(lang_in);
        return VLC_EGENERIC;
    }
    llama_sampler_chain_add(ll_smpl, llama_sampler_init_greedy());

    struct llama_sampler_seq_config smpl_cfg = {
        .seq_id = 0,
        .sampler = ll_smpl,
    };

    struct llama_context_params ctx_params = llama_context_default_params();
    ctx_params.n_ctx = 0;
    ctx_params.n_batch = 1024;
    ctx_params.n_ubatch = 1024;
    ctx_params.n_seq_max = 1;
    ctx_params.embeddings = true;
    ctx_params.offload_kqv = true;
    ctx_params.kv_unified = true;
    ctx_params.abort_callback = stt_whisper_translate_CallbackAbort;
    ctx_params.abort_callback_data = backend;
    ctx_params.samplers = &smpl_cfg;
    ctx_params.n_samplers = 1;
    ll_ctx = llama_init_from_model(ll_model, ctx_params);
    if (ll_ctx == NULL) {
        llama_sampler_free(ll_smpl);
        llama_model_free(ll_model);
        free(lang_in);
        return VLC_EGENERIC;
    }

    sys->ll_model = ll_model;
    sys->ll_ctx = ll_ctx;
    sys->ll_smpl = ll_smpl;
    sys->loaded_translation_index = t_index;
    sys->loaded_translation_lang_in = lang_in;
    return VLC_SUCCESS;
}


vlc_frame_t *
stt_whisper_translate_Run(struct stt_whisper_backend *backend,
                          vlc_frame_t *sub)
{
    assert(backend != NULL);
    assert(backend->type == stt_whisper_backend_thread);
    assert(backend->sys != NULL);
    struct stt_whisper_backend_thread *sys = backend->sys;
    assert(sub != NULL);

    struct llama_context *ctx = sys->ll_ctx;
    if (ctx == NULL)
        return sub;

    llama_memory_clear(llama_get_memory(ctx), false);

    int trans_index = sys->params.translation_index;
    assert(trans_index > 0);
    assert(trans_index < madlad_400_languages_count);
    const char *lang_txt = madlad_400_languages[trans_index].iso_639;
    char *prompt = NULL;
    int len = asprintf(&prompt, "<2%s> %.*s", lang_txt,
                       (int)sub->i_buffer, sub->p_buffer);
    if (len < 0) {
        return sub;
    }

    const struct llama_vocab * vocab = llama_model_get_vocab(sys->ll_model);

    int n_tokens_enc = -llama_tokenize(vocab, prompt, len, NULL, 0,
                                       true, false);
    if (n_tokens_enc <= 0) {
        free(prompt);
        return sub;
    }
    /* Clamp to the model's actual context window to bound memory use. */
    int ctx_size = llama_n_ctx(sys->ll_ctx);
    if (n_tokens_enc > ctx_size)
        n_tokens_enc = ctx_size;

    llama_token* tokens_enc = malloc(n_tokens_enc * sizeof(llama_token));
    if (tokens_enc == NULL) {
        free(prompt);
        return sub;
    }
    n_tokens_enc = llama_tokenize(vocab, prompt, len, tokens_enc,
                                  n_tokens_enc, true, false);
    free(prompt);
    if (n_tokens_enc <= 0) {
        free(tokens_enc);
        return sub;
    }

    struct llama_batch batch_enc = llama_batch_init(n_tokens_enc, 0, 1);
    batch_enc.n_tokens = n_tokens_enc;

    for (int i = 0; i < n_tokens_enc; i++) {
        batch_enc.token[i] = tokens_enc[i];
        batch_enc.pos[i] = i;
        batch_enc.n_seq_id[i] = 1;
        batch_enc.seq_id[i][0] = 0;
        batch_enc.logits[i] = 0;
    }

    if (llama_encode(ctx, batch_enc) != 0) {
        llama_batch_free(batch_enc);
        free(tokens_enc);
        return sub;
    }

    llama_token decoder_start_token = llama_model_decoder_start_token(sys->ll_model);
    if (decoder_start_token == LLAMA_TOKEN_NULL) {
        decoder_start_token = llama_vocab_bos(vocab);
    }

    // Create decoder batch with BOS token
    int max_tokens = STT_WHISPER_TRANSLATE_MAX_TOKENS;
    int n_ctx = (int)llama_n_ctx(sys->ll_ctx);
    int dec_cap = max_tokens < n_ctx ? max_tokens : n_ctx;
    if (dec_cap < 1) {
        dec_cap = 1;
    }
    struct llama_batch batch_dec = llama_batch_init(dec_cap, 0, 1);
    batch_dec.n_tokens = 1;
    batch_dec.token[0] = decoder_start_token;
    batch_dec.pos[0] = 0;
    batch_dec.n_seq_id[0] = 1;
    batch_dec.seq_id[0][0] = 0;
    batch_dec.logits[0] = 1;

    // Decode the first token
    if (llama_decode(ctx, batch_dec) != 0) {
        llama_batch_free(batch_dec);
        llama_batch_free(batch_enc);
        free(tokens_enc);
        return sub;
    }

    // Generate response
    int n_generated = 0;
    int n_cur = 1;

    struct VLC_VECTOR(char) vec;
    vlc_vector_init(&vec);

    while (n_generated < max_tokens) {
        if (atomic_load(&sys->abort)) {
            break;
        }

        llama_token new_token = llama_get_sampled_token_ith(ctx, -1);

        if (llama_vocab_is_eog(vocab, new_token)) {
            break;
        }

        char buf[128];
        int n = llama_token_to_piece(vocab, new_token, buf, sizeof(buf), 0,
                                     true);
        if (n > 0) {
            vlc_vector_push_all(&vec, buf, n);
        }

        batch_dec.n_tokens = 1;
        batch_dec.token[0] = new_token;
        batch_dec.pos[0] = n_cur;
        batch_dec.n_seq_id[0] = 1;
        batch_dec.seq_id[0][0] = 0;
        batch_dec.logits[0] = 1;
        n_cur++;

        if (llama_decode(ctx, batch_dec) != 0) {
            break;
        }

        n_generated++;
    }

    llama_batch_free(batch_dec);
    llama_batch_free(batch_enc);
    free(tokens_enc);

    vlc_frame_t *translated = vlc_frame_Alloc(vec.size + 1);
    if (translated != NULL) {
        vlc_frame_CopyProperties(translated, sub);
        vlc_frame_Release(sub);
        memset(translated->p_buffer, 0, vec.size + 1);
        memcpy(translated->p_buffer, vec.data, vec.size);
        sub = translated;
    }
    vlc_vector_clear(&vec);
    return sub;
}
