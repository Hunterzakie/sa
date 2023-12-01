// SPDX-License-Identifier: LGPL-2.1-or-later
/*****************************************************************************
 * protocol.c: Internal rpc/ipc protocol for whisper implementation
 *****************************************************************************
 * Copyright © 2025 Videolabs, VideoLAN and VLC authors
 *
 * Authors: Gabriel Lafond Thenaille <gabriel@videolabs.io>
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <assert.h>

#include <vlc_common.h>
#include <vlc_stt.h>
#include <vlc_frame.h>

#include "whisper_protocol.h"

enum stt_whisper_command_type {
    stt_whisper_command_addAudio = 0x01,
    stt_whisper_command_drain = 0x02,
    stt_whisper_command_flush = 0x03,
    stt_whisper_command_setParams = 0x04,
    stt_whisper_command_close = 0x05,
    stt_whisper_command_start = 0x06,
    stt_whisper_command_stop = 0x07,
    stt_whisper_command_confirmReply = 0x08,
    stt_whisper_command_abort = 0x09,

    stt_whisper_command_ready = 0x81,
    stt_whisper_command_sendSubtitle = 0x82,
    stt_whisper_command_onProgress = 0x83,
    stt_whisper_command_confirm = 0x84,
    stt_whisper_command_log = 0x85,

    stt_whisper_command_invalid = 0xff,
};

#define STT_WHISPER_COMMAND_HEADER_SIZE 16
#define STT_WHISPER_MAX_MSG_SIZE (64 * 1024 * 1024)

/**
 * deserialize functions
 */

static void
serialize_int(uint64_t n, size_t nbytes, uint8_t **out, size_t *size)
{
    assert(out != NULL);
    assert(size != NULL);
    assert(*size >= nbytes);
    assert(nbytes <= 8);

    for (size_t i = 0; i < nbytes; i++) {
        (*out)[i] = n & 0xff;
        n >>= 8;
    }
    *out += nbytes;
    *size -= nbytes;
}


static void
serialize_padding(size_t nbytes, uint8_t **out, size_t *size)
{
    assert(out != NULL);
    assert(size != NULL);
    assert(*size >= nbytes);

    for (size_t i = 0; i < nbytes; i++) {
        (*out)[i] = 0;
    }
    *out += nbytes;
    *size -= nbytes;
}

static void
serialize_buffer(const uint8_t *buffer, size_t nbytes, uint8_t **out,
                 size_t *size)
{
    assert(out != NULL);
    assert(size != NULL);
    assert(*size >= nbytes);
    if (nbytes == 0) {
        assert(buffer != NULL);
    }

    memcpy(*out, buffer, nbytes);
    *out += nbytes;
    *size -= nbytes;
}

static void
serialize_string(const char *str, uint8_t **out,
                 size_t *size)
{
    assert(out != NULL);
    assert(size != NULL);
    assert(str != NULL);

    size_t len = strlen(str) + 1;

    assert(*size >= len);

    memcpy(*out, str, len);
    *out += len;
    *size -= len;
}

/**
 * deserialize functions
 */

static uint64_t
deserialize_int(size_t nbytes, const uint8_t **in, size_t *size)
{
    assert(in != NULL);
    assert(size != NULL);
    assert(*size >= nbytes);
    assert(nbytes <= 8);

    uint64_t n = 0;
    for (size_t i = 0; i < nbytes; i++) {
        uint64_t b = (*in)[i];
        n |= b << (i * 8);
    }
    *in += nbytes;
    *size -= nbytes;
    return n;
}
#define deserialize_int_cast(type, nbytes, in, size) \
    ((type)(deserialize_int(nbytes, in, size)))

static void
deserialize_skip_padding(size_t nbytes, const uint8_t **in, size_t *size)
{
    assert(in != NULL);
    assert(size != NULL);
    assert(*size >= nbytes);
    *in += nbytes;
    *size -= nbytes;
}

static void
deserialize_buffer(uint8_t *buffer, size_t nbytes, const uint8_t **in,
                   size_t *size)
{
    assert(in != NULL);
    assert(size != NULL);
    assert(*size >= nbytes);
    if (nbytes == 0) {
        assert(buffer != NULL);
    }

    memcpy(buffer, *in, nbytes);
    *in += nbytes;
    *size -= nbytes;
}

