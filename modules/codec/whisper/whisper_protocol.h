// SPDX-License-Identifier: LGPL-2.1-or-later
/*****************************************************************************
 * whisper_protocol.h: Internal rpc/ipc protocol for whisper
 *****************************************************************************
 * Copyright © 2025 Videolabs, VideoLAN and VLC authors
 *
 * Authors: Gabriel Lafond Thenaille <gabriel@videolabs.io>
 *****************************************************************************/

#ifndef VLC_WHISPER_PROTOCOL_H
#define VLC_WHISPER_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#include <vlc_frame.h>

#include "whisper_internal.h"

/**
 * VLC-Whisper External Process Communication Protocol
 *
 *
 * 1. Message Format
 *
 * All messages consist of a fixed 16-octet header followed by an optional
 * variable-length payload. The header format is:
 *
 * Byte  | 0     | 1     | 2     | 3     | 4     | 5     | 6     | 7     |
 *       +-------+-------+-------+-------+-------+-------+-------+-------+
 *     0 | Cmd   | Status|            Padding                            |
 *       +-------+-------+-------+-------+-------+-------+-------+-------+
 *     8 |                    Payload Size (64-bit LE)                   |
 *       +-------+-------+-------+-------+-------+-------+-------+-------+
 *
 * Header Fields:
 *   - Command (1 octet): Message type identifier (see Section 2)
 *   - Status (1 octet): vlc_stt_ctx_status value; meaningful for Ready,
 *     OnProgress and Confirm, vlc_stt_ctx_status_done (0) otherwise
 *   - Padding (6 octets): Reserved for future use
 *   - Payload Size (8 octets): Length of payload in octets, little-endian
 *
 * Message Processing:
 *   1. Receiver reads 16 octets (blocking until complete)
 *   2. Parses header to extract command type and payload size
 *   3. If payload size > 0, reads exactly that many octets (blocking)
 *   4. Dispatches complete message to command-specific handler
 *
 * A payload size above 64 MiB is a protocol error: the receiver stops its
 * receive loop. Unknown command codes are ignored. Malformed payloads (too
 * short, inconsistent size field, missing null terminator) are dropped
 * without reply.
 *
 * 2. Command Definitions
 *
 * Commands are categorized as operations (VLC -> Whisper) or callbacks
 * (Whisper -> VLC). Command codes 0x01-0x09 are operations, 0x81-0x85
 * are callbacks.
 *
 * Most messages are one-way. The model-download confirmation is the only
 * request/response exchange: the whisper process emits a Confirm callback
 * (0x84) and blocks until VLC answers with a ConfirmReply operation (0x08).
 * VLC may also send an Abort operation (0x09) to interrupt a load in progress
 * (e.g. download/hash) without ending the session.
 *
 * 2.1 Operations (VLC -> Whisper Process)
 *
 * 2.1.1 AddAudio (0x01)
 *
 * Purpose: Stream an audio frame to the whisper process for transcription.
 *
 * Payload structure (24 + audio data length octets):
 *
 * Byte  | 0     | 1     | 2     | 3     | 4     | 5     | 6     | 7     |
 *       +-------+-------+-------+-------+-------+-------+-------+-------+
 *     0 |                    PTS (64-bit LE)                            |
 *       +---------------------------------------------------------------+
 *     8 |                    Length (64-bit LE)                         |
 *       +---------------------------------------------------------------+
 *    16 |                    Buffer Size (64-bit LE)                    |
 *       +---------------------------------------------------------------+
 *    24 |                                                               |
 *       +              Audio Data (32-bit float samples)                +
 *       |                            ...                                |
 *       +---------------------------------------------------------------+
 *
 * Payload Fields:
 *   - PTS (8 octets): Presentation timestamp in VLC tick units (microseconds)
 *   - Length (8 octets): Frame duration in VLC tick units
 *   - Buffer Size (8 octets): Audio data length in octets
 *   - Audio Data (variable): 32bit float samples, 16kHz mono
 *
 * Semantics:
 *   Appends audio frame to internal buffer. When sufficient audio is
 *   accumulated, whisper process performs inference and sends SendSubtitle
 *   callbacks with transcribed text. Multiple AddAudio commands may be
 *   processed before any subtitle is generated.
 *
 * 2.1.2 Drain (0x02)
 *
 * Purpose: Signal end-of-stream and request final transcription.
 *
 * Payload structure: Identical to AddAudio (see Section 2.1.1), or none
 * (header only, payload size = 0) when there is no final audio frame.
 *
 * Semantics:
 *   Processes the final audio chunk, if any, and flushes all buffered data. The
 *   whisper process performs inference on any remaining audio and sends
 *   all pending SendSubtitle callbacks before acknowledging end-of-stream.
 *   After Drain, the session remains open.
 *
 * 2.1.3 Flush (0x03)
 *
 * Purpose: Reset transcription state and discard buffered audio.
 *
 * Payload: None (header only, payload size = 0)
 *
 * Semantics:
 *   Immediately discards all buffered audio and partial transcription state.
 *   Used when seeking or handling stream discontinuities. No callbacks are
 *   generated for discarded data. The whisper process returns to the ready
 *   state and can accept new AddAudio commands.
 *
 * 2.1.4 SetParams (0x04)
 *
 * Purpose: Configure transcription engine parameters.
 *
 * Payload structure (16 + language length + model name length octets):
 *
 * Byte  | 0     | 1     | 2     | 3     | 4     | 5     | 6     | 7     |
 *       +-------+-------+-------+-------+-------+-------+-------+-------+
 *     0 |                    Length MS (64-bit LE)                      |
 *       +-------------------------------+-------------------------------+
 *     8 |     N Threads (32-bit LE)     | Translation Index (32-bit LE) |
 *       +-------------------------------+-------------------------------+
 *    16 |                                                               |
 *       +          Language String (null-terminated, variable)          +
 *       |                            ...                                |
 *       +---------------------------------------------------------------+
 *       |                                                               |
 *       +         Model Name String (null-terminated, variable)         +
 *       |                            ...                                |
 *       +---------------------------------------------------------------+
 *
 * Payload Fields:
 *   - Length MS (8 octets): Audio chunk length in milliseconds for inference
 *   - N Threads (4 octets): Worker thread count for model inference
 *   - Translation Index (4 octets): Translation mode selector (0 = off)
 *   - Language (variable): ISO 639-1 language code, null-terminated ASCII
 *   - Model Name (variable): Model filename, null-terminated ASCII
 *
 * Semantics:
 *   The first SetParams makes the whisper process load the specified model
 *   and send a Ready callback when loading completes or fails. A later
 *   SetParams updates the parameters; if the model, language or translation
 *   index changed, the models are reloaded before the next inference and no
 *   Ready callback is sent.
 *
 * 2.1.5 Close (0x05)
 *
 * Purpose: Initiate graceful shutdown of the whisper process.
 *
 * Payload: None (header only, payload size = 0)
 *
 * Semantics:
 *   Signals the whisper process to exit its receive loop. The process may
 *   send final callbacks before terminating. After Close, the pipe is
 *   closed and the session ends. VLC should not send further commands after
 *   Close.
 *
 * 2.1.6 Start (0x06)
 *
 * Purpose: Begin a new transcription session.
 *
 * Payload: None (header only, payload size = 0)
 *
 * Semantics:
 *   Resets internal transcription state (clears buffered audio, drain and
 *   stop flags, and any pending PTS). Must be sent after the Ready callback
 *   and before the first AddAudio command of each session. Sending Start
 *   while audio processing is in progress results in undefined behavior.
 *
 * 2.1.7 Stop (0x07)
 *
 * Purpose: Suspend the current transcription session.
 *
 * Payload: None (header only, payload size = 0)
 *
 * Semantics:
 *   Signals the whisper process to cease processing. All buffered audio is
 *   discarded and any inference in progress is aborted. No further callbacks
 *   are generated for the stopped session. A new session may be started with
 *   a subsequent Start command.
 *
 * 2.1.8 ConfirmReply (0x08)
 *
 * Purpose: Answer a pending Confirm (0x84) model-download request.
 *
 * Payload structure (1 octet):
 *
 * Byte  | 0       |
 *       +---------+
 *     0 | Accepted|
 *       +---------+
 *
 * Payload Fields:
 *   - Accepted (1 octet): non-zero to proceed with the download, zero to
 *     cancel it.
 *
 * Semantics:
 *   Sent in response to a Confirm callback. The whisper process is blocked in
 *   its loader thread waiting for this reply; it resumes (downloading) or
 *   aborts the load based on the value.
 *
 * 2.1.9 Abort (0x09)
 *
 * Purpose: Request the in-progress load (download/hash/load) to abort.
 *
 * Payload: None (header only, payload size = 0)
 *
 * Semantics:
 *   Sets the abort flag in the whisper process so the current model load bails
 *   out at the next cancellation point and reports an error status. Unlike Stop
 *   it targets the loading phase and leaves the session otherwise intact. Also
 *   wakes a loader thread blocked on a pending Confirm.
 *
 * 2.2 Callbacks (Whisper Process -> VLC)
 *
 * 2.2.1 Ready (0x81)
 *
 * Purpose: Signal that initialization is complete and the process is ready
 *          to accept audio.
 *
 * Payload: None (header only, payload size = 0)
 *
 * Status Field: vlc_stt_ctx_status_done on success,
 *               vlc_stt_ctx_status_error on failure.
 *
 * Semantics:
 *   Sent once, at the end of the initial model load triggered by the first
 *   SetParams.
 *
 * 2.2.2 SendSubtitle (0x82)
 *
 * Purpose: Return a transcribed subtitle segment.
 *
 * Payload structure (24 + subtitle text length octets):
 *
 * Byte  | 0     | 1     | 2     | 3     | 4     | 5     | 6     | 7     |
 *       +-------+-------+-------+-------+-------+-------+-------+-------+
 *     0 |                    PTS (64-bit LE)                            |
 *       +---------------------------------------------------------------+
 *     8 |                    Length (64-bit LE)                         |
 *       +---------------------------------------------------------------+
 *    16 |                    Buffer Size (64-bit LE)                    |
 *       +---------------------------------------------------------------+
 *    24 |                                                               |
 *       +          Subtitle Text (UTF-8 encoded, variable)              +
 *       |                            ...                                |
 *       +---------------------------------------------------------------+
 *
 * Payload Fields:
 *   - PTS (8 octets): Subtitle start time in VLC tick units
 *   - Length (8 octets): Subtitle duration in VLC tick units
 *   - Buffer Size (8 octets): Text length in octets
 *   - Subtitle Text (variable): UTF-8 encoded transcription
 *
 * Semantics:
 *   Carries transcribed text corresponding to processed audio. Multiple
 *   SendSubtitle callbacks may be generated for a single AddAudio command,
 *   as the whisper engine segments speech into sentence-level chunks.
 *   Subtitles are sent in temporal order.
 *
 * 2.2.3 OnProgress (0x83)
 *
 * Purpose: Report processing progress during long operations.
 *
 * Payload structure (16 + description length octets):
 *
 * Byte  | 0     | 1     | 2     | 3     | 4     | 5     | 6     | 7     |
 *       +-------+-------+-------+-------+-------+-------+-------+-------+
 *     0 |                    Position (64-bit LE)                       |
 *       +---------------------------------------------------------------+
 *     8 |                    Total (64-bit LE)                          |
 *       +---------------------------------------------------------------+
 *    16 |                                                               |
 *       +         Description String (null-terminated, variable)        +
 *       |                            ...                                |
 *       +---------------------------------------------------------------+
 *
 * Payload Fields:
 *   - Position (8 octets): Current progress, signed (e.g. bytes downloaded).
 *   - Total (8 octets): Expected final Position, signed.
 *   - Description (variable): Model name, null-terminated ASCII.
 *
 * Status Field: vlc_stt_ctx_status value of the current operation
 *               (downloading, extracting, hashing or loading).
 *
 * Semantics:
 *   Sent periodically during model loading. Allows VLC to display progress.
 *   Progress values are operation-specific; Position/Total ratio indicates
 *   completion percentage. Not all operations generate OnProgress callbacks.
 *
 * 2.2.4 Confirm (0x84)
 *
 * Purpose: Ask VLC to confirm a model download before it starts.
 *
 * Payload structure (8 + path length octets):
 *
 * Byte  | 0     | 1     | 2     | 3     | 4     | 5     | 6     | 7     |
 *       +-------+-------+-------+-------+-------+-------+-------+-------+
 *     0 |                    Download Size (64-bit LE)                  |
 *       +---------------------------------------------------------------+
 *     8 |                                                               |
 *       +          Destination Path (null-terminated, variable)         +
 *       |                            ...                                |
 *       +---------------------------------------------------------------+
 *
 * Payload Fields:
 *   - Download Size (8 octets): size of the download in octets.
 *   - Destination Path (variable): where the model will be written,
 *     null-terminated ASCII/UTF-8.
 *
 * Status Field: vlc_stt_ctx_status_downloading.
 *
 * Semantics:
 *   Emitted from the loader thread before a download. The whisper process
 *   blocks until VLC replies with a ConfirmReply (0x08), an Abort (0x09) is
 *   received or VLC disconnects; the last two cancel the download. A missing
 *   handler on the VLC side is treated as "proceed".
 *
 * 2.2.5 Log (0x85)
 *
 * Purpose: Forward a log message emitted by the whisper process.
 *
 * Payload structure (8 + module, file and message lengths octets):
 *
 * Byte  | 0     | 1     | 2     | 3     | 4     | 5     | 6     | 7     |
 *       +-------+-------+-------+-------+-------+-------+-------+-------+
 *     0 |       Type (32-bit LE)        |       Line (32-bit LE)        |
 *       +-------------------------------+-------------------------------+
 *     8 |                                                               |
 *       +           Module Name (null-terminated, variable)             +
 *       |                            ...                                |
 *       +---------------------------------------------------------------+
 *       |                                                               |
 *       +         Source File Name (null-terminated, variable)          +
 *       |                            ...                                |
 *       +---------------------------------------------------------------+
 *       |                                                               |
 *       +             Message (null-terminated, variable)               +
 *       |                            ...                                |
 *       +---------------------------------------------------------------+
 *
 * Payload Fields:
 *   - Type (4 octets): vlc_log_type enumeration value, signed.
 *   - Line (4 octets): source code line number, unsigned.
 *   - Module Name (variable): emitting module, empty if unknown.
 *   - Source File Name (variable): source code file, empty if unknown.
 *   - Message (variable): formatted UTF-8 log message.
 *
 * Semantics:
 *   The whisper process runs its own libvlc instance, so its log messages
 *   would otherwise not reach the VLC log. VLC re-emits each one on the
 *   speech-to-text object.
 *
 * 3. Data Type Specifications
 *
 * 3.1 Integer Encoding
 *
 * All multi-octet integers use little-endian byte order (least significant
 * byte at lowest address).
 *
 * 3.2 String Encoding
 *
 * Strings are null-terminated byte sequences with no explicit length prefix.
 * The null terminator (0x00) marks the end of the string.
 *
 * 3.3 Buffer Encoding
 *
 * Opaque octet sequences are transmitted without transformation. Length is
 * specified in a preceding size field.
 *
 * 4. Protocol State Machine
 *
 * The whisper process operates in the following states:
 *
 *   INITIAL: Process started, no model loaded
 *     -> SetParams -> LOADING
 *     -> Close -> CLOSED
 *
 *   LOADING: Model being downloaded/hashed/loaded; OnProgress, Confirm and
 *            Log callbacks may be sent
 *     -> ConfirmReply -> LOADING (answers a pending Confirm)
 *     -> Abort -> LOADING (load fails, Ready with error status follows)
 *     -> (load complete) Ready -> STOPPED
 *     -> Close -> CLOSED
 *
 *   STOPPED: Model loaded, audio not processed
 *     -> Start -> RUNNING
 *     -> SetParams -> STOPPED (reload deferred to next inference)
 *     -> Close -> CLOSED
 *
 *   RUNNING: Audio accepted and transcribed; SendSubtitle callbacks may be
 *            sent
 *     -> AddAudio -> RUNNING
 *     -> Drain -> RUNNING (pending audio processed, then idle)
 *     -> Flush -> RUNNING (buffered audio discarded)
 *     -> SetParams -> RUNNING (models reloaded before next inference)
 *     -> Stop -> STOPPED
 *     -> Close -> CLOSED
 *
 *   CLOSED: Receive loop exited on both ends
 *     -> (terminal state)
 *
 * Log callbacks may be sent in any state but CLOSED. Sending AddAudio or
 * Drain outside RUNNING results in undefined behavior.
 *
 */

