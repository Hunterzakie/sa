// SPDX-License-Identifier: LGPL-2.1-or-later
/*****************************************************************************
 * loader: Loader of whisper models
 *****************************************************************************
 * Copyright © 2025 Videolabs, VideoLAN and VLC authors
 *
 * Authors: Gabriel Lafond Thenaille <gabriel@videolabs.io>
 *****************************************************************************/

#include <stdint.h>
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <assert.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>

#ifdef HAVE_GCRYPT
#include <gcrypt.h>
#endif

#include <whisper.h>
#include <llama.h>

#include <vlc_common.h>
#include <vlc_configuration.h>
#include <vlc_stream.h>
#include <vlc_stream_extractor.h>
#include <vlc_fs.h>
#include <vlc_gcrypt.h>
#include <vlc_strings.h>
#include <vlc_stt.h>
#include <vlc_tick.h>
#include <vlc_frame.h>

#include "whisper_internal.h"

static const struct stt_whisper_model_base coreml_base = {
    "ggml-base-encoder.mlmodelc.zip",
    "7e6ab77041942572f239b5b602f8aaa1c3ed29d73e3d8f20abea03a773541089",
    "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base-encoder.mlmodelc.zip",
};
static const struct stt_whisper_model_base coreml_small = {
    "ggml-small-encoder.mlmodelc.zip",
    "de43fb9fed471e95c19e60ae67575c2bf09e8fb607016da171b06ddad313988b",
    "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small-encoder.mlmodelc.zip",
};
static const struct stt_whisper_model_base coreml_medium = {
    "ggml-medium-encoder.mlmodelc.zip",
    "79b0b8d436d47d3f24dd3afc91f19447dd686a4f37521b2f6d9c30a642133fbd",
    "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-medium-encoder.mlmodelc.zip",
};
static const struct stt_whisper_model_base coreml_largev3 = {
    "ggml-large-v3-turbo-encoder.mlmodelc.zip",
    "84bedfe895bd7b5de6e8e89a0803dfc5addf8c0c5bc4c937451716bf7cf7988a",
    "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-large-v3-turbo-encoder.mlmodelc.zip",
};

static const struct stt_whisper_speech_model whisper_speech_models[] = {
    {
        .file = {
            "ggml-base.bin",
            "60ed5bc3dd14eea856493d334349b405782ddcaf0028d4b5df4088345fba2efe",
            "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.bin",
        },
        .nickname = "base",
        .coreml_zip = &coreml_base,
        .coreml_dir_name = "ggml-base-encoder.mlmodelc",
    },
    {
        .file = {
            "ggml-small.bin",
            "1be3a9b2063867b937e64e2ec7483364a79917e157fa98c5d94b5c1fffea987b",
            "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small.bin",
        },
        .nickname = "small",
        .coreml_zip = &coreml_small,
        .coreml_dir_name = "ggml-small-encoder.mlmodelc",
    },
    {
        .file = {
            "ggml-medium.bin",
            "6c14d5adee5f86394037b4e4e8b59f1673b6cee10e3cf0b11bbdbee79c156208",
            "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-medium.bin",
        },
        .nickname = "medium",
        .coreml_zip = &coreml_medium,
        .coreml_dir_name = "ggml-medium-encoder.mlmodelc",
    },
    {
        .file = {
            "ggml-large-v3-turbo.bin",
            "1fc70f774d38eb169993ac391eea357ef47c88757ef72ee5943879b7e8e2bc69",
            "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-large-v3-turbo.bin",
        },
        .nickname = "large-v3",
        .coreml_zip = &coreml_largev3,
        .coreml_dir_name = "ggml-large-v3-turbo-encoder.mlmodelc",
    },
};

static const struct stt_whisper_aux_model whisper_vad_models[] = {
    {
        .file = {
            "ggml-silero-v6.2.0.bin",
            "2aa269b785eeb53a82983a20501ddf7c1d9c48e33ab63a41391ac6c9f7fb6987",
            "https://huggingface.co/ggml-org/whisper-vad/resolve/main/ggml-silero-v6.2.0.bin",
        },
        .nickname = "silero",
    },
};

static const struct stt_whisper_aux_model whisper_translation_models[] = {
    {
        .file = {
            "madlad400-3b-mt-q4_k_m.gguf",
            "fc56f16d215db71e856de3c3770974c867e3d95a782d415d4cfabc9fb470b8e4",
            "https://huggingface.co/mtsdurica/madlad400-3b-mt-Q4_K_M-GGUF/resolve/main/madlad400-3b-mt-q4_k_m.gguf",
        },
        .nickname = "madlad400",
    },
};