static char *
deserialize_string(const uint8_t **in, size_t *size)
{
    assert(in != NULL);
    assert(size != NULL);

    const uint8_t *nul = memchr(*in, '\0', *size);
    if (nul == NULL) {
        return NULL;
    }

    size_t len = nul - *in;
    char *str = strndup((const char *)(*in), len);
    if (str == NULL) {
        return NULL;
    }
    *in += len + 1;
    *size -= len + 1;
    return str;
}

/**
 * Internal struct
 */

struct stt_whisper_command_header {
    enum stt_whisper_command_type command;
    enum vlc_stt_ctx_status status;
    size_t size;
};

struct stt_whisper_command_recv {
    atomic_bool abort;

    struct stt_whisper_backend *backend;
    ssize_t (*recv)(void *, uint8_t *, size_t);
    void *opaque;
    /* Output channel, used to answer requests (e.g. ConfirmReply). May be
     * NULL on a receive-only endpoint. */
    ssize_t (*send)(void *, const uint8_t *, size_t);
    void *send_opaque;
};

static int
stt_whisper_command_Recv(uint8_t *buf, size_t size,
                         ssize_t (*recv)(void *, uint8_t *, size_t),
                         void *opaque)
{
    assert(buf != NULL);
    assert(recv != NULL);

    int ret = 0;
    while (size != 0) {
        ret = recv(opaque, buf, size);
        if (ret <= 0) {
            break;
        }
        assert(size >= (size_t)ret);
        size -= (size_t)ret;
        buf += ret;
    }

    return ret <= 0 ? VLC_EGENERIC : VLC_SUCCESS;
}

static int
stt_whisper_command_Send(const uint8_t *buf, size_t size,
                         ssize_t (*send)(void *, const uint8_t *, size_t),
                         void *opaque)
{
    assert(buf != NULL);
    assert(send != NULL);

    int ret = 0;
    while (size != 0) {
        ret = send(opaque, buf, size);
        if (ret < 0) {
            return ret;
        } else if (ret == 0) {
            return VLC_EGENERIC;
        }
        assert(size >= (size_t)ret);
        size -= (size_t)ret;
        buf += ret;
    }

    return VLC_SUCCESS;
}

/**
 * Header receive function
 */

static int
stt_whisper_command_header_Recv(struct stt_whisper_command_recv *cmd_recv,
                                struct stt_whisper_command_header *hdr)
{
    assert(cmd_recv != NULL);
    assert(hdr != NULL);
    assert(cmd_recv->recv != NULL);

    uint8_t header[STT_WHISPER_COMMAND_HEADER_SIZE] = {0};
    size_t header_size = STT_WHISPER_COMMAND_HEADER_SIZE;
    int ret = stt_whisper_command_Recv(header, header_size,
                                       cmd_recv->recv, cmd_recv->opaque);
    if (ret != VLC_SUCCESS) {
        return ret;
    }

    const uint8_t *ptr = header;

    hdr->command = deserialize_int(1, &ptr, &header_size);
    hdr->status = deserialize_int(1, &ptr, &header_size);
    deserialize_skip_padding(6, &ptr, &header_size);
    hdr->size = deserialize_int_cast(size_t, 8, &ptr, &header_size);

    return VLC_SUCCESS;
}

/**
 * Header serialize function
 */

static void
stt_whisper_command_header_Serialize(enum stt_whisper_command_type type,
                                     enum vlc_stt_ctx_status status,
                                     size_t msg_size, uint8_t *out,
                                     size_t size)
{
    assert(out != NULL);
    assert(size >= STT_WHISPER_COMMAND_HEADER_SIZE);

    serialize_int(type, 1, &out, &size);
    serialize_int(status, 1, &out, &size);
    serialize_padding(6, &out, &size);
    serialize_int(msg_size, 8, &out, &size);
    assert(size == 0);
}


/**
 * Send functions
 */

int
stt_whisper_command_send_AddAudio(vlc_frame_t *audio,
                                  ssize_t (*send)(void *, const uint8_t *, size_t),
                                  void *opaque)
{
    assert(audio != NULL);

    size_t hdr_size = STT_WHISPER_COMMAND_HEADER_SIZE;
    size_t msg_size = 24 /* add_audio header */ + audio->i_buffer;
    size_t total_size = hdr_size + msg_size;
    uint8_t *buf = malloc(sizeof(*buf) * (total_size));
    if (buf == NULL) {
        return VLC_ENOMEM;
    }
    uint8_t *ptr = buf;

    stt_whisper_command_header_Serialize(stt_whisper_command_addAudio,
                                         vlc_stt_ctx_status_done,
                                         msg_size, ptr, hdr_size);
    ptr += hdr_size;

    serialize_int(audio->i_pts, 8, &ptr, &msg_size);
    serialize_int(audio->i_length, 8, &ptr, &msg_size);
    serialize_int(audio->i_buffer, 8, &ptr, &msg_size);
    serialize_buffer(audio->p_buffer, audio->i_buffer, &ptr, &msg_size);
    assert(msg_size == 0);

    int ret = stt_whisper_command_Send(buf, total_size, send, opaque);
    free(buf);
    return ret;
}