int
stt_whisper_command_send_AddAudio(vlc_frame_t *audio,
                                  ssize_t (*send)(void *, const uint8_t *, size_t),
                                  void *opaque);

int
stt_whisper_command_send_Drain(vlc_frame_t *audio,
                               ssize_t (*send)(void *, const uint8_t *, size_t),
                               void *opaque);
 
int
stt_whisper_command_send_Flush(ssize_t (*send)(void *, const uint8_t *, size_t),
                               void *opaque);
 
int
stt_whisper_command_send_SetParams(const struct stt_whisper_params *params,
                                   ssize_t (*send)(void *, const uint8_t *, size_t),
                                   void *opaque);

int
stt_whisper_command_send_Close(ssize_t (*send)(void *, const uint8_t *, size_t),
                               void *opaque);

int
stt_whisper_command_send_Start(ssize_t (*send)(void *, const uint8_t *, size_t),
                               void *opaque);

int
stt_whisper_command_send_Stop(ssize_t (*send)(void *, const uint8_t *, size_t),
                              void *opaque);

int
stt_whisper_command_send_Ready(enum vlc_stt_ctx_status status,
                               ssize_t (*send)(void *, const uint8_t *, size_t),
                               void *opaque);
 
int
stt_whisper_command_send_SendSubtitle(vlc_frame_t *sub,
                                      ssize_t (*send)(void *, const uint8_t *, size_t),
                                      void *opaque);
 