static_assert(ARRAY_SIZE(whisper_speech_models)
              == STT_WHISPER_NSPEECH_MODELS,
              "speech model table size out of sync with header");

#define STT_WHISPER_NTRANSLATION_MODELS ARRAY_SIZE(whisper_translation_models)

const char *const stt_whisper_speech_model_values[STT_WHISPER_NSPEECH_MODELS] = {
    "base",
    "small",
    "medium",
    "large-v3",
};
const char *const stt_whisper_speech_model_descriptions[STT_WHISPER_NSPEECH_MODELS] = {
    N_("Base (fastest, uses the least resources, lowest accuracy)"),
    N_("Small (fast, low resource usage, decent accuracy)"),
    N_("Medium (slower, higher resource usage, good accuracy)"),
    N_("Large (slowest, uses the most resources, best accuracy)"),
};

static const char *const stt_whisper_translation_model_values[] = {
    "madlad400-3b",
};
static const char *const stt_whisper_translation_model_descriptions[] = {
    N_("madlad400 3b"),
};
static_assert(ARRAY_SIZE(stt_whisper_translation_model_values)
              == STT_WHISPER_NTRANSLATION_MODELS,
              "translation model values array out of sync with model table");
static_assert(ARRAY_SIZE(stt_whisper_translation_model_descriptions)
              == STT_WHISPER_NTRANSLATION_MODELS,
              "translation model descriptions array out of sync with model table");

const struct stt_whisper_speech_model *
stt_whisper_speech_model_Find(const char *nickname)
{
    if (nickname == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < STT_WHISPER_NSPEECH_MODELS; i++) {
        if (strcmp(whisper_speech_models[i].nickname, nickname) == 0) {
            return &whisper_speech_models[i];
        }
    }
    return NULL;
}

const struct stt_whisper_aux_model *
stt_whisper_translation_model_Find(const char *nickname)
{
    if (nickname == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < STT_WHISPER_NTRANSLATION_MODELS; i++) {
        if (strcmp(whisper_translation_models[i].nickname, nickname) == 0) {
            return &whisper_translation_models[i];
        }
    }
    return NULL;
}

static char *
stt_whisper_model_GetCacheDirPath(void)
{
    char *cache_dir = config_GetUserDir(VLC_CACHE_DIR);
    if (cache_dir == NULL) {
        return NULL;
    }
    char *cache_path = NULL;
    int ret = asprintf(&cache_path, "%s" DIR_SEP "models",
                       cache_dir);
    free(cache_dir);
    if (ret < 0) {
        return NULL;
    }
    return cache_path;
}

static char *
stt_whisper_model_GetCachePath(const struct stt_whisper_model_base *model)
{
    assert(model != NULL);

    char *cache_dir = stt_whisper_model_GetCacheDirPath();
    if (cache_dir == NULL) {
        return NULL;
    }
    char *model_path = NULL;
    int ret = asprintf(&model_path, "%s" DIR_SEP "%s",
                       cache_dir, model->file_name);
    free(cache_dir);
    if (ret < 0) {
        return NULL;
    }
    return model_path;
}

static int
stt_whisper_model_CreateSubDirectory(vlc_object_t *obj, const char *file_path)
{
    assert(obj != NULL);
    assert(file_path != NULL);

    const char *sep = strrchr(file_path, DIR_SEP_CHAR);
    if (sep == NULL) {
        return VLC_SUCCESS;
    }
    char *basedir = strndup(file_path, sep - file_path);
    if (basedir == NULL) {
        return VLC_ENOMEM;
    }
    int ret = vlc_mkdir_parent(basedir, 0700);
    if (ret != 0) {
        int err = errno;
        msg_Err(obj, "Can't create `%s' directory: %s", file_path,
                vlc_strerror_c(err));
        return -err;
    }
    return VLC_SUCCESS;
}

#define STT_WHISPER_DL_BUF_SIZE (1 << 24)