int
stt_whisper_command_send_Drain(vlc_frame_t *audio,
                               ssize_t (*send)(void *, const uint8_t *, size_t),
                               void *opaque)
{

    size_t hdr_size = STT_WHISPER_COMMAND_HEADER_SIZE;
    size_t msg_size = 0;
    if (audio != NULL) {
        msg_size = 24 /* drain header */ + audio->i_buffer;
    }
    size_t total_size = hdr_size + msg_size;
    uint8_t *buf = malloc(sizeof(*buf) * (total_size));
    if (buf == NULL) {
        return VLC_ENOMEM;
    }
    uint8_t *ptr = buf;

    stt_whisper_command_header_Serialize(stt_whisper_command_drain,
                                         vlc_stt_ctx_status_done,
                                         msg_size, ptr, hdr_size);
    ptr += hdr_size;

    if (msg_size != 0) {
        serialize_int(audio->i_pts, 8, &ptr, &msg_size);
        serialize_int(audio->i_length, 8, &ptr, &msg_size);
        serialize_int(audio->i_buffer, 8, &ptr, &msg_size);
        serialize_buffer(audio->p_buffer, audio->i_buffer, &ptr, &msg_size);
        assert(msg_size == 0);
    }

    int ret = stt_whisper_command_Send(buf, total_size, send, opaque);
    free(buf);
    return ret;
}
 
int
stt_whisper_command_send_Flush(ssize_t (*send)(void *, const uint8_t *, size_t),
                               void *opaque)
{
    size_t hdr_size = STT_WHISPER_COMMAND_HEADER_SIZE;
    uint8_t buf[STT_WHISPER_COMMAND_HEADER_SIZE];

    stt_whisper_command_header_Serialize(stt_whisper_command_flush,
                                         vlc_stt_ctx_status_done,
                                         0, buf, hdr_size);

    int ret = stt_whisper_command_Send(buf, hdr_size, send, opaque);
    return ret;
}
 
int
stt_whisper_command_send_SetParams(const struct stt_whisper_params *params,
                                   ssize_t (*send)(void *, const uint8_t *, size_t),
                                   void *opaque)
{
    assert(params != NULL);

    assert(params->language != NULL);
    size_t lang_len = strlen(params->language) + 1;

    assert(params->model_name != NULL);
    size_t model_len = strlen(params->model_name) + 1;

    size_t hdr_size = STT_WHISPER_COMMAND_HEADER_SIZE;
    size_t msg_size = 16 + lang_len + model_len;
    size_t total_size = hdr_size + msg_size;
    uint8_t *buf = malloc(sizeof(*buf) * (total_size));
    if (buf == NULL) {
        return VLC_ENOMEM;
    }
    uint8_t *ptr = buf;

    stt_whisper_command_header_Serialize(stt_whisper_command_setParams,
                                         vlc_stt_ctx_status_done,
                                         msg_size, ptr, hdr_size);
    ptr += hdr_size;

    serialize_int(params->length_ms, 8, &ptr, &msg_size);
    serialize_int(params->n_threads, 4, &ptr, &msg_size);
    serialize_int(params->translation_index ,4, &ptr, &msg_size);
    serialize_string(params->language, &ptr, &msg_size);
    serialize_string(params->model_name, &ptr, &msg_size);
    assert(msg_size == 0);

    int ret = stt_whisper_command_Send(buf, total_size, send, opaque);
    free(buf);
    return ret;
}

int
stt_whisper_command_send_Close(ssize_t (*send)(void *, const uint8_t *, size_t),
                               void *opaque)
{
    size_t hdr_size = STT_WHISPER_COMMAND_HEADER_SIZE;
    uint8_t buf[STT_WHISPER_COMMAND_HEADER_SIZE];

    stt_whisper_command_header_Serialize(stt_whisper_command_close,
                                         vlc_stt_ctx_status_done,
                                         0, buf, hdr_size);

    int ret = stt_whisper_command_Send(buf, hdr_size, send, opaque);
    return ret;
}