int
stt_whisper_command_send_OnProgress(enum vlc_stt_ctx_status status,
                                    int64_t pos, int64_t total,
                                    const char *description,
                                    ssize_t (*send)(void *, const uint8_t *, size_t),
                                    void *opaque);

int
stt_whisper_command_send_Confirm(uint64_t download_size, const char *path,
                                 ssize_t (*send)(void *, const uint8_t *, size_t),
                                 void *opaque);

int
stt_whisper_command_send_ConfirmReply(bool accepted,
                                      ssize_t (*send)(void *, const uint8_t *, size_t),
                                      void *opaque);

int
stt_whisper_command_send_Abort(ssize_t (*send)(void *, const uint8_t *, size_t),
                               void *opaque);

int
stt_whisper_command_send_Log(int type, const char *module, const char *file,
                             unsigned line, const char *msg,
                             ssize_t (*send)(void *, const uint8_t *, size_t),
                             void *opaque);

/**
 * Receive and dispatch messages until the peer closes or an error occurs.
 *
 * @param backend backend whose ops/callbacks are invoked on each message.
 * @param recv blocking read function for the input channel.
 * @param recv_opaque opaque passed to @p recv.
 * @param send write function for the output channel, used to answer requests
 *        (e.g. ConfirmReply); may be NULL on a receive-only endpoint.
 * @param send_opaque opaque passed to @p send.
 */
int
stt_whisper_command_RecvLoop(struct stt_whisper_backend *backend,
                             ssize_t (*recv)(void *, uint8_t *, size_t),
                             void *recv_opaque,
                             ssize_t (*send)(void *, const uint8_t *, size_t),
                             void *send_opaque);

#endif /* VLC_WHISPER_PROTOCOL_H */