#ifdef HAVE_GCRYPT
static int
stt_whisper_model_CheckHashPath(struct stt_whisper_backend *backend,
                                const char *model_path,
                                const struct stt_whisper_model_base *model)
{
    assert(backend != NULL);
    assert(backend->obj != NULL);
    assert(model_path != NULL);
    assert(model != NULL);
    assert(model->checksum != NULL);

    int fd = vlc_open(model_path, O_RDONLY);
    if (fd < 0) {
        return errno == ENOENT ? 0 : -1;
    }

    struct stat st;
    if (fstat(fd, &st) != 0) {
        vlc_close(fd);
        return -1;
    }
    const int64_t total_size = st.st_size;

    uint8_t *buf = malloc(STT_WHISPER_DL_BUF_SIZE);
    if (buf == NULL) {
        vlc_close(fd);
        return -1;
    }

    gcry_md_hd_t gctx;
    vlc_gcrypt_init();
    if (gcry_md_open(&gctx, GCRY_MD_SHA256, GCRY_MD_FLAG_SECURE)) {
        free(buf);
        vlc_close(fd);
        return -1;
    }

    int ret = 1;
    int64_t hashed = 0;
    int64_t last_progress = 0;
    if (backend->owner.cbs->on_progress != NULL) {
        backend->owner.cbs->on_progress(backend->owner.opaque,
                                        vlc_stt_ctx_status_hashing, hashed,
                                        total_size, model->file_name);
    }

    for (;;) {
        ssize_t read_size = read(fd, buf, STT_WHISPER_DL_BUF_SIZE);
        if (read_size > 0) {
            gcry_md_write(gctx, buf, read_size);
            hashed += read_size;
            int64_t p = (int64_t)(((float)hashed / (float)total_size) * 100);
            if (backend->owner.cbs->on_progress != NULL && p > last_progress) {
                backend->owner.cbs->on_progress(backend->owner.opaque,
                                                vlc_stt_ctx_status_hashing,
                                                hashed, total_size,
                                                model->file_name);
                last_progress = p;
            }
        } else if (read_size == 0) {
            break;
        } else {
            ret = -1;
            break;
        }
    }

    if (ret == 1) {
        gcry_md_final(gctx);
        unsigned char *hash = gcry_md_read(gctx, 0);
        char hash_hex[(32 * 2) + 1];
        vlc_hex_encode_binary(hash, 32, hash_hex);

        if (strcmp(hash_hex, model->checksum) != 0) {
            msg_Err(backend->obj, "Cached model `%s' has wrong hash",
                    model->file_name);
            ret = -1;
        }
    }

    gcry_md_close(gctx);
    free(buf);
    vlc_close(fd);
    return ret;
}
#endif

static int
stt_whisper_model_VerifyHash(struct stt_whisper_backend *backend,
                             const char *model_path,
                             const struct stt_whisper_model_base *model)
{
    assert(backend != NULL);
    assert(model_path != NULL);
    assert(model != NULL);

#ifdef HAVE_GCRYPT
    return stt_whisper_model_CheckHashPath(backend, model_path, model);
#else
    struct stat st;
    VLC_UNUSED(backend);
    VLC_UNUSED(model);
    return vlc_stat(model_path, &st) == 0 ? 1 : 0;
#endif
}