int
stt_whisper_command_send_Start(ssize_t (*send)(void *, const uint8_t *, size_t),
                               void *opaque)
{
    size_t hdr_size = STT_WHISPER_COMMAND_HEADER_SIZE;
    uint8_t buf[STT_WHISPER_COMMAND_HEADER_SIZE];

    stt_whisper_command_header_Serialize(stt_whisper_command_start,
                                         vlc_stt_ctx_status_done,
                                         0, buf, hdr_size);

    return stt_whisper_command_Send(buf, hdr_size, send, opaque);
}

int
stt_whisper_command_send_Stop(ssize_t (*send)(void *, const uint8_t *, size_t),
                              void *opaque)
{
    size_t hdr_size = STT_WHISPER_COMMAND_HEADER_SIZE;
    uint8_t buf[STT_WHISPER_COMMAND_HEADER_SIZE];

    stt_whisper_command_header_Serialize(stt_whisper_command_stop,
                                         vlc_stt_ctx_status_done,
                                         0, buf, hdr_size);

    return stt_whisper_command_Send(buf, hdr_size, send, opaque);
}


int
stt_whisper_command_send_Ready(enum vlc_stt_ctx_status status,
                               ssize_t (*send)(void *, const uint8_t *, size_t),
                               void *opaque)
{
    size_t hdr_size = STT_WHISPER_COMMAND_HEADER_SIZE;
    uint8_t buf[STT_WHISPER_COMMAND_HEADER_SIZE];

    stt_whisper_command_header_Serialize(stt_whisper_command_ready,
                                         status, 0, buf, hdr_size);

    int ret = stt_whisper_command_Send(buf, hdr_size, send, opaque);
    return ret;
}
 
int
stt_whisper_command_send_SendSubtitle(vlc_frame_t *sub,
                                      ssize_t (*send)(void *, const uint8_t *, size_t),
                                      void *opaque)
{
    assert(sub != NULL);

    size_t hdr_size = STT_WHISPER_COMMAND_HEADER_SIZE;
    size_t msg_size = 24 /* send_sub header */ + sub->i_buffer;
    size_t total_size = hdr_size + msg_size;
    uint8_t *buf = malloc(sizeof(*buf) * (total_size));
    if (buf == NULL) {
        return VLC_ENOMEM;
    }
    uint8_t *ptr = buf;

    stt_whisper_command_header_Serialize(stt_whisper_command_sendSubtitle,
                                         vlc_stt_ctx_status_done,
                                         msg_size, ptr, hdr_size);
    ptr += hdr_size;

    serialize_int(sub->i_pts, 8, &ptr, &msg_size);
    serialize_int(sub->i_length, 8, &ptr, &msg_size);
    serialize_int(sub->i_buffer, 8, &ptr, &msg_size);
    serialize_buffer(sub->p_buffer, sub->i_buffer, &ptr, &msg_size);
    assert(msg_size == 0);

    int ret = stt_whisper_command_Send(buf, total_size, send, opaque);
    free(buf);
    return ret;
}
 
int
stt_whisper_command_send_OnProgress(enum vlc_stt_ctx_status status,
                                    int64_t pos, int64_t total,
                                    const char *description,
                                    ssize_t (*send)(void *, const uint8_t *, size_t),
                                    void *opaque)
{
    assert(description != NULL);

    size_t hdr_size = STT_WHISPER_COMMAND_HEADER_SIZE;
    size_t desc_len = strlen(description) + 1;
    size_t msg_size = 16 /* pos + total */ + desc_len;
    size_t total_size = hdr_size + msg_size;
    uint8_t *buf = malloc(sizeof(*buf) * (total_size));
    if (buf == NULL) {
        return VLC_ENOMEM;
    }
    uint8_t *ptr = buf;

    stt_whisper_command_header_Serialize(stt_whisper_command_onProgress,
                                         status,
                                         msg_size, ptr, hdr_size);
    ptr += hdr_size;

    serialize_int(pos, 8, &ptr, &msg_size);
    serialize_int(total, 8, &ptr, &msg_size);
    serialize_string(description, &ptr, &msg_size);
    assert(msg_size == 0);

    int ret = stt_whisper_command_Send(buf, total_size, send, opaque);
    free(buf);
    return ret;
}

int
stt_whisper_command_send_Confirm(uint64_t download_size, const char *path,
                                 ssize_t (*send)(void *, const uint8_t *, size_t),
                                 void *opaque)
{
    assert(path != NULL);

    size_t hdr_size = STT_WHISPER_COMMAND_HEADER_SIZE;
    size_t path_len = strlen(path) + 1;
    size_t msg_size = 8 /* download size */ + path_len;
    size_t total_size = hdr_size + msg_size;
    uint8_t *buf = malloc(sizeof(*buf) * (total_size));
    if (buf == NULL) {
        return VLC_ENOMEM;
    }
    uint8_t *ptr = buf;

    stt_whisper_command_header_Serialize(stt_whisper_command_confirm,
                                         vlc_stt_ctx_status_downloading,
                                         msg_size, ptr, hdr_size);
    ptr += hdr_size;

    serialize_int(download_size, 8, &ptr, &msg_size);
    serialize_string(path, &ptr, &msg_size);
    assert(msg_size == 0);

    int ret = stt_whisper_command_Send(buf, total_size, send, opaque);
    free(buf);
    return ret;
}

int
stt_whisper_command_send_ConfirmReply(bool accepted,
                                      ssize_t (*send)(void *, const uint8_t *, size_t),
                                      void *opaque)
{
    size_t hdr_size = STT_WHISPER_COMMAND_HEADER_SIZE;
    size_t msg_size = 1;
    uint8_t buf[STT_WHISPER_COMMAND_HEADER_SIZE + 1];
    uint8_t *ptr = buf;

    stt_whisper_command_header_Serialize(stt_whisper_command_confirmReply,
                                         vlc_stt_ctx_status_done,
                                         msg_size, ptr, hdr_size);
    ptr += hdr_size;

    serialize_int(accepted ? 1 : 0, 1, &ptr, &msg_size);
    assert(msg_size == 0);

    return stt_whisper_command_Send(buf, hdr_size + 1, send, opaque);
}

int
stt_whisper_command_send_Abort(ssize_t (*send)(void *, const uint8_t *, size_t),
                               void *opaque)
{
    size_t hdr_size = STT_WHISPER_COMMAND_HEADER_SIZE;
    uint8_t buf[STT_WHISPER_COMMAND_HEADER_SIZE];

    stt_whisper_command_header_Serialize(stt_whisper_command_abort,
                                         vlc_stt_ctx_status_done,
                                         0, buf, hdr_size);

    return stt_whisper_command_Send(buf, hdr_size, send, opaque);
}

int
stt_whisper_command_send_Log(int type, const char *module, const char *file,
                             unsigned line, const char *msg,
                             ssize_t (*send)(void *, const uint8_t *, size_t),
                             void *opaque)
{
    assert(msg != NULL);

    if (module == NULL) {
        module = "";
    }
    if (file == NULL) {
        file = "";
    }

    size_t hdr_size = STT_WHISPER_COMMAND_HEADER_SIZE;
    size_t msg_size = 8 /* type + line */ + strlen(module) + 1
                    + strlen(file) + 1 + strlen(msg) + 1;
    size_t total_size = hdr_size + msg_size;
    uint8_t *buf = malloc(sizeof(*buf) * (total_size));
    if (buf == NULL) {
        return VLC_ENOMEM;
    }
    uint8_t *ptr = buf;

    stt_whisper_command_header_Serialize(stt_whisper_command_log,
                                         vlc_stt_ctx_status_done,
                                         msg_size, ptr, hdr_size);
    ptr += hdr_size;

    serialize_int(type, 4, &ptr, &msg_size);
    serialize_int(line, 4, &ptr, &msg_size);
    serialize_string(module, &ptr, &msg_size);
    serialize_string(file, &ptr, &msg_size);
    serialize_string(msg, &ptr, &msg_size);
    assert(msg_size == 0);

    int ret = stt_whisper_command_Send(buf, total_size, send, opaque);
    free(buf);
    return ret;
}

/**
 * Receive functions
 */

static vlc_frame_t *
stt_whisper_command_recv_Frame(const uint8_t *buf, size_t size)
{
    assert(buf != NULL);
    if (size < 24) {
        return NULL;
    }

    vlc_tick_t pts = deserialize_int_cast(vlc_tick_t, 8, &buf, &size);
    vlc_tick_t length = deserialize_int_cast(vlc_tick_t, 8, &buf, &size);
    uint64_t buf_size_raw = deserialize_int_cast(uint64_t, 8, &buf, &size);
    if (buf_size_raw != size) {
        return NULL;
    }
    size_t buf_size = (size_t)buf_size_raw;
    vlc_frame_t *frame = vlc_frame_Alloc(buf_size);
    if (frame == NULL) {
        return NULL;
    }
    deserialize_buffer(frame->p_buffer, frame->i_buffer, &buf, &size);
    frame->i_pts = pts;
    frame->i_length = length;
    return frame;
}