static int
stt_whisper_model_Download(struct stt_whisper_backend *backend,
                           const struct stt_whisper_model_base *model)
{
    assert(backend != NULL);
    assert(backend->type == stt_whisper_backend_thread);
    assert(backend->sys != NULL);
    struct stt_whisper_backend_thread *sys = backend->sys;
    assert(model != NULL);
    assert(model->url != NULL);
    assert(model->file_name != NULL);
    assert(model->checksum != NULL);

#ifdef HAVE_GCRYPT
    stream_t *stream = NULL;
    int fd = -1;
    char *model_path = NULL;
    uint8_t *buf = malloc(STT_WHISPER_DL_BUF_SIZE);
    if (buf == NULL) {
        return VLC_ENOMEM;
    }

    stream = vlc_stream_NewURL(backend->obj, model->url);
    if (stream == NULL) {
        free(buf);
        return VLC_ENOMEM;
    }

    model_path = stt_whisper_model_GetCachePath(model);
    if (model_path == NULL) {
        goto error;
    }

    if (stt_whisper_model_CreateSubDirectory(backend->obj, model_path) != 0) {
        goto error;
    }

    uint64_t total_size = 0;
    if (vlc_stream_GetSize(stream, &total_size) < 0) {
        goto error;
    }

    fd = vlc_open(model_path, O_CREAT | O_EXCL | O_WRONLY,
                  S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fd < 0) {
        if (errno == EEXIST) {
            int hash_status = stt_whisper_model_VerifyHash(backend, model_path,
                                                           model);
            if (hash_status == 1) {
                vlc_stream_Delete(stream);
                free(model_path);
                free(buf);
                return VLC_SUCCESS;
            }
            if (hash_status == -1 && vlc_unlink(model_path) != 0) {
                goto error;
            }
            fd = vlc_open(model_path, O_CREAT | O_EXCL | O_WRONLY,
                          S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
            if (fd < 0) {
                goto error;
            }
        }
        else {
            goto error;
        }
    }

    /* A NULL confirm callback means "proceed" (see vlc_stt_ctx_callbacks). The
     * external-process backend does not wire confirm/abort, so guard against a
     * NULL pointer instead of crashing the loader thread. */
    if (backend->owner.cbs->confirm != NULL
        && !backend->owner.cbs->confirm(backend->owner.opaque, total_size,
                                        model_path)) {
        goto error;
    }

    msg_Dbg(backend->obj, "Start downloading %s(%" PRIu64 "MB) from %s",
            model->file_name, total_size / 1000000, model->url);

    gcry_md_hd_t gctx;
    vlc_gcrypt_init();
    if (gcry_md_open(&gctx, GCRY_MD_SHA256, GCRY_MD_FLAG_SECURE)) {
        goto error;
    }

    int ret = STT_WHISPER_DL_BUF_SIZE;
    size_t downloaded = 0;
    size_t last_progress = 0;

    if (backend->owner.cbs->on_progress != NULL) {
        backend->owner.cbs->on_progress(backend->owner.opaque,
                                        vlc_stt_ctx_status_downloading,
                                        downloaded, total_size,
                                        model->file_name);
    }

    while (1) {
        ret = vlc_stream_ReadPartial(stream, buf, STT_WHISPER_DL_BUF_SIZE);
        if (ret > 0) {
            const uint8_t *ptr = buf;
            while (ret > 0) {
                int wret = write(fd, ptr, ret);
                if (wret <= 0) {
                    gcry_md_close(gctx);
                    goto error;
                }
                downloaded += wret;
                gcry_md_write(gctx, ptr, wret);
                ptr += wret;
                ret -= wret;
            }
        } else if (ret < 0) {
            gcry_md_close(gctx);
            goto error;
        } else {
            if (vlc_stream_Eof(stream)) {
                break;
            }
        }
        if (atomic_load(&sys->abort)
            || (backend->owner.cbs->abort != NULL
                && backend->owner.cbs->abort(backend->owner.opaque))) {
            gcry_md_close(gctx);
            goto error;
        }

        size_t p = (size_t)(((float)downloaded / (float)total_size) * 100);
        if (backend->owner.cbs->on_progress != NULL && p > last_progress) {
            backend->owner.cbs->on_progress(backend->owner.opaque,
                                            vlc_stt_ctx_status_downloading,
                                            downloaded, total_size,
                                            model->file_name);
            last_progress = p;
        }
    }

    gcry_md_final(gctx);
    unsigned char *hash = gcry_md_read(gctx, 0);
    char hash_hex[(32 * 2) + 1];
    vlc_hex_encode_binary(hash, 32, hash_hex);
    gcry_md_close(gctx);
    if (strcmp(hash_hex, model->checksum) != 0) {
        msg_Err(backend->obj, "Error wrong hash!");
        goto error;
    }
    vlc_close(fd);
    free(model_path);
    free(buf);
    vlc_stream_Delete(stream);
    return VLC_SUCCESS;

error:
    if (fd != -1) {
        vlc_close(fd);
    }
    if (model_path != NULL) {
        vlc_unlink(model_path);
    }
    free(model_path);
    free(buf);
    vlc_stream_Delete(stream);
    return VLC_EGENERIC;
#else
    msg_Err(backend->obj, "No Gcrypt!");
    VLC_UNUSED(sys);
    VLC_UNUSED(model);
    return VLC_EGENERIC;
#endif
}

#if defined(__APPLE__)
static int
stt_whisper_extract_StreamToFile(struct stt_whisper_backend *backend,
                                 const char *src_url, const char *dest_path,
                                 const char *progress_label)
{
    assert(backend != NULL);
    assert(backend->type == stt_whisper_backend_thread);
    assert(backend->sys != NULL);
    struct stt_whisper_backend_thread *sys = backend->sys;
    assert(src_url != NULL);
    assert(dest_path != NULL);
    assert(progress_label != NULL);

    stream_t *stream = NULL;
    int fd = -1;
    uint8_t *buf = malloc(STT_WHISPER_DL_BUF_SIZE);
    if (buf == NULL) {
        return VLC_ENOMEM;
    }

    stream = vlc_stream_NewMRL(backend->obj, src_url);
    if (stream == NULL) {
        free(buf);
        return VLC_ENOMEM;
    }

    if (stt_whisper_model_CreateSubDirectory(backend->obj, dest_path) != 0) {
        goto error;
    }

    fd = vlc_open(dest_path, O_CREAT | O_WRONLY | O_TRUNC,
                  S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fd < 0) {
        goto error;
    }

    uint64_t total_size = 0;
    if (vlc_stream_GetSize(stream, &total_size) < 0) {
        goto error;
    }

    msg_Dbg(backend->obj, "Start extracting %s(%" PRIu64 "MB) from %s", progress_label,
             total_size / 1000000, src_url);

    int ret = STT_WHISPER_DL_BUF_SIZE;
    size_t extracted = 0;
    size_t last_progress = 0;

    if (backend->owner.cbs->on_progress != NULL) {
        backend->owner.cbs->on_progress(backend->owner.opaque,
                                        vlc_stt_ctx_status_extracting,
                                        extracted, total_size, progress_label);
    }

    while (1) {
        ret = vlc_stream_ReadPartial(stream, buf, STT_WHISPER_DL_BUF_SIZE);
        if (ret > 0) {
            const uint8_t *ptr = buf;
            while (ret > 0) {
                int wret = write(fd, ptr, ret);
                if (wret <= 0) {
                    goto error;
                }
                extracted += wret;
                ptr += wret;
                ret -= wret;
            }
        } else if (ret < 0) {
            goto error;
        } else {
            if (vlc_stream_Eof(stream)) {
                break;
            }
        }
        if (atomic_load(&sys->abort)) {
            goto error;
        }

        size_t p = (size_t)(((float)extracted / (float)total_size) * 100);
        if (backend->owner.cbs->on_progress != NULL && p > last_progress) {
            backend->owner.cbs->on_progress(backend->owner.opaque,
                                            vlc_stt_ctx_status_extracting,
                                            extracted, total_size,
                                            progress_label);
            last_progress = p;
        }
    }

    vlc_close(fd);
    free(buf);
    vlc_stream_Delete(stream);
    return VLC_SUCCESS;

error:
    if (fd != -1) {
        vlc_close(fd);
    }
    free(buf);
    vlc_unlink(dest_path);
    vlc_stream_Delete(stream);
    return VLC_EGENERIC;
}

static bool
stt_whisper_is_safe_zip_entry(const char *name)
{
    if (name == NULL || name[0] == '/' || name[0] == '\\') {
        return false;
    }
    const char *p = name;
    while (*p != '\0') {
        const char *end = strchr(p, DIR_SEP_CHAR);
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if ((len == 1 && p[0] == '.') ||
            (len == 2 && p[0] == '.' && p[1] == '.')) {
            return false;
        }
        p = end ? end + 1 : p + len;
    }
    return true;
}

static int
stt_whisper_model_ExtractRecursive(struct stt_whisper_backend *backend,
                                   input_item_node_t *node, const char *dir,
                                   const char *progress_label)
{
    assert(backend != NULL);
    assert(backend->type == stt_whisper_backend_thread);
    assert(node != NULL);
    assert(dir != NULL);

    char *cur = NULL;
    int ret = VLC_SUCCESS;
    if (node->p_item != NULL) {
        input_item_t *item = node->p_item;
        if (!stt_whisper_is_safe_zip_entry(item->psz_name)) {
            return VLC_EGENERIC;
        }
        if (asprintf(&cur, "%s" DIR_SEP "%s", dir, item->psz_name) < 0) {
            return VLC_ENOMEM;
        }
        if (!memcmp(item->psz_uri, "file://", 7)) {
            ret = stt_whisper_extract_StreamToFile(backend, item->psz_uri, cur,
                                                   progress_label);
            free(cur);
            return ret;
        }
        for (int i = 0; i < node->i_children; i++) {
            ret = stt_whisper_model_ExtractRecursive(backend,
                                                     node->pp_children[i],
                                                     cur, progress_label);
            if (ret != 0) {
                free(cur);
                return ret;
            }
        }
        free(cur);
        return VLC_SUCCESS;
    }
    return VLC_EGENERIC;
}

static int
stt_whisper_model_Extract(struct stt_whisper_backend *backend,
                          const struct stt_whisper_model_base *model)
{
    assert(backend != NULL);
    assert(backend->type == stt_whisper_backend_thread);
    assert(model != NULL);
    assert(model->url != NULL);

    stream_t *stream = vlc_stream_NewURL(backend->obj, model->url);
    if (stream == NULL) {
        return VLC_EGENERIC;
    }
    int ret = vlc_stream_directory_Attach(&stream, "archive", NULL, 0);
    if (ret != VLC_SUCCESS) {
        vlc_stream_Delete(stream);
        return VLC_EGENERIC;
    }

    input_item_t *item = input_item_New(INPUT_ITEM_URI_NOP, _("WHISPER"));
    if (item == NULL) {
        vlc_stream_Delete(stream);
        return VLC_EGENERIC;
    }
    stream->p_input_item = item;
    input_item_node_t *node = input_item_node_Create(stream->p_input_item);
    if (node == NULL) {
        input_item_Release(item);
        vlc_stream_Delete(stream);
        return VLC_EGENERIC;
    }

    ret = vlc_stream_ReadDir(stream, node);
    if (ret == VLC_SUCCESS) {
        if (node->i_children != 1) {
            ret = VLC_EGENERIC;
            goto end;
        }
        char *cache_dir = stt_whisper_model_GetCacheDirPath();
        if (cache_dir == NULL) {
            ret = VLC_EGENERIC;
            goto end;
        }
        ret = stt_whisper_model_ExtractRecursive(backend, node->pp_children[0],
                                                 cache_dir, model->file_name);
        free(cache_dir);
    }
end:
    input_item_node_Delete(node);
    input_item_Release(item);
    vlc_stream_Delete(stream);
    return ret;
}

/* Returns true if the cached extracted directory <coreml_dir_name> exists
 * under cache/models/. */
static bool
stt_whisper_coreml_dir_Stat(const char *coreml_dir_name)
{
    assert(coreml_dir_name != NULL);

    char *cache_dir = stt_whisper_model_GetCacheDirPath();
    if (cache_dir == NULL) {
        return false;
    }
    char *path = NULL;
    if (asprintf(&path, "%s" DIR_SEP "%s", cache_dir, coreml_dir_name) < 0) {
        free(cache_dir);
        return false;
    }
    free(cache_dir);
    struct stat st;
    int ret = vlc_stat(path, &st);
    free(path);
    return ret == 0;
}
#endif

#undef STT_WHISPER_DL_BUF_SIZE

static bool
stt_whisper_model_VerifyCache(struct stt_whisper_backend *backend,
                              const struct stt_whisper_model_base *model)
{
    assert(backend != NULL);
    assert(model != NULL);

    char *path = stt_whisper_model_GetCachePath(model);
    if (path == NULL) {
        return false;
    }
    int ret = stt_whisper_model_VerifyHash(backend, path, model);
    if (ret == -1) {
        vlc_unlink(path);
    }
    free(path);
    return ret == 1;
}

char *
stt_whisper_aux_model_Resolve(struct stt_whisper_backend *backend,
                              const struct stt_whisper_aux_model *model)
{
    assert(backend != NULL);
    assert(model != NULL);

    if (!stt_whisper_model_VerifyCache(backend, &model->file)) {
        if (stt_whisper_model_Download(backend, &model->file) != VLC_SUCCESS) {
            return NULL;
        }
    }
    return stt_whisper_model_GetCachePath(&model->file);
}

static char *
stt_whisper_speech_model_Resolve(struct stt_whisper_backend *backend,
                                 const struct stt_whisper_speech_model *model,
                                 bool want_coreml, bool *coreml_loaded_out)
{
    assert(backend != NULL);
    assert(model != NULL);
    assert(coreml_loaded_out != NULL);

    *coreml_loaded_out = false;

    if (!stt_whisper_model_VerifyCache(backend, &model->file)) {
        if (stt_whisper_model_Download(backend, &model->file) != VLC_SUCCESS) {
            return NULL;
        }
    }

    char *bin_path = stt_whisper_model_GetCachePath(&model->file);
    if (bin_path == NULL) {
        return NULL;
    }

#if defined(__APPLE__)
    if (!want_coreml || model->coreml_zip == NULL) {
        return bin_path;
    }

    if (stt_whisper_coreml_dir_Stat(model->coreml_dir_name)) {
        *coreml_loaded_out = true;
        return bin_path;
    }

    if (!stt_whisper_model_VerifyCache(backend, model->coreml_zip)) {
        if (stt_whisper_model_Download(backend, model->coreml_zip) != VLC_SUCCESS) {
            return bin_path;
        }
    }

    char *zip_path = stt_whisper_model_GetCachePath(model->coreml_zip);
    if (zip_path == NULL) {
        return bin_path;
    }
    char *zip_mrl = NULL;
    if (asprintf(&zip_mrl, "file://%s", zip_path) < 0) {
        free(zip_path);
        return bin_path;
    }
    free(zip_path);

    struct stt_whisper_model_base local_zip = *model->coreml_zip;
    local_zip.url = zip_mrl;
    int ret = stt_whisper_model_Extract(backend, &local_zip);
    free(zip_mrl);
    if (ret == VLC_SUCCESS) {
        *coreml_loaded_out = true;
    }
    return bin_path;
#else
    assert(!want_coreml);
    return bin_path;
#endif
}

struct stt_whisper_loader_ctx {
    int fd;
    uint64_t total_size;
    uint64_t position;
    uint64_t last_progress;
    struct stt_whisper_backend *backend;
    const char *model_name;
};

static size_t
stt_whisper_loader_Read(void *ctx, void *output, size_t read_size)
{
    struct stt_whisper_loader_ctx *lctx = ctx;
    struct stt_whisper_backend *backend = lctx->backend;
    struct stt_whisper_backend_thread *sys = backend->sys;
    uint8_t *out = output;
    size_t total = 0;

    assert(backend->owner.cbs->abort != NULL);

    if (read_size == 0) {
        return 0;
    }

    while (total < read_size) {
        if (atomic_load(&sys->abort)
            || backend->owner.cbs->abort(backend->owner.opaque)) {
            /* whisper.cpp does not always check the read callback's return
             * value, so zero-fill the buffer to prevent a partial read caused
             * by aborts or read errors from exposing uninitialized data. */
            memset(output, 0, read_size);
            break;
        }

        ssize_t n = read(lctx->fd, out + total, read_size - total);
        if (n > 0) {
            total += n;
            lctx->position += n;
            continue;
        }
        if (n == 0) {
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        break;
    }

    uint64_t p = (uint64_t)(((float)lctx->position / (float)lctx->total_size) * 100);
    if (backend->owner.cbs->on_progress != NULL && p > lctx->last_progress) {
        backend->owner.cbs->on_progress(backend->owner.opaque,
                                        vlc_stt_ctx_status_loading,
                                        lctx->position,
                                        lctx->total_size,
                                        lctx->model_name);
        lctx->last_progress = p;
    }
    return total;
}

static bool
stt_whisper_loader_Eof(void *ctx)
{
    struct stt_whisper_loader_ctx *lctx = ctx;
    return lctx->position >= lctx->total_size;
}

static void
stt_whisper_loader_Close(void *ctx)
{
    struct stt_whisper_loader_ctx *lctx = ctx;
    vlc_close(lctx->fd);
}

static struct whisper_context *
stt_whisper_WhisperLoader(struct stt_whisper_backend *backend,
                          const char *model_path, const char *model_name,
                          bool coreml)
{
    assert(backend != NULL);
    assert(model_path != NULL);
    assert(model_name != NULL);

    const struct whisper_context_params ctxparams = {
        .use_gpu = true,
    };
    /* CoreML loads from a filesystem path derived from ctx->path_model,
     * which is only set by the file-based API, not the custom loader. */
    if (coreml) {
        if (backend->owner.cbs->on_progress != NULL) {
            backend->owner.cbs->on_progress(backend->owner.opaque,
                                            vlc_stt_ctx_status_loading,
                                            0, -1, model_name);
        }
        struct whisper_context *ctx = whisper_init_from_file_with_params(model_path, ctxparams);
        if (ctx != NULL && backend->owner.cbs->on_progress != NULL) {
            backend->owner.cbs->on_progress(backend->owner.opaque,
                                            vlc_stt_ctx_status_loading,
                                            1, 1, model_name);
        }
        return ctx;
    }

    int fd = vlc_open(model_path, O_RDONLY);
    if (fd < 0) {
        return NULL;
    }

    struct stat st;
    if (fstat(fd, &st) != 0) {
        vlc_close(fd);
        return NULL;
    }

    struct stt_whisper_loader_ctx lctx = {
        .fd = fd,
        .total_size = (uint64_t)st.st_size,
        .position = 0,
        .last_progress = 0,
        .backend = backend,
        .model_name = model_name,
    };

    if (backend->owner.cbs->on_progress != NULL) {
        backend->owner.cbs->on_progress(backend->owner.opaque,
                                        vlc_stt_ctx_status_loading,
                                        0, (int64_t)st.st_size, model_name);
    }

    struct whisper_model_loader loader = {
        .context = &lctx,
        .read = stt_whisper_loader_Read,
        .eof = stt_whisper_loader_Eof,
        .close = stt_whisper_loader_Close,
    };

    /* whisper calls loader.close() internally before returning */
    return whisper_init_with_params(&loader, ctxparams);
}

static int
stt_whisper_LoadSpeech(struct stt_whisper_backend *backend)
{
    assert(backend != NULL);
    assert(backend->type == stt_whisper_backend_thread);
    struct stt_whisper_backend_thread *sys = backend->sys;

    vlc_fifo_Lock(sys->fifo);
    char *desired =sys->params.model_name ? strdup(sys->params.model_name) : NULL;
    vlc_fifo_Unlock(sys->fifo);

    if (desired == NULL) {
        return VLC_EGENERIC;
    }

    if (sys->whisper_ctx != NULL && sys->loaded_model_name != NULL
        && strcmp(sys->loaded_model_name, desired) == 0) {
        free(desired);
        return VLC_SUCCESS;
    }

    if (sys->vad_ctx != NULL) {
        whisper_vad_free(sys->vad_ctx);
        sys->vad_ctx = NULL;
    }
    if (sys->whisper_ctx != NULL) {
        whisper_free(sys->whisper_ctx);
        sys->whisper_ctx = NULL;
    }
    free(sys->loaded_model_name);
    sys->loaded_model_name = NULL;

    const struct stt_whisper_speech_model *model =
        stt_whisper_speech_model_Find(desired);
    if (model == NULL) {
        msg_Err(backend->obj, "No models named `%s'!", desired);
        free(desired);
        return VLC_EGENERIC;
    }

    const char *info = whisper_print_system_info();
    msg_Dbg(backend->obj, "%s", info);
    bool want_coreml = strstr(info, "COREML = 1") != NULL;

    bool coreml_loaded = false;
    char *bin_path = stt_whisper_speech_model_Resolve(backend, model,
                                                      want_coreml,
                                                      &coreml_loaded);
    if (bin_path == NULL) {
        free(desired);
        return VLC_EGENERIC;
    }

    struct whisper_context *whisper_ctx =
        stt_whisper_WhisperLoader(backend, bin_path, desired, coreml_loaded);
    free(bin_path);
    if (whisper_ctx == NULL) {
        msg_Err(backend->obj,
                "Whisper context isn't load properly speech-to-text deactivated.");
        free(desired);
        return VLC_EGENERIC;
    }

    /* Openvino not supported yet so this function has not effect */
    whisper_ctx_init_openvino_encoder(whisper_ctx, NULL, "GPU", NULL);

    msg_Dbg(backend->obj, "Loading vad model");
    struct whisper_vad_context_params vadparams = {
        .n_threads = 1,
        .use_gpu = false,
        .gpu_device = 0,
    };
    struct whisper_vad_context *vad_ctx = NULL;
    const struct stt_whisper_aux_model *vad_model = NULL;
    for (size_t i = 0; i < ARRAY_SIZE(whisper_vad_models); i++) {
        if (strcmp(whisper_vad_models[i].nickname, "silero") == 0) {
            vad_model = &whisper_vad_models[i];
            break;
        }
    }
    if (vad_model != NULL) {
        char *vad_path = stt_whisper_aux_model_Resolve(backend, vad_model);
        if (vad_path != NULL) {
            vad_ctx = whisper_vad_init_from_file_with_params(vad_path, vadparams);
            whisper_vad_set_context(whisper_ctx, vad_ctx);
            free(vad_path);
        }
    }

    sys->whisper_ctx = whisper_ctx;
    sys->vad_ctx = vad_ctx;
    sys->loaded_model_name = desired;
    return VLC_SUCCESS;
}

void
stt_whisper_Load(struct stt_whisper_backend *backend)
{
    assert(backend != NULL);
    assert(backend->type == stt_whisper_backend_thread);
    assert(backend->sys != NULL);
    struct stt_whisper_backend_thread *sys = backend->sys;

    bool first_load = (sys->whisper_ctx == NULL);

    if (first_load) {
        llama_backend_init();
    }

    int sret = stt_whisper_LoadSpeech(backend);
    (void)stt_whisper_translate_Load(backend);

    if (first_load) {
        backend->owner.cbs->ready(backend->owner.opaque,
                                  sret == VLC_SUCCESS
                                      ? vlc_stt_ctx_status_done
                                      : vlc_stt_ctx_status_error);
    }
}