/**
 * Operations trigger functions
 */

static void
stt_whisper_command_recv_AddAudio(struct stt_whisper_command_recv *cmd_recv,
                                  const uint8_t *buf, size_t size)
{
    assert(cmd_recv != NULL);
    assert(cmd_recv->backend != NULL);

    vlc_frame_t *audio = stt_whisper_command_recv_Frame(buf, size);
    if (audio == NULL) {
        return;
    }

    struct stt_whisper_backend *backend = cmd_recv->backend;
    assert(backend->ops != NULL);
    assert(backend->ops->add_audio != NULL);
    backend->ops->add_audio(backend, audio);
}

static void
stt_whisper_command_recv_Drain(struct stt_whisper_command_recv *cmd_recv,
                               const uint8_t *buf, size_t size)
{
    assert(cmd_recv != NULL);
    assert(cmd_recv->backend != NULL);

    vlc_frame_t *audio = NULL;
    if (size != 0) {
        audio = stt_whisper_command_recv_Frame(buf, size);
        if (audio == NULL) {
            return;
        }
    }

    struct stt_whisper_backend *backend = cmd_recv->backend;
    assert(backend->ops != NULL);
    assert(backend->ops->drain != NULL);
    backend->ops->drain(backend, audio);
}

static void
stt_whisper_command_recv_Flush(struct stt_whisper_command_recv *cmd_recv)
{
    assert(cmd_recv != NULL);
    assert(cmd_recv->backend != NULL);
    struct stt_whisper_backend *backend = cmd_recv->backend;
    assert(backend->ops != NULL);
    assert(backend->ops->flush != NULL);
    backend->ops->flush(backend);
}

static void
stt_whisper_command_recv_SetParams(struct stt_whisper_command_recv *cmd_recv,
                                   const uint8_t *buf, size_t size)
{
    assert(cmd_recv != NULL);
    assert(cmd_recv->backend != NULL);
    if (size < 16) {
        return;
    }

    struct stt_whisper_params params;
    params.length_ms = deserialize_int_cast(vlc_tick_t, 8, &buf, &size);
    params.n_threads = deserialize_int_cast(int, 4, &buf, &size);
    params.translation_index = deserialize_int_cast(int, 4, &buf, &size);
    params.language = deserialize_string(&buf, &size);
    if (params.language == NULL) {
        return;
    }
    params.model_name = deserialize_string(&buf, &size);
    if (params.model_name == NULL) {
        free(params.language);
        return;
    }

    struct stt_whisper_backend *backend = cmd_recv->backend;
    assert(backend->ops != NULL);
    assert(backend->ops->set_params != NULL);
    backend->ops->set_params(backend, &params);
    free(params.language);
    free(params.model_name);
}

static void
stt_whisper_command_recv_Close(struct stt_whisper_command_recv *cmd_recv)
{
    assert(cmd_recv != NULL);
    assert(cmd_recv->backend != NULL);


    struct stt_whisper_backend *backend = cmd_recv->backend;
    assert(backend->ops != NULL);
    assert(backend->ops->close != NULL);
    backend->ops->close(backend);
}

static void
stt_whisper_command_recv_Start(struct stt_whisper_command_recv *cmd_recv)
{
    assert(cmd_recv != NULL);
    assert(cmd_recv->backend != NULL);
    struct stt_whisper_backend *backend = cmd_recv->backend;
    assert(backend->ops != NULL);
    assert(backend->ops->start != NULL);
    backend->ops->start(backend);
}

static void
stt_whisper_command_recv_Stop(struct stt_whisper_command_recv *cmd_recv)
{
    assert(cmd_recv != NULL);
    assert(cmd_recv->backend != NULL);
    struct stt_whisper_backend *backend = cmd_recv->backend;
    assert(backend->ops != NULL);
    assert(backend->ops->stop != NULL);
    backend->ops->stop(backend);
}

/**
 * Callbacks trigger functions
 */

static void
stt_whisper_command_recv_Ready(struct stt_whisper_command_recv *cmd_recv,
                               enum vlc_stt_ctx_status status)
{
    assert(cmd_recv != NULL);
    assert(cmd_recv->backend != NULL);
    struct stt_whisper_backend *backend = cmd_recv->backend;
    assert(backend->owner.cbs != NULL);
    assert(backend->owner.cbs->ready != NULL);
    backend->owner.cbs->ready(backend->owner.opaque, status);
}

static void
stt_whisper_command_recv_SendSubtitle(struct stt_whisper_command_recv *cmd_recv,
                                      const uint8_t *buf, size_t size)
{
    assert(cmd_recv != NULL);
    assert(cmd_recv->backend != NULL);

    vlc_frame_t *sub = stt_whisper_command_recv_Frame(buf, size);
    if (sub == NULL) {
        return;
    }

    struct stt_whisper_backend *backend = cmd_recv->backend;
    assert(backend->owner.cbs != NULL);
    assert(backend->owner.cbs->send_subtitle != NULL);
    backend->owner.cbs->send_subtitle(backend->owner.opaque, sub);
}

static void
stt_whisper_command_recv_OnProgress(struct stt_whisper_command_recv *cmd_recv,
                                    enum vlc_stt_ctx_status status,
                                    const uint8_t *buf, size_t size)
{
    assert(cmd_recv != NULL);
    assert(cmd_recv->backend != NULL);

    if (size < 16) {
        return;
    }
    int64_t pos = deserialize_int_cast(int64_t, 8, &buf, &size);
    int64_t total = deserialize_int_cast(int64_t, 8, &buf, &size);
    char *description = deserialize_string(&buf, &size);

    struct stt_whisper_backend *backend = cmd_recv->backend;
    assert(backend->owner.cbs != NULL);
    if (backend->owner.cbs->on_progress != NULL) {
        backend->owner.cbs->on_progress(backend->owner.opaque, status, pos,
                                        total,
                                        description ? description : "");
    }
    free(description);
}

/* Host side: a model-download confirmation was requested by the whisper
 * process. Ask the owner and send the answer back. */
static void
stt_whisper_command_recv_Confirm(struct stt_whisper_command_recv *cmd_recv,
                                 const uint8_t *buf, size_t size)
{
    assert(cmd_recv != NULL);
    assert(cmd_recv->backend != NULL);

    if (size < 8) {
        return;
    }
    uint64_t download_size = deserialize_int_cast(uint64_t, 8, &buf, &size);
    char *path = deserialize_string(&buf, &size);

    struct stt_whisper_backend *backend = cmd_recv->backend;
    assert(backend->owner.cbs != NULL);
    bool accepted = true;
    if (backend->owner.cbs->confirm != NULL) {
        accepted = backend->owner.cbs->confirm(backend->owner.opaque,
                                               download_size,
                                               path != NULL ? path : "");
    }
    free(path);

    if (cmd_recv->send != NULL) {
        stt_whisper_command_send_ConfirmReply(accepted, cmd_recv->send,
                                              cmd_recv->send_opaque);
    }
}

/* Host side: re-emit a log message of the whisper process. */
static void
stt_whisper_command_recv_Log(struct stt_whisper_command_recv *cmd_recv,
                             const uint8_t *buf, size_t size)
{
    assert(cmd_recv != NULL);
    assert(cmd_recv->backend != NULL);

    if (size < 8) {
        return;
    }
    int type = deserialize_int_cast(int32_t, 4, &buf, &size);
    unsigned line = deserialize_int_cast(uint32_t, 4, &buf, &size);
    char *module = deserialize_string(&buf, &size);
    char *file = module != NULL ? deserialize_string(&buf, &size) : NULL;
    char *msg = file != NULL ? deserialize_string(&buf, &size) : NULL;

    if (msg != NULL) {
        vlc_object_Log(cmd_recv->backend->obj, type,
                       module[0] != '\0' ? module : "vlc-whisper",
                       file[0] != '\0' ? file : NULL, line, NULL,
                       "%s", msg);
    }
    free(msg);
    free(file);
    free(module);
}

/* Process side: the host answered a pending Confirm request. */
static void
stt_whisper_command_recv_ConfirmReply(struct stt_whisper_command_recv *cmd_recv,
                                      const uint8_t *buf, size_t size)
{
    assert(cmd_recv != NULL);
    assert(cmd_recv->backend != NULL);

    if (size < 1) {
        return;
    }
    bool accepted = deserialize_int_cast(uint8_t, 1, &buf, &size) != 0;

    struct stt_whisper_backend *backend = cmd_recv->backend;
    assert(backend->owner.cbs != NULL);
    if (backend->owner.cbs->confirm_reply != NULL) {
        backend->owner.cbs->confirm_reply(backend->owner.opaque, accepted);
    }
}

/* Process side: the host asked to abort the in-progress load. */
static void
stt_whisper_command_recv_Abort(struct stt_whisper_command_recv *cmd_recv)
{
    assert(cmd_recv != NULL);
    assert(cmd_recv->backend != NULL);

    struct stt_whisper_backend *backend = cmd_recv->backend;
    assert(backend->ops != NULL);
    if (backend->ops->request_abort != NULL) {
        backend->ops->request_abort(backend);
    }
}

/**
 * Command receive loop
 */

int
stt_whisper_command_RecvLoop(struct stt_whisper_backend *backend,
                             ssize_t (*recv)(void *, uint8_t *, size_t),
                             void *recv_opaque,
                             ssize_t (*send)(void *, const uint8_t *, size_t),
                             void *send_opaque)
{
    uint8_t *buf = NULL;
    size_t buf_size = 0;

    assert(backend != NULL);
    assert(recv != NULL);

    struct stt_whisper_command_recv cmd_recv = {
        .backend = backend,
        .recv = recv,
        .opaque = recv_opaque,
        .send = send,
        .send_opaque = send_opaque,
    };

    int status = VLC_SUCCESS;
    bool stop = false;
    while (!stop) {
        struct stt_whisper_command_header hdr;
        hdr.command = 0;
        hdr.status = 0;
        hdr.size = 0;

        status = stt_whisper_command_header_Recv(&cmd_recv, &hdr);
        if (status != VLC_SUCCESS) {
            break;
        }

        if (hdr.size > STT_WHISPER_MAX_MSG_SIZE) {
            status = VLC_EGENERIC;
            break;
        }

        if (hdr.size != 0 && buf_size < hdr.size) {
            uint8_t *ptr = realloc(buf, hdr.size);
            if (ptr == NULL) {
                status = VLC_ENOMEM;
                break;
            }
            buf = ptr;
            buf_size = hdr.size;
        }

        if (hdr.size != 0) {
            status = stt_whisper_command_Recv(buf, hdr.size, cmd_recv.recv,
                                              cmd_recv.opaque);
            if (status != VLC_SUCCESS) {
                break;
            }
        }

        switch (hdr.command) {
            case stt_whisper_command_addAudio:
                stt_whisper_command_recv_AddAudio(&cmd_recv, buf, hdr.size);
                break;
            case stt_whisper_command_drain:
                stt_whisper_command_recv_Drain(&cmd_recv, buf, hdr.size);
                break;
            case stt_whisper_command_flush:
                stt_whisper_command_recv_Flush(&cmd_recv);
                break;
            case stt_whisper_command_setParams:
                stt_whisper_command_recv_SetParams(&cmd_recv, buf, hdr.size);
                break;
            case stt_whisper_command_close:
                stt_whisper_command_recv_Close(&cmd_recv);
                stop = true;
                break;
            case stt_whisper_command_start:
                stt_whisper_command_recv_Start(&cmd_recv);
                break;
            case stt_whisper_command_stop:
                stt_whisper_command_recv_Stop(&cmd_recv);
                break;
            case stt_whisper_command_confirmReply:
                stt_whisper_command_recv_ConfirmReply(&cmd_recv, buf, hdr.size);
                break;
            case stt_whisper_command_abort:
                stt_whisper_command_recv_Abort(&cmd_recv);
                break;

            case stt_whisper_command_ready:
                stt_whisper_command_recv_Ready(&cmd_recv, hdr.status);
                break;
            case stt_whisper_command_sendSubtitle:
                stt_whisper_command_recv_SendSubtitle(&cmd_recv, buf, hdr.size);
                break;
            case stt_whisper_command_onProgress:
                stt_whisper_command_recv_OnProgress(&cmd_recv, hdr.status, buf,
                                                    hdr.size);
                break;
            case stt_whisper_command_confirm:
                stt_whisper_command_recv_Confirm(&cmd_recv, buf, hdr.size);
                break;
            case stt_whisper_command_log:
                stt_whisper_command_recv_Log(&cmd_recv, buf, hdr.size);
                break;

            default:
                break;
        }
    }

    free(buf);
    return status;
}
