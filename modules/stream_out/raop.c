/*****************************************************************************
 * raop.c: Remote Audio Output Protocol streaming support
 *****************************************************************************
 * Copyright (C) 2008, 2018, 2026 VLC authors and VideoLAN
 *
 * Author: Michael Hanselmann
 *         Alexander Lyon <arlyon -at- me.com>
 *         Felix Paul Kühne <fkuehne -at- videolan.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

/*****************************************************************************
 * Preamble
 *****************************************************************************/
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <string.h>

#include <gcrypt.h>

#include <vlc_common.h>
#include <vlc_configuration.h>
#include <vlc_plugin.h>
#include <vlc_sout.h>
#include <vlc_block.h>
#include <vlc_arrays.h>
#include <vlc_network.h>
#include <vlc_strings.h>
#include <vlc_charset.h>
#include <vlc_gcrypt.h>
#include <vlc_es.h>
#include <vlc_http.h>
#include <vlc_keystore.h>
#include <vlc_memstream.h>
#include <vlc_threads.h>
#include <vlc_tick.h>
#include <vlc_rand.h>
#include <vlc_meta.h>
#include <vlc_stream.h>
#include <vlc_url.h>

#include "raop_alac.h"
#include "raop_common.h"

#define RAOP_PORT 5000
#define RAOP_USER_AGENT "VLC " VERSION
#define RAOP_SAMPLE_RATE 44100
#define RAOP_LATENCY_FRAMES 88200
#define NTP_EPOCH_OFFSET UINT64_C(2208988800)

typedef struct sout_stream_id_sys_t sout_stream_id_sys_t;
typedef struct sout_stream_sys_t sout_stream_sys_t;

static const char ps_raop_rsa_pubkey[] =
    "\xe7\xd7\x44\xf2\xa2\xe2\x78\x8b\x6c\x1f\x55\xa0\x8e\xb7\x05\x44"
    "\xa8\xfa\x79\x45\xaa\x8b\xe6\xc6\x2c\xe5\xf5\x1c\xbd\xd4\xdc\x68"
    "\x42\xfe\x3d\x10\x83\xdd\x2e\xde\xc1\xbf\xd4\x25\x2d\xc0\x2e\x6f"
    "\x39\x8b\xdf\x0e\x61\x48\xea\x84\x85\x5e\x2e\x44\x2d\xa6\xd6\x26"
    "\x64\xf6\x74\xa1\xf3\x04\x92\x9a\xde\x4f\x68\x93\xef\x2d\xf6\xe7"
    "\x11\xa8\xc7\x7a\x0d\x91\xc9\xd9\x80\x82\x2e\x50\xd1\x29\x22\xaf"
    "\xea\x40\xea\x9f\x0e\x14\xc0\xf7\x69\x38\xc5\xf3\x88\x2f\xc0\x32"
    "\x3d\xd9\xfe\x55\x15\x5f\x51\xbb\x59\x21\xc2\x01\x62\x9f\xd7\x33"
    "\x52\xd5\xe2\xef\xaa\xbf\x9b\xa0\x48\xd7\xb8\x13\xa2\xb6\x76\x7f"
    "\x6c\x3c\xcf\x1e\xb4\xce\x67\x3d\x03\x7b\x0d\x2e\xa3\x0c\x5f\xff"
    "\xeb\x06\xf8\xd0\x8a\xdd\xe4\x09\x57\x1a\x9c\x68\x9f\xef\x10\x72"
    "\x88\x55\xdd\x8c\xfb\x9a\x8b\xef\x5c\x89\x43\xef\x3b\x5f\xaa\x15"
    "\xdd\xe6\x98\xbe\xdd\xf3\x59\x96\x03\xeb\x3e\x6f\x61\x37\x2b\xb6"
    "\x28\xf6\x55\x9f\x59\x9a\x78\xbf\x50\x06\x87\xaa\x7f\x49\x76\xc0"
    "\x56\x2d\x41\x29\x56\xf8\x98\x9e\x18\xa6\x35\x5b\xd8\x15\x97\x82"
    "\x5e\x0f\xc8\x75\x34\x3e\xc7\x82\x11\x76\x25\xcd\xbf\x98\x44\x7b";

static const char ps_raop_rsa_exp[] = "\x01\x00\x01";

static const char psz_delim_space[] = " ";
static const char psz_delim_colon[] = ":";
static const char psz_delim_equal[] = "=";
static const char psz_delim_semicolon[] = ";";

/*****************************************************************************
 * Prototypes
 *****************************************************************************/
static int Open( vlc_object_t * );
static void Close( sout_stream_t * );

static int SinkOpen( vlc_object_t * );
static void SinkClose( sout_stream_t * );
static void SinkCleanup( sout_stream_sys_t * );

static void *SinkAdd( sout_stream_t *, const es_format_t *, const char * );
static void SinkDel( sout_stream_t *, void * );
static int SinkSend( sout_stream_t *, void *, block_t * );

static int VolumeCallback( vlc_object_t *p_this, char const *psz_cmd,
                           vlc_value_t oldval, vlc_value_t newval,
                           void *p_data );

typedef enum
{
    JACK_TYPE_NONE = 0,
    JACK_TYPE_ANALOG,
    JACK_TYPE_DIGITAL,
} jack_type_t;

struct sout_stream_sys_t
{
    /* Input parameters */
    char *psz_host;
    char *psz_password;
    int i_port;
    int i_volume;

    /* Plugin status */
    sout_stream_id_sys_t *p_audio_stream;
    bool b_volume_callback;

    /* Connection state */
    int i_control_fd;
    int i_audio_udp_fd;
    int i_control_udp_fd;
    int i_timing_udp_fd;

    int i_local_control_port;
    int i_local_timing_port;
    int i_server_audio_port;
    int i_server_control_port;
    int i_server_timing_port;

    uint8_t ps_aes_key[16];
    uint8_t ps_aes_iv[16];
    gcry_cipher_hd_t aes_ctx;

    char *psz_url;
    char *psz_client_instance;
    char *psz_session;
    char *psz_last_status_line;

    int i_cseq;
    int i_audio_latency;
    int i_jack_type;

    vlc_http_auth_t auth;

    /* Keystore-backed credential lookup. Initialized in Open() so the
     * URL/credential survive across the lazy SinkOpen handshake; populated
     * lazily on the first 401 response from the receiver. */
    vlc_url_t cred_url;
    vlc_credential cred;
    bool b_cred_init;

    /* Buffered reader for the RTSP control connection: net_Read'ing one byte
     * per call would mean ~10 syscalls per RTSP response header line. Pull
     * up to 4 KiB at a time and serve from the buffer. */
    uint8_t rtsp_buf[4096];
    size_t i_rtsp_buf_pos;
    size_t i_rtsp_buf_fill;

    /* RTP audio state, protected by lock. */
    vlc_mutex_t lock;
    uint16_t i_seq;
    uint32_t i_rtp_ts;
    uint32_t i_ssrc;
    bool b_first_audio_packet;
    bool b_first_sync;

    /* Periodic SYNC packet emitter */
    vlc_timer_t sync_timer;
    bool b_sync_timer;

    /* Real-time pacing for audio packets. The sout chain feeds blocks as
     * fast as the source can be read; without pacing we'd flood the
     * receiver's UDP buffer in milliseconds and most packets would be
     * dropped before playback. */
    vlc_tick_t i_next_packet_tick;

    /* PCM accumulator: araw delivers blocks of arbitrary size, but we
     * must emit exactly RAOP_FRAMES_PER_PACKET samples per RTP packet so
     * the verbatim ALAC frame size is constant. */
    uint8_t pcm_acc[RAOP_FRAMES_PER_PACKET * 2 * sizeof(int16_t)];
    size_t i_pcm_acc;

    /* Pre-allocated RTP send buffer: 12-byte header + fixed ALAC payload. */
    uint8_t *p_sendbuf;

    /* Pushed in by the raop_demux filter; under lock. b_session_ready
     * gates whether p_meta has already been flushed to the receiver. */
    vlc_meta_t *p_meta;
    bool b_session_ready;

    /* Shared interface exposed to the raop_demux filter via a vlc_object
     * variable on the input_thread ancestor. */
    raop_common common;
    vlc_object_t *p_common_owner;
    bool b_common_var;
};

struct sout_stream_id_sys_t
{
    es_format_t fmt;
};

/*****************************************************************************
 * Module descriptor
 *****************************************************************************/
#define SOUT_CFG_PREFIX "sout-raop-"

#define IP_TEXT N_("Host")
#define IP_LONGTEXT N_("Hostname or IP address of target device")

#define PORT_TEXT N_("Port")
#define PORT_LONGTEXT N_("RTSP control port of target device.")

#define VOLUME_TEXT N_("Volume")
#define VOLUME_LONGTEXT N_("Output volume for analog output: 0 for silence, " \
                           "1..255 from almost silent to very loud.")

vlc_module_begin()
    set_shortname( N_("RAOP") )
    set_description( N_("Remote Audio Output Protocol stream output") )
    set_capability( "sout output", 0 )
    add_shortcut( "raop" )
    set_subcategory( SUBCAT_SOUT_STREAM )
    add_string( SOUT_CFG_PREFIX "ip", "",
                IP_TEXT, IP_LONGTEXT )
    add_integer( SOUT_CFG_PREFIX "port", RAOP_PORT,
                 PORT_TEXT, PORT_LONGTEXT )
    add_integer_with_range( SOUT_CFG_PREFIX "volume", 100, 0, 255,
                            VOLUME_TEXT, VOLUME_LONGTEXT )
    set_callback( Open )

    add_submodule()
        add_shortcut( "raop-sink" )
        set_capability( "sout output", 0 )
        set_callback( SinkOpen )
vlc_module_end()

static const char *const ppsz_sout_options[] = {
    "ip",
    "port",
    "volume",
    NULL
};

/*****************************************************************************
 * Utilities:
 *****************************************************************************/
static void FreeSys( vlc_object_t *p_this, sout_stream_sys_t *p_sys )
{
    sout_stream_t *p_stream = (sout_stream_t*)p_this;

    SinkCleanup( p_sys );

    if ( p_sys->b_volume_callback )
        var_DelCallback( p_stream, SOUT_CFG_PREFIX "volume",
                         VolumeCallback, NULL );

    free( p_sys->psz_host );
    free( p_sys->psz_password );
    vlc_http_auth_Deinit( &p_sys->auth );

    if ( p_sys->b_cred_init )
    {
        vlc_credential_clean( &p_sys->cred );
        vlc_UrlClean( &p_sys->cred_url );
    }

    if ( p_sys->b_common_var && p_sys->p_common_owner != NULL )
        var_Destroy( p_sys->p_common_owner, RAOP_SHARED_VAR_NAME );
    if ( p_sys->p_meta != NULL )
        vlc_meta_Delete( p_sys->p_meta );

    free( p_sys );
}

static void SetMetaCb( void *opaque, vlc_meta_t *p_meta )
{
    sout_stream_sys_t *p_sys = opaque;

    vlc_mutex_lock( &p_sys->lock );
    if ( p_sys->p_meta != NULL )
        vlc_meta_Delete( p_sys->p_meta );
    p_sys->p_meta = p_meta;
    vlc_mutex_unlock( &p_sys->lock );
}

static void FreeId( sout_stream_id_sys_t *id )
{
    free( id );
}

static void RemoveBase64Padding( char *str )
{
    char *ps_pos = strchr( str, '=' );
    if ( ps_pos != NULL )
        *ps_pos = '\0';
}

static int CheckForGcryptErrorWithLine( sout_stream_t *p_stream,
                                        gcry_error_t i_gcrypt_err,
                                        unsigned int i_line )
{
    if ( i_gcrypt_err != GPG_ERR_NO_ERROR )
    {
        msg_Err( p_stream, "gcrypt error (line %d): %s", i_line,
                 gpg_strerror( i_gcrypt_err ) );
        return 1;
    }

    return 0;
}

/* Wrapper to pass line number for easier debugging */
#define CheckForGcryptError( p_this, i_gcrypt_err ) \
    CheckForGcryptErrorWithLine( p_this, i_gcrypt_err, __LINE__ )

/* Refill the RTSP read buffer from the control socket. Returns 1 on
 * success, 0 on EOF/error. */
static int RtspBufferRefill( vlc_object_t *p_obj, sout_stream_sys_t *p_sys )
{
    ssize_t got = net_Read( p_obj, p_sys->i_control_fd,
                            p_sys->rtsp_buf, sizeof( p_sys->rtsp_buf ) );
    if ( got <= 0 )
        return 0;
    p_sys->i_rtsp_buf_pos = 0;
    p_sys->i_rtsp_buf_fill = (size_t)got;
    return 1;
}

/* Consume up to len bytes from the buffered reader, falling through to a
 * direct net_Read if more is needed. */
static ssize_t RtspRead( vlc_object_t *p_obj, sout_stream_sys_t *p_sys,
                         void *out, size_t len )
{
    uint8_t *p = out;
    size_t total = 0;

    if ( p_sys->i_rtsp_buf_pos < p_sys->i_rtsp_buf_fill )
    {
        size_t take = p_sys->i_rtsp_buf_fill - p_sys->i_rtsp_buf_pos;
        if ( take > len )
            take = len;
        memcpy( p, p_sys->rtsp_buf + p_sys->i_rtsp_buf_pos, take );
        p_sys->i_rtsp_buf_pos += take;
        p += take;
        len -= take;
        total += take;
    }
    if ( len > 0 )
    {
        ssize_t got = net_Read( p_obj, p_sys->i_control_fd, p, len );
        if ( got <= 0 )
            return total > 0 ? (ssize_t)total : got;
        total += (size_t)got;
    }
    return (ssize_t)total;
}

/* net_Gets() was removed from libvlccore; reimplement a minimal line reader
 * for the RTSP control connection. Backed by the buffered reader so a
 * typical response costs one syscall, not one per byte. */
static char *RaopReadLine( vlc_object_t *p_obj, sout_stream_sys_t *p_sys )
{
    char *buf = NULL;
    size_t size = 0, len = 0;

    for ( ;; )
    {
        if ( len + 1 >= size )
        {
            if ( size >= 65536 )
            {
                free( buf );
                return NULL;
            }
            char *newbuf = realloc( buf, size + 256 );
            if ( unlikely( newbuf == NULL ) )
            {
                free( buf );
                return NULL;
            }
            buf = newbuf;
            size += 256;
        }

        if ( p_sys->i_rtsp_buf_pos >= p_sys->i_rtsp_buf_fill
          && !RtspBufferRefill( p_obj, p_sys ) )
        {
            free( buf );
            return NULL;
        }

        char c = (char)p_sys->rtsp_buf[p_sys->i_rtsp_buf_pos++];
        if ( c == '\n' )
        {
            buf[len] = '\0';
            if ( len > 0 && buf[len - 1] == '\r' )
                buf[len - 1] = '\0';
            return buf;
        }
        buf[len++] = c;
    }
}

/* MGF1 is specified in RFC2437, section 10.2.1. Variables are named after the
 * specification.
 */
static int MGF1( vlc_object_t *p_this,
                 unsigned char *mask, size_t l,
                 const unsigned char *Z, const size_t zLen,
                 const int Hash )
{
    sout_stream_t *p_stream = (sout_stream_t*)p_this;
    gcry_error_t i_gcrypt_err;
    gcry_md_hd_t md_handle = NULL;
    unsigned int hLen;
    unsigned char *ps_md;
    uint32_t counter = 0;
    uint8_t C[4];
    size_t i_copylen;
    int i_err = VLC_SUCCESS;

    assert( mask != NULL );
    assert( Z != NULL );

    hLen = gcry_md_get_algo_dlen( Hash );

    i_gcrypt_err = gcry_md_open( &md_handle, Hash, 0 );
    if ( CheckForGcryptError( p_stream, i_gcrypt_err ) )
    {
        i_err = VLC_EGENERIC;
        goto error;
    }

    while ( l > 0 )
    {
        /* 3. For counter from 0 to \lceil{l / hLen}\rceil-1, do the following:
         * a. Convert counter to an octet string C of length 4 with the
         *    primitive I2OSP: C = I2OSP (counter, 4)
         */
        C[0] = (counter >> 24) & 0xff;
        C[1] = (counter >> 16) & 0xff;
        C[2] = (counter >> 8) & 0xff;
        C[3] = counter & 0xff;
        ++counter;

        /* b. Concatenate the hash of the seed Z and C to the octet string T:
         *    T = T || Hash (Z || C)
         */
        gcry_md_reset( md_handle );
        gcry_md_write( md_handle, Z, zLen );
        gcry_md_write( md_handle, C, 4 );
        ps_md = gcry_md_read( md_handle, Hash );

        /* 4. Output the leading l octets of T as the octet string mask. */
        i_copylen = __MIN( l, hLen );
        memcpy( mask, ps_md, i_copylen );
        mask += i_copylen;
        l -= i_copylen;
    }

error:
    gcry_md_close( md_handle );

    return i_err;
}

/* EME-OAEP-ENCODE is specified in RFC2437, section 9.1.1.1. Variables are
 * named after the specification.
 */
static int AddOaepPadding( vlc_object_t *p_this,
                           unsigned char *EM, const size_t emLenWithPrefix,
                           const unsigned char *M, const size_t mLen,
                           const unsigned char *P, const size_t pLen )
{
    const int Hash = GCRY_MD_SHA1;
    const unsigned int hLen = gcry_md_get_algo_dlen( Hash );
    unsigned char *seed = NULL;
    unsigned char *DB = NULL;
    unsigned char *dbMask = NULL;
    unsigned char *seedMask = NULL;
    size_t emLen;
    size_t psLen;
    size_t i;
    int i_err = VLC_SUCCESS;

    /* Space for 0x00 prefix in EM. */
    emLen = emLenWithPrefix - 1;

    /* Step 2:
     * If ||M|| > emLen-2hLen-1 then output "message too long" and stop.
     */
    if ( mLen > (emLen - (2 * hLen) - 1) )
    {
        msg_Err( p_this , "Message too long" );
        goto error;
    }

    /* Step 3:
     * Generate an octet string PS consisting of emLen-||M||-2hLen-1 zero
     * octets. The length of PS may be 0.
     */
    psLen = emLen - mLen - (2 * hLen) - 1;

    /*
     * Step 5:
     * Concatenate pHash, PS, the message M, and other padding to form a data
     * block DB as: DB = pHash || PS || 01 || M
     */
    DB = calloc( 1, hLen + psLen + 1 + mLen );
    dbMask = calloc( 1, emLen - hLen );
    seedMask = calloc( 1, hLen );

    if ( DB == NULL || dbMask == NULL || seedMask == NULL )
    {
        i_err = VLC_ENOMEM;
        goto error;
    }

    /* Step 4:
     * Let pHash = Hash(P), an octet string of length hLen.
     */
    gcry_md_hash_buffer( Hash, DB, P, pLen );

    /* Step 3:
     * Generate an octet string PS consisting of emLen-||M||-2hLen-1 zero
     * octets. The length of PS may be 0.
     */
    memset( DB + hLen, 0, psLen );

    /* Step 5:
     * Concatenate pHash, PS, the message M, and other padding to form a data
     * block DB as: DB = pHash || PS || 01 || M
     */
    DB[hLen + psLen] = 0x01;
    memcpy( DB + hLen + psLen + 1, M, mLen );

    /* Step 6:
     * Generate a random octet string seed of length hLen
     */
    seed = gcry_random_bytes( hLen, GCRY_STRONG_RANDOM );
    if ( seed == NULL )
    {
        i_err = VLC_ENOMEM;
        goto error;
    }

    /* Step 7:
     * Let dbMask = MGF(seed, emLen-hLen).
     */
    i_err = MGF1( p_this, dbMask, emLen - hLen, seed, hLen, Hash );
    if ( i_err != VLC_SUCCESS )
        goto error;

    /* Step 8:
     * Let maskedDB = DB \xor dbMask.
     */
    for ( i = 0; i < (emLen - hLen); ++i )
        DB[i] ^= dbMask[i];

    /* Step 9:
     * Let seedMask = MGF(maskedDB, hLen).
     */
    i_err = MGF1( p_this, seedMask, hLen, DB, emLen - hLen, Hash );
    if ( i_err != VLC_SUCCESS )
        goto error;

    /* Step 10:
     * Let maskedSeed = seed \xor seedMask.
     */
    for ( i = 0; i < hLen; ++i )
        seed[i] ^= seedMask[i];

    /* Step 11:
     * Let EM = maskedSeed || maskedDB.
     */
    assert( (1 + hLen + (hLen + psLen + 1 + mLen)) == emLenWithPrefix );
    EM[0] = 0x00;
    memcpy( EM + 1, seed, hLen );
    memcpy( EM + 1 + hLen, DB, hLen + psLen + 1 + mLen );

    /* Step 12:
     * Output EM.
     */

error:
    free( DB );
    free( dbMask );
    free( seedMask );
    free( seed );

    return i_err;
}

static int EncryptAesKeyBase64( vlc_object_t *p_this, char **result )
{
    sout_stream_t *p_stream = (sout_stream_t*)p_this;
    sout_stream_sys_t *p_sys = p_stream->p_sys;
    gcry_error_t i_gcrypt_err;
    gcry_sexp_t sexp_rsa_params = NULL;
    gcry_sexp_t sexp_input = NULL;
    gcry_sexp_t sexp_encrypted = NULL;
    gcry_sexp_t sexp_token_a = NULL;
    gcry_mpi_t mpi_pubkey = NULL;
    gcry_mpi_t mpi_exp = NULL;
    gcry_mpi_t mpi_input = NULL;
    gcry_mpi_t mpi_output = NULL;
    unsigned char ps_padded_key[256];
    size_t i_value_size;
    int i_err;

    /* Add RSA-OAES-SHA1 padding */
    i_err = AddOaepPadding( p_this,
                            ps_padded_key, sizeof( ps_padded_key ),
                            p_sys->ps_aes_key, sizeof( p_sys->ps_aes_key ),
                            NULL, 0 );
    if ( i_err != VLC_SUCCESS )
        goto error;
    i_err = VLC_EGENERIC;

    /* Read public key */
    i_gcrypt_err = gcry_mpi_scan( &mpi_pubkey, GCRYMPI_FMT_USG,
                                  ps_raop_rsa_pubkey,
                                  sizeof( ps_raop_rsa_pubkey ) - 1, NULL );
    if ( CheckForGcryptError( p_stream, i_gcrypt_err ) )
        goto error;

    /* Read exponent */
    i_gcrypt_err = gcry_mpi_scan( &mpi_exp, GCRYMPI_FMT_USG, ps_raop_rsa_exp,
                                  sizeof( ps_raop_rsa_exp ) - 1, NULL );
    if ( CheckForGcryptError( p_stream, i_gcrypt_err ) )
        goto error;

    /* If the input data starts with a set bit (0x80), gcrypt thinks it's a
     * signed integer and complains. Prefixing it with a zero byte (\0)
     * works, but involves more work. Converting it to an MPI in our code is
     * cleaner.
     */
    i_gcrypt_err = gcry_mpi_scan( &mpi_input, GCRYMPI_FMT_USG,
                                  ps_padded_key, sizeof( ps_padded_key ),
                                  NULL);
    if ( CheckForGcryptError( p_stream, i_gcrypt_err ) )
        goto error;

    /* Build S-expression with RSA parameters */
    i_gcrypt_err = gcry_sexp_build( &sexp_rsa_params, NULL,
                                    "(public-key(rsa(n %m)(e %m)))",
                                    mpi_pubkey, mpi_exp );
    if ( CheckForGcryptError( p_stream, i_gcrypt_err ) )
        goto error;

    /* Build S-expression for data */
    i_gcrypt_err = gcry_sexp_build( &sexp_input, NULL, "(data(value %m))",
                                    mpi_input );
    if ( CheckForGcryptError( p_stream, i_gcrypt_err ) )
        goto error;

    /* Encrypt data */
    i_gcrypt_err = gcry_pk_encrypt( &sexp_encrypted, sexp_input,
                                    sexp_rsa_params );
    if ( CheckForGcryptError( p_stream, i_gcrypt_err ) )
        goto error;

    /* Extract encrypted data */
    sexp_token_a = gcry_sexp_find_token( sexp_encrypted, "a", 0 );
    if ( !sexp_token_a )
    {
        msg_Err( p_this , "Token 'a' not found in result S-expression" );
        goto error;
    }

    mpi_output = gcry_sexp_nth_mpi( sexp_token_a, 1, GCRYMPI_FMT_USG );
    if ( !mpi_output )
    {
        msg_Err( p_this, "Unable to extract MPI from result" );
        goto error;
    }

    /* Copy encrypted data into char array. Use FMT_STD with a fixed-size
     * buffer so the ciphertext is left-padded to the full 256-byte RSA
     * modulus length: gcry_mpi_aprint(USG) would strip leading zero bytes,
     * leaving the receiver to misalign the AES key on decrypt and produce
     * garbage. */
    {
        unsigned char ps_fixed[256];
        i_gcrypt_err = gcry_mpi_print( GCRYMPI_FMT_USG, ps_fixed,
                                       sizeof( ps_fixed ),
                                       &i_value_size, mpi_output );
        if ( CheckForGcryptError( p_stream, i_gcrypt_err ) )
            goto error;

        if ( i_value_size < sizeof( ps_fixed ) )
        {
            size_t i_pad = sizeof( ps_fixed ) - i_value_size;
            memmove( ps_fixed + i_pad, ps_fixed, i_value_size );
            memset( ps_fixed, 0, i_pad );
        }

        *result = vlc_b64_encode_binary( ps_fixed, sizeof( ps_fixed ) );
    }
    i_err = VLC_SUCCESS;

error:
    gcry_sexp_release( sexp_rsa_params );
    gcry_sexp_release( sexp_input );
    gcry_sexp_release( sexp_encrypted );
    gcry_sexp_release( sexp_token_a );
    gcry_mpi_release( mpi_pubkey );
    gcry_mpi_release( mpi_exp );
    gcry_mpi_release( mpi_input );
    gcry_mpi_release( mpi_output );

    return i_err;
}

/* Splits the value of a received header.
 *
 * Example: "Transport: RTP/AVP/TCP;unicast;mode=record;server_port=6000"
 */
static int SplitHeader( char **ppsz_next, char **ppsz_name,
                        char **ppsz_value )
{
    /* Find semicolon (separator between assignments) */
    *ppsz_name = strsep( ppsz_next, psz_delim_semicolon );
    if ( *ppsz_name )
    {
        /* Skip spaces */
        *ppsz_name += strspn( *ppsz_name, psz_delim_space );

        /* Get value */
        *ppsz_value = *ppsz_name;
        strsep( ppsz_value, psz_delim_equal );
    }
    else
        *ppsz_value = NULL;

    return !!*ppsz_name;
}

static void FreeHeader( void *p_value, void *p_data )
{
    VLC_UNUSED( p_data );
    free( p_value );
}

/* Strict TCP/UDP port parser for receiver-supplied values: rejects empty
 * input, trailing garbage, overflow, and out-of-range numbers. Returns -1
 * on any failure. */
static int ParsePort( const char *psz_value )
{
    if ( psz_value == NULL || *psz_value == '\0' )
        return -1;

    char *psz_end;
    errno = 0;
    long val = strtol( psz_value, &psz_end, 10 );
    if ( errno != 0 || *psz_end != '\0' || val <= 0 || val > 65535 )
        return -1;
    return (int)val;
}

static int ReadStatusLine( vlc_object_t *p_this )
{
    sout_stream_t *p_stream = (sout_stream_t*)p_this;
    sout_stream_sys_t *p_sys = p_stream->p_sys;
    char *psz_line = NULL;
    char *psz_token;
    char *psz_next;
    int i_result = VLC_EGENERIC;

    p_sys->psz_last_status_line = RaopReadLine( p_this, p_sys );
    if ( !p_sys->psz_last_status_line )
        goto error;

    /* Create working copy */
    psz_line = strdup( p_sys->psz_last_status_line );
    psz_next = psz_line;

    /* Protocol field */
    psz_token = strsep( &psz_next, psz_delim_space );
    if ( !psz_token || strncmp( psz_token, "RTSP/1.", 7 ) != 0 )
    {
        msg_Err( p_this, "Unknown protocol (%s)",
                 p_sys->psz_last_status_line );
        goto error;
    }

    /* Status field */
    psz_token = strsep( &psz_next, psz_delim_space );
    if ( !psz_token )
    {
        msg_Err( p_this, "Request failed (%s)",
                 p_sys->psz_last_status_line );
        goto error;
    }

    char *psz_end;
    errno = 0;
    long i_status = strtol( psz_token, &psz_end, 10 );
    if ( errno != 0 || psz_end == psz_token
      || i_status < 100 || i_status > 999 )
    {
        msg_Err( p_this, "Malformed status code (%s)",
                 p_sys->psz_last_status_line );
        goto error;
    }
    i_result = (int)i_status;

error:
    free( psz_line );

    return i_result;
}

static int ReadHeader( vlc_object_t *p_this,
                       vlc_dictionary_t *p_resp_headers,
                       int *done )
{
    sout_stream_t *p_stream = (sout_stream_t*)p_this;
    sout_stream_sys_t *p_sys = p_stream->p_sys;
    char *psz_original = NULL;
    char *psz_line = NULL;
    char *psz_token;
    char *psz_next;
    char *psz_name;
    char *psz_value;
    int i_err = VLC_SUCCESS;

    psz_line = RaopReadLine( p_this, p_sys );
    if ( !psz_line )
    {
        i_err = VLC_EGENERIC;
        goto error;
    }

    /* Empty line for response end */
    if ( psz_line[0] == '\0' )
        *done = 1;
    else
    {
        psz_original = strdup( psz_line );
        psz_next = psz_line;

        psz_token = strsep( &psz_next, psz_delim_colon );
        if ( !psz_token || psz_next[0] != ' ' )
        {
            msg_Err( p_this, "Invalid header format (%s)", psz_original );
            i_err = VLC_EGENERIC;
            goto error;
        }

        psz_name = psz_token;
        psz_value = psz_next + 1;

        vlc_dictionary_insert( p_resp_headers, psz_name, strdup( psz_value ) );
    }

error:
    free( psz_original );
    free( psz_line );

    return i_err;
}

static void WriteAuxHeaders( struct vlc_memstream *restrict stream,
                             vlc_dictionary_t *p_req_headers )
{
    char **ppsz_keys = vlc_dictionary_all_keys( p_req_headers );

    if( unlikely( !ppsz_keys ) )
        return;

    for( size_t i = 0; ppsz_keys[i] != NULL; i++ )
    {
        char *name = ppsz_keys[i];
        char *value = vlc_dictionary_value_for_key( p_req_headers, name );

        vlc_memstream_printf( stream, "%s: %s\r\n", name, value );
        free( name );
    }

    free( ppsz_keys );
}

static int SendRequest( vlc_object_t *p_this, const char *psz_method,
                        const char *psz_content_type,
                        const void *p_body, size_t i_body_length,
                        vlc_dictionary_t *p_req_headers )
{
    sout_stream_t *p_stream = (sout_stream_t*)p_this;
    sout_stream_sys_t *p_sys = p_stream->p_sys;
    struct vlc_memstream stream;
    ssize_t val;

    vlc_memstream_open( &stream );

    vlc_memstream_printf( &stream, "%s %s RTSP/1.0\r\n", psz_method,
                          p_sys->psz_url );
    vlc_memstream_puts( &stream, "User-Agent: " RAOP_USER_AGENT "\r\n" );
    vlc_memstream_printf( &stream, "Client-Instance: %s\r\n",
                          p_sys->psz_client_instance );
    vlc_memstream_printf( &stream, "CSeq: %u\r\n", ++p_sys->i_cseq );

    if( psz_content_type != NULL )
        vlc_memstream_printf( &stream, "Content-Type: %s\r\n",
                              psz_content_type );

    WriteAuxHeaders( &stream, p_req_headers );

    if( p_body != NULL && i_body_length > 0 )
    {
        vlc_memstream_printf( &stream, "Content-Length: %zu\r\n",
                              i_body_length );
        vlc_memstream_puts( &stream, "\r\n" );
        vlc_memstream_write( &stream, p_body, i_body_length );
    }
    else
        vlc_memstream_puts( &stream, "\r\n" );

    if( vlc_memstream_close( &stream ) )
        return VLC_ENOMEM;

    val = net_Write( p_this, p_sys->i_control_fd, stream.ptr, stream.length );
    free( stream.ptr );

    if( val < (ssize_t)stream.length )
        return VLC_EGENERIC;
    return VLC_SUCCESS;
}

static int ParseAuthenticateHeader( vlc_object_t *p_this,
                                    vlc_dictionary_t *p_resp_headers )
{
    sout_stream_t *p_stream = (sout_stream_t*)p_this;
    sout_stream_sys_t *p_sys = p_stream->p_sys;
    char *psz_auth;
    int i_err = VLC_SUCCESS;

    psz_auth = vlc_dictionary_value_for_key( p_resp_headers,
                                             "WWW-Authenticate" );
    if ( psz_auth == NULL )
    {
        msg_Err( p_this, "HTTP 401 response missing "
                         "WWW-Authenticate header" );
        i_err = VLC_EGENERIC;
        goto error;
    }

    vlc_http_auth_ParseWwwAuthenticateHeader( p_this, &p_sys->auth, psz_auth );

error:
    return i_err;
}

/* Pull a password from the keystore, or prompt the user when nothing is
 * stored. Called only after a 401, so the realm parsed from the challenge
 * keys both the keystore lookup and the dialog text. */
static int AcquirePassword( vlc_object_t *p_this )
{
    sout_stream_t *p_stream = (sout_stream_t*)p_this;
    sout_stream_sys_t *p_sys = p_stream->p_sys;

    p_sys->cred.psz_realm = p_sys->auth.psz_realm;
    p_sys->cred.psz_authtype = "Digest";

    int i_ret = vlc_credential_get( &p_sys->cred, p_this, NULL, NULL,
                                    _("AirPlay authentication"),
                                    _("The receiver \"%s\" requires a "
                                      "password."), p_sys->psz_host );
    if ( i_ret != 0 )
        return VLC_EGENERIC;

    free( p_sys->psz_password );
    p_sys->psz_password = strdup( p_sys->cred.psz_password );
    return p_sys->psz_password != NULL ? VLC_SUCCESS : VLC_ENOMEM;
}

static int ExecRequest( vlc_object_t *p_this, const char *psz_method,
                        const char *psz_content_type,
                        const void *p_body, size_t i_body_length,
                        vlc_dictionary_t *p_req_headers,
                        vlc_dictionary_t *p_resp_headers )
{
    sout_stream_t *p_stream = (sout_stream_t*)p_this;
    sout_stream_sys_t *p_sys = p_stream->p_sys;
    char *psz_authorization = NULL;
    int headers_done;
    int i_err = VLC_SUCCESS;
    int i_status;
    int i_auth_state;

    if ( p_sys->i_control_fd < 0 )
    {
        msg_Err( p_this, "Control connection not open" );
        i_err = VLC_EGENERIC;
        goto error;
    }

    i_auth_state = 0;
    while ( 1 )
    {
        /* Send header only when Digest authentication is used */
        if ( p_sys->psz_password != NULL && p_sys->auth.psz_nonce != NULL )
        {
            /* AirPort Express expects "iTunes" as the digest username
             * under the legacy "raop" realm; non-RAOP realms use "AirPlay". */
            const char *psz_user =
                ( p_sys->auth.psz_realm != NULL
                  && strcasecmp( p_sys->auth.psz_realm, "raop" ) != 0 )
                ? "AirPlay" : "iTunes";

            FREENULL( psz_authorization );

            psz_authorization =
                vlc_http_auth_FormatAuthorizationHeader( p_this, &p_sys->auth,
                                                         psz_method,
                                                         p_sys->psz_url,
                                                         psz_user,
                                                         p_sys->psz_password );
            if ( psz_authorization == NULL )
            {
                i_err = VLC_EGENERIC;
                goto error;
            }

            vlc_dictionary_insert( p_req_headers, "Authorization",
                                   psz_authorization );
        }

        /* Send request */
        i_err = SendRequest( p_this, psz_method, psz_content_type,
                             p_body, i_body_length, p_req_headers );
        if ( i_err != VLC_SUCCESS )
            goto error;

        /* Read status line */
        i_status = ReadStatusLine( p_this );
        if ( i_status < 0 )
        {
            i_err = i_status;
            goto error;
        }

        vlc_dictionary_clear( p_resp_headers, FreeHeader, NULL );

        /* Read headers */
        headers_done = 0;
        while ( !headers_done )
        {
            i_err = ReadHeader( p_this, p_resp_headers, &headers_done );
            if ( i_err != VLC_SUCCESS )
                goto error;
        }

        /* Drain any response body so it doesn't bleed into the next reply.
         * RECORD responses can carry a text/parameters body advertising the
         * device's audio latency, for example. Goes through the buffered
         * reader: kernel may have packed body bytes after the header in the
         * same TCP segment, and those already sit in the buffer. */
        const char *psz_cl = vlc_dictionary_value_for_key( p_resp_headers,
                                                           "Content-Length" );
        if ( psz_cl != NULL )
        {
            long i_cl = strtol( psz_cl, NULL, 10 );
            char ps_drain[256];
            while ( i_cl > 0 )
            {
                size_t i_want = i_cl > (long)sizeof( ps_drain ) ?
                                sizeof( ps_drain ) : (size_t)i_cl;
                ssize_t r = RtspRead( p_this, p_sys, ps_drain, i_want );
                if ( r <= 0 )
                    break;
                i_cl -= r;
            }
        }

        if ( i_status == 200 )
            /* Request successful */
            break;
        else if ( i_status == 401 )
        {
            /* Authorization required */
            if ( i_auth_state == 1 )
            {
                msg_Err( p_this, "Access denied, password invalid" );
                i_err = VLC_EGENERIC;
                goto error;
            }

            i_err = ParseAuthenticateHeader( p_this, p_resp_headers );
            if ( i_err != VLC_SUCCESS )
                goto error;

            if ( p_sys->psz_password == NULL )
            {
                i_err = AcquirePassword( p_this );
                if ( i_err != VLC_SUCCESS )
                    goto error;
            }

            i_auth_state = 1;
        }
        else
        {
            msg_Err( p_this, "Request failed (%s), status is %d",
                     p_sys->psz_last_status_line, i_status );
            i_err = VLC_EGENERIC;
            goto error;
        }
    }

error:
    FREENULL( p_sys->psz_last_status_line );
    free( psz_authorization );

    return i_err;
}

static int AnnounceSDP( vlc_object_t *p_this, char *psz_local,
                        uint32_t i_session_id )
{
    sout_stream_t *p_stream = (sout_stream_t*)p_this;
    sout_stream_sys_t *p_sys = p_stream->p_sys;
    vlc_dictionary_t req_headers;
    vlc_dictionary_t resp_headers;
    unsigned char ps_sac[16];
    char *psz_sdp = NULL;
    char *psz_sac_base64 = NULL;
    char *psz_aes_key_base64 = NULL;
    char *psz_aes_iv_base64 = NULL;
    int i_err = VLC_SUCCESS;
    int i_rc;

    vlc_dictionary_init( &req_headers, 0 );
    vlc_dictionary_init( &resp_headers, 0 );

    /* Encrypt AES key and encode it in Base64 */
    i_rc = EncryptAesKeyBase64( p_this, &psz_aes_key_base64 );
    if ( i_rc != VLC_SUCCESS || psz_aes_key_base64 == NULL )
    {
        i_err = VLC_EGENERIC;
        goto error;
    }
    RemoveBase64Padding( psz_aes_key_base64 );

    /* Encode AES IV in Base64 */
    psz_aes_iv_base64 = vlc_b64_encode_binary( p_sys->ps_aes_iv,
                                               sizeof( p_sys->ps_aes_iv ) );
    if ( psz_aes_iv_base64 == NULL )
    {
        i_err = VLC_EGENERIC;
        goto error;
    }
    RemoveBase64Padding( psz_aes_iv_base64 );

    /* Random bytes for Apple-Challenge header */
    vlc_rand_bytes( ps_sac, sizeof( ps_sac ) );

    psz_sac_base64 = vlc_b64_encode_binary( ps_sac, sizeof( ps_sac ) );
    if ( psz_sac_base64 == NULL )
    {
        i_err = VLC_EGENERIC;
        goto error;
    }
    RemoveBase64Padding( psz_sac_base64 );

    /* Build SDP
     * Note: IPv6 addresses also use "IP4". Make sure not to include the
     * scope ID.
     */
    i_rc = asprintf( &psz_sdp,
                     "v=0\r\n"
                     "o=iTunes %u 0 IN IP4 %s\r\n"
                     "s=iTunes\r\n"
                     "c=IN IP4 %s\r\n"
                     "t=0 0\r\n"
                     "m=audio 0 RTP/AVP 96\r\n"
                     "a=rtpmap:96 AppleLossless\r\n"
                     "a=fmtp:96 352 0 16 40 10 14 2 255 0 0 44100\r\n"
                     "a=rsaaeskey:%s\r\n"
                     "a=aesiv:%s\r\n",
                     i_session_id, psz_local, p_sys->psz_host,
                     psz_aes_key_base64, psz_aes_iv_base64 );

    if ( i_rc < 0 )
    {
        i_err = VLC_ENOMEM;
        goto error;
    }

    /* Build and send request */
    vlc_dictionary_insert( &req_headers, "Apple-Challenge", psz_sac_base64 );

    i_err = ExecRequest( p_this, "ANNOUNCE", "application/sdp",
                         psz_sdp, strlen( psz_sdp ),
                         &req_headers, &resp_headers );
    if ( i_err != VLC_SUCCESS )
        goto error;

error:
    vlc_dictionary_clear( &req_headers, NULL, NULL );
    vlc_dictionary_clear( &resp_headers, FreeHeader, NULL );

    free( psz_sdp );
    free( psz_sac_base64 );
    free( psz_aes_key_base64 );
    free( psz_aes_iv_base64 );

    return i_err;
}

static int SendSetup( vlc_object_t *p_this )
{
    sout_stream_t *p_stream = (sout_stream_t*)p_this;
    sout_stream_sys_t *p_sys = p_stream->p_sys;
    vlc_dictionary_t req_headers;
    vlc_dictionary_t resp_headers;
    char *psz_transport = NULL;
    int i_err = VLC_SUCCESS;
    char *psz_tmp;
    char *psz_next;
    char *psz_name;
    char *psz_value;

    vlc_dictionary_init( &req_headers, 0 );
    vlc_dictionary_init( &resp_headers, 0 );

    if ( asprintf( &psz_transport,
                   "RTP/AVP/UDP;unicast;interleaved=0-1;mode=record;"
                   "control_port=%d;timing_port=%d",
                   p_sys->i_local_control_port,
                   p_sys->i_local_timing_port ) < 0 )
    {
        i_err = VLC_ENOMEM;
        goto error;
    }
    vlc_dictionary_insert( &req_headers, "Transport", psz_transport );

    i_err = ExecRequest( p_this, "SETUP", NULL, NULL, 0,
                         &req_headers, &resp_headers );
    if ( i_err != VLC_SUCCESS )
        goto error;

    psz_tmp = vlc_dictionary_value_for_key( &resp_headers, "Session" );
    if ( !psz_tmp )
    {
        msg_Err( p_this, "Missing 'Session' header during setup" );
        i_err = VLC_EGENERIC;
        goto error;
    }

    free( p_sys->psz_session );
    p_sys->psz_session = strdup( psz_tmp );

    /* Parse remote audio / control / timing ports out of Transport */
    psz_next = vlc_dictionary_value_for_key( &resp_headers, "Transport" );
    while ( SplitHeader( &psz_next, &psz_name, &psz_value ) )
    {
        if ( psz_value == NULL )
            continue;

        int *pi_port;
        if ( strcmp( psz_name, "server_port" ) == 0 )
            pi_port = &p_sys->i_server_audio_port;
        else if ( strcmp( psz_name, "control_port" ) == 0 )
            pi_port = &p_sys->i_server_control_port;
        else if ( strcmp( psz_name, "timing_port" ) == 0 )
            pi_port = &p_sys->i_server_timing_port;
        else
            continue;

        int i_port = ParsePort( psz_value );
        if ( i_port < 0 )
        {
            msg_Err( p_this, "Invalid '%s' value '%s' from receiver",
                     psz_name, psz_value );
            i_err = VLC_EGENERIC;
            goto error;
        }
        *pi_port = i_port;
    }

    if ( p_sys->i_server_audio_port <= 0 )
    {
        msg_Err( p_this, "Missing 'server_port' during setup" );
        i_err = VLC_EGENERIC;
        goto error;
    }

    /* Get jack type */
    psz_next = vlc_dictionary_value_for_key( &resp_headers,
                                             "Audio-Jack-Status" );
    while ( SplitHeader( &psz_next, &psz_name, &psz_value ) )
    {
        if ( strcmp( psz_name, "type" ) != 0 )
            continue;

        if ( strcmp( psz_value, "analog" ) == 0 )
            p_sys->i_jack_type = JACK_TYPE_ANALOG;

        else if ( strcmp( psz_value, "digital" ) == 0 )
            p_sys->i_jack_type = JACK_TYPE_DIGITAL;

        break;
    }

error:
    vlc_dictionary_clear( &req_headers, NULL, NULL );
    vlc_dictionary_clear( &resp_headers, FreeHeader, NULL );
    free( psz_transport );

    return i_err;
}

static int SendRecord( vlc_object_t *p_this )
{
    sout_stream_t *p_stream = (sout_stream_t*)p_this;
    sout_stream_sys_t *p_sys = p_stream->p_sys;
    vlc_dictionary_t req_headers;
    vlc_dictionary_t resp_headers;
    int i_err = VLC_SUCCESS;
    char *psz_value;

    vlc_dictionary_init( &req_headers, 0 );
    vlc_dictionary_init( &resp_headers, 0 );

    vlc_dictionary_insert( &req_headers, "Range", (void *)"npt=0-" );
    vlc_dictionary_insert( &req_headers, "RTP-Info",
                           (void *)"seq=0;rtptime=0" );
    vlc_dictionary_insert( &req_headers, "Session",
                           (void *)p_sys->psz_session );

    i_err = ExecRequest( p_this, "RECORD", NULL, NULL, 0,
                         &req_headers, &resp_headers );
    if ( i_err != VLC_SUCCESS )
        goto error;

    psz_value = vlc_dictionary_value_for_key( &resp_headers, "Audio-Latency" );
    p_sys->i_audio_latency = 0;
    if ( psz_value != NULL && *psz_value != '\0' )
    {
        char *psz_end;
        errno = 0;
        long i_latency = strtol( psz_value, &psz_end, 10 );
        if ( errno == 0 && *psz_end == '\0'
          && i_latency >= 0 && i_latency <= INT_MAX )
            p_sys->i_audio_latency = (int)i_latency;
        else
            msg_Warn( p_this, "Ignoring malformed Audio-Latency value '%s'",
                      psz_value );
    }

error:
    vlc_dictionary_clear( &req_headers, NULL, NULL );
    vlc_dictionary_clear( &resp_headers, FreeHeader, NULL );

    return i_err;
}

/* DMAP framing: 4-byte ASCII tag + 4-byte big-endian length + payload.
 * AirPlay 1 receivers want all per-field tags wrapped in one outer mlit. */
static void DmapWriteString( struct vlc_memstream *ms, const char tag[4],
                             const char *str )
{
    uint32_t i_len = strlen( str );
    uint8_t be[4];
    SetDWBE( be, i_len );
    vlc_memstream_write( ms, tag, 4 );
    vlc_memstream_write( ms, be, 4 );
    vlc_memstream_write( ms, str, i_len );
}

static int SendDmapMeta( vlc_object_t *p_this )
{
    sout_stream_t *p_stream = (sout_stream_t *)p_this;
    sout_stream_sys_t *p_sys = p_stream->p_sys;
    int i_err = VLC_SUCCESS;
    vlc_dictionary_t req_headers;
    vlc_dictionary_t resp_headers;
    struct vlc_memstream inner = { 0 };
    struct vlc_memstream outer = { 0 };

    vlc_dictionary_init( &req_headers, 0 );
    vlc_dictionary_init( &resp_headers, 0 );

    vlc_memstream_open( &inner );
    vlc_mutex_lock( &p_sys->lock );
    if ( p_sys->p_meta != NULL )
    {
        const char *p;
        if ( ( p = vlc_meta_Get( p_sys->p_meta, vlc_meta_Title ) ) != NULL )
            DmapWriteString( &inner, "minm", p );
        if ( ( p = vlc_meta_Get( p_sys->p_meta, vlc_meta_Artist ) ) != NULL )
            DmapWriteString( &inner, "asar", p );
        if ( ( p = vlc_meta_Get( p_sys->p_meta, vlc_meta_Album ) ) != NULL )
            DmapWriteString( &inner, "asal", p );
    }
    vlc_mutex_unlock( &p_sys->lock );
    if ( vlc_memstream_close( &inner ) != 0 )
    {
        i_err = VLC_ENOMEM;
        goto out;
    }
    if ( inner.length == 0 )
        goto out;

    vlc_memstream_open( &outer );
    uint8_t outer_len_be[4];
    SetDWBE( outer_len_be, (uint32_t)inner.length );
    vlc_memstream_write( &outer, "mlit", 4 );
    vlc_memstream_write( &outer, outer_len_be, 4 );
    vlc_memstream_write( &outer, inner.ptr, inner.length );
    if ( vlc_memstream_close( &outer ) != 0 )
    {
        i_err = VLC_ENOMEM;
        goto out;
    }

    vlc_dictionary_insert( &req_headers, "Session",
                           (void *)p_sys->psz_session );
    vlc_dictionary_insert( &req_headers, "RTP-Info", (void *)"rtptime=0" );

    i_err = ExecRequest( p_this, "SET_PARAMETER",
                         "application/x-dmap-tagged",
                         outer.ptr, outer.length,
                         &req_headers, &resp_headers );

out:
    vlc_dictionary_clear( &req_headers, NULL, NULL );
    vlc_dictionary_clear( &resp_headers, FreeHeader, NULL );
    free( inner.ptr );
    free( outer.ptr );
    return i_err;
}

static int SendArtwork( vlc_object_t *p_this )
{
    sout_stream_t *p_stream = (sout_stream_t *)p_this;
    sout_stream_sys_t *p_sys = p_stream->p_sys;
    stream_t *p_art = NULL;
    block_t *p_block = NULL;
    int i_err = VLC_SUCCESS;
    vlc_dictionary_t req_headers;
    vlc_dictionary_t resp_headers;

    vlc_dictionary_init( &req_headers, 0 );
    vlc_dictionary_init( &resp_headers, 0 );

    vlc_mutex_lock( &p_sys->lock );
    if ( p_sys->p_meta != NULL )
    {
        const char *p = vlc_meta_Get( p_sys->p_meta, vlc_meta_ArtworkURL );
        if ( p != NULL )
            p_art = vlc_stream_NewURL( p_this, p );
    }
    vlc_mutex_unlock( &p_sys->lock );

    if ( p_art == NULL )
        goto out;

    uint64_t i_size;
    if ( vlc_stream_GetSize( p_art, &i_size ) != VLC_SUCCESS
      || i_size == 0 || i_size > UINT64_C( 8 * 1024 * 1024 ) )
        goto out;

    p_block = vlc_stream_Block( p_art, (size_t)i_size );
    if ( p_block == NULL || p_block->i_buffer != (size_t)i_size )
        goto out;

    /* Sniff JPEG / PNG; AirPlay 1 receivers reject anything else.
     *   PNG:  first 4 bytes of the 8-byte signature, "\x89 P N G".
     *   JPEG: SOI (FFD8) plus the FF that starts the next marker;
     *         the marker code (E0/E1/DB/...) varies, mask it out. */
    const uint32_t MAGIC_PNG  = 0x89504E47u;
    const uint32_t MAGIC_JPEG = 0xFFD8FF00u;

    if ( p_block->i_buffer < 4 )
        goto out;
    const uint32_t magic = GetDWBE( p_block->p_buffer );
    const char *psz_mime;
    if ( magic == MAGIC_PNG )
        psz_mime = "image/png";
    else if ( ( magic & 0xFFFFFF00u ) == MAGIC_JPEG )
        psz_mime = "image/jpeg";
    else
        goto out;

    vlc_dictionary_insert( &req_headers, "Session",
                           (void *)p_sys->psz_session );
    vlc_dictionary_insert( &req_headers, "RTP-Info", (void *)"rtptime=0" );

    i_err = ExecRequest( p_this, "SET_PARAMETER", psz_mime,
                         p_block->p_buffer, p_block->i_buffer,
                         &req_headers, &resp_headers );

out:
    vlc_dictionary_clear( &req_headers, NULL, NULL );
    vlc_dictionary_clear( &resp_headers, FreeHeader, NULL );
    if ( p_block != NULL )
        block_Release( p_block );
    if ( p_art != NULL )
        vlc_stream_Delete( p_art );
    return i_err;
}

static int SendFlush( vlc_object_t *p_this )
{
    vlc_dictionary_t resp_headers;
    vlc_dictionary_t req_headers;
    int i_err = VLC_SUCCESS;

    vlc_dictionary_init( &req_headers, 0 );
    vlc_dictionary_init( &resp_headers, 0 );

    vlc_dictionary_insert( &req_headers, "RTP-Info",
                           (void *)"seq=0;rtptime=0" );

    i_err = ExecRequest( p_this, "FLUSH", NULL, NULL, 0,
                         &req_headers, &resp_headers );
    if ( i_err != VLC_SUCCESS )
        goto error;

error:
    vlc_dictionary_clear( &req_headers, NULL, NULL );
    vlc_dictionary_clear( &resp_headers, FreeHeader, NULL );

    return i_err;
}

static int SendTeardown( vlc_object_t *p_this )
{
    vlc_dictionary_t resp_headers;
    vlc_dictionary_t req_headers;
    int i_err = VLC_SUCCESS;

    vlc_dictionary_init( &req_headers, 0 );
    vlc_dictionary_init( &resp_headers, 0 );

    i_err = ExecRequest( p_this, "TEARDOWN", NULL, NULL, 0,
                         &req_headers, &resp_headers );
    if ( i_err != VLC_SUCCESS )
        goto error;

error:
    vlc_dictionary_clear( &req_headers, NULL, NULL );
    vlc_dictionary_clear( &resp_headers, FreeHeader, NULL );

    return i_err;
}

static int UpdateVolume( vlc_object_t *p_this )
{
    sout_stream_t *p_stream = (sout_stream_t*)p_this;
    sout_stream_sys_t *p_sys = p_stream->p_sys;
    vlc_dictionary_t req_headers;
    vlc_dictionary_t resp_headers;
    char *psz_parameters = NULL;
    double d_volume;
    int i_err = VLC_SUCCESS;
    int i_rc;

    vlc_dictionary_init( &req_headers, 0 );
    vlc_dictionary_init( &resp_headers, 0 );

    /* Our volume is 0..255, RAOP is -144..0 (-144 off, -30..0 on) */

    /* Limit range */
    p_sys->i_volume = VLC_CLIP( p_sys->i_volume, 0, 255 );

    if ( p_sys->i_volume == 0 )
        d_volume = -144.0;
    else
        d_volume = -30 + ( ( (double)p_sys->i_volume ) * 30.0 / 255.0 );

    /* Format without using locales */
    i_rc = vlc_asprintf_c( &psz_parameters, "volume: %0.6f\r\n", d_volume );
    if ( i_rc < 0 )
    {
        i_err = VLC_ENOMEM;
        goto error;
    }

    vlc_dictionary_insert( &req_headers, "Session",
                           (void *)p_sys->psz_session );

    i_err = ExecRequest( p_this, "SET_PARAMETER",
                         "text/parameters",
                         psz_parameters, strlen( psz_parameters ),
                         &req_headers, &resp_headers );
    if ( i_err != VLC_SUCCESS )
        goto error;

error:
    vlc_dictionary_clear( &req_headers, NULL, NULL );
    vlc_dictionary_clear( &resp_headers, FreeHeader, NULL );
    free( psz_parameters );

    return i_err;
}

static void LogInfo( vlc_object_t *p_this )
{
    sout_stream_t *p_stream = (sout_stream_t*)p_this;
    sout_stream_sys_t *p_sys = p_stream->p_sys;
    const char *psz_jack_name;

    msg_Info( p_this, "Audio latency: %d", p_sys->i_audio_latency );

    switch ( p_sys->i_jack_type )
    {
        case JACK_TYPE_ANALOG:
            psz_jack_name = "analog";
            break;

        case JACK_TYPE_DIGITAL:
            psz_jack_name = "digital";
            break;

        case JACK_TYPE_NONE:
        default:
            psz_jack_name = "none";
            break;
    }

    msg_Info( p_this, "Jack type: %s", psz_jack_name );
}

static uint64_t NtpNow( void )
{
    struct timespec ts;
    clock_gettime( CLOCK_REALTIME, &ts );

    uint64_t sec = (uint64_t)ts.tv_sec + NTP_EPOCH_OFFSET;
    uint64_t frac = ((uint64_t)ts.tv_nsec << 32) / 1000000000ULL;
    return ( sec << 32 ) | frac;
}

static void SyncTimerCallback( void *opaque )
{
    sout_stream_t *p_stream = opaque;
    sout_stream_sys_t *p_sys = p_stream->p_sys;
    uint8_t pkt[20];

    /* Only i_rtp_ts is shared with the input thread (FlushOnePacket);
     * b_first_sync is touched only here. */
    vlc_mutex_lock( &p_sys->lock );
    uint32_t i_rtp_now = p_sys->i_rtp_ts;
    vlc_mutex_unlock( &p_sys->lock );
    bool b_first = p_sys->b_first_sync;
    p_sys->b_first_sync = false;

    /* The receiver advertises its buffering latency in the RECORD response;
     * fall back to the standard 2-second buffer if it didn't. */
    uint32_t i_latency = p_sys->i_audio_latency > 0
                       ? (uint32_t)p_sys->i_audio_latency : RAOP_LATENCY_FRAMES;
    uint32_t i_rtp_at_ntp = i_rtp_now - i_latency;
    uint64_t i_ntp = NtpNow();

    pkt[0] = b_first ? 0x90 : 0x80;
    pkt[1] = 0x80 | 0x54;
    SetWBE(  pkt + 2, 0x0007 );
    SetDWBE( pkt + 4, i_rtp_at_ntp );
    SetQWBE( pkt + 8, i_ntp );
    SetDWBE( pkt + 16, i_rtp_now );

    if ( p_sys->i_control_udp_fd >= 0 )
        net_Write( p_stream, p_sys->i_control_udp_fd, pkt, sizeof( pkt ) );
}

/* Build, encrypt, and send one verbatim ALAC packet from the PCM frame
 * currently sitting at the start of p_sys->pcm_acc. */
static int FlushOnePacket( sout_stream_t *p_stream )
{
    sout_stream_sys_t *p_sys = p_stream->p_sys;
    gcry_error_t i_gcrypt_err;
    const size_t i_payload = RAOP_ALAC_FRAME_BYTES;
    /* 12-byte RTP header (V/P/X/CC, M/PT, seq, timestamp, SSRC). */
    const size_t i_len = 12 + i_payload;

    /* Only i_rtp_ts is shared with another thread (the SYNC timer);
     * the rest is touched only on the input thread. */
    bool b_marker = p_sys->b_first_audio_packet;
    uint16_t i_seq = p_sys->i_seq;
    uint32_t i_ssrc = p_sys->i_ssrc;
    vlc_mutex_lock( &p_sys->lock );
    uint32_t i_rtp_ts = p_sys->i_rtp_ts;
    vlc_mutex_unlock( &p_sys->lock );

    p_sys->p_sendbuf[0] = 0x80;
    p_sys->p_sendbuf[1] = b_marker ? 0xe0 : 0x60;
    SetWBE(  p_sys->p_sendbuf + 2, i_seq );
    SetDWBE( p_sys->p_sendbuf + 4, i_rtp_ts );
    SetDWBE( p_sys->p_sendbuf + 8, i_ssrc );

    raop_alac_pack_verbatim( p_sys->p_sendbuf + 12, p_sys->pcm_acc );

    i_gcrypt_err = raop_alac_encrypt( p_sys->aes_ctx, p_sys->ps_aes_iv,
                                      p_sys->p_sendbuf + 12, i_payload );
    if ( CheckForGcryptError( p_stream, i_gcrypt_err ) )
        return VLC_EGENERIC;

    vlc_tick_t i_now = vlc_tick_now();
    if ( p_sys->i_next_packet_tick == 0 )
        p_sys->i_next_packet_tick = i_now;
    else if ( p_sys->i_next_packet_tick > i_now )
        vlc_tick_wait( p_sys->i_next_packet_tick );
    p_sys->i_next_packet_tick +=
        vlc_tick_from_samples( RAOP_FRAMES_PER_PACKET, RAOP_SAMPLE_RATE );

    int rc = net_Write( p_stream, p_sys->i_audio_udp_fd,
                        p_sys->p_sendbuf, i_len );
    if ( rc < 0 )
        return VLC_EGENERIC;

    p_sys->i_seq++;
    vlc_mutex_lock( &p_sys->lock );
    p_sys->i_rtp_ts += RAOP_FRAMES_PER_PACKET;
    vlc_mutex_unlock( &p_sys->lock );
    p_sys->b_first_audio_packet = false;

    return VLC_SUCCESS;
}

static void SendAudio( sout_stream_t *p_stream, block_t *p_buffer )
{
    sout_stream_sys_t *p_sys = p_stream->p_sys;

    while ( p_buffer )
    {
        const uint8_t *p = p_buffer->p_buffer;
        size_t left = p_buffer->i_buffer;

        while ( left > 0 )
        {
            size_t avail = sizeof( p_sys->pcm_acc ) - p_sys->i_pcm_acc;
            size_t take = ( left < avail ) ? left : avail;
            memcpy( p_sys->pcm_acc + p_sys->i_pcm_acc, p, take );
            p_sys->i_pcm_acc += take;
            p += take;
            left -= take;

            if ( p_sys->i_pcm_acc == sizeof( p_sys->pcm_acc ) )
            {
                if ( FlushOnePacket( p_stream ) != VLC_SUCCESS )
                {
                    block_ChainRelease( p_buffer );
                    return;
                }
                p_sys->i_pcm_acc = 0;
            }
        }

        block_t *p_next = p_buffer->p_next;
        block_Release( p_buffer );
        p_buffer = p_next;
    }
}

static const struct sout_stream_operations sink_ops = {
    .add = SinkAdd,
    .del = SinkDel,
    .send = SinkSend,
    .close = SinkClose,
};

static void SinkCleanup( sout_stream_sys_t *p_sys )
{
    if ( p_sys->b_sync_timer )
    {
        vlc_timer_destroy( p_sys->sync_timer );
        p_sys->b_sync_timer = false;
    }

    if ( p_sys->i_control_fd >= 0 )
    {
        net_Close( p_sys->i_control_fd );
        p_sys->i_control_fd = -1;
    }
    if ( p_sys->i_audio_udp_fd >= 0 )
    {
        net_Close( p_sys->i_audio_udp_fd );
        p_sys->i_audio_udp_fd = -1;
    }
    if ( p_sys->i_control_udp_fd >= 0 )
    {
        net_Close( p_sys->i_control_udp_fd );
        p_sys->i_control_udp_fd = -1;
    }
    if ( p_sys->i_timing_udp_fd >= 0 )
    {
        net_Close( p_sys->i_timing_udp_fd );
        p_sys->i_timing_udp_fd = -1;
    }

    if ( p_sys->aes_ctx != NULL )
    {
        gcry_cipher_close( p_sys->aes_ctx );
        p_sys->aes_ctx = NULL;
    }

    FREENULL( p_sys->p_sendbuf );

    FREENULL( p_sys->psz_url );
    FREENULL( p_sys->psz_session );
    FREENULL( p_sys->psz_client_instance );
    FREENULL( p_sys->psz_last_status_line );
}

/*****************************************************************************
 * SinkOpen: open the RTSP control connection and run the protocol handshake
 *****************************************************************************/
static int SinkOpen( vlc_object_t *p_this )
{
    sout_stream_t *p_stream = (sout_stream_t*)p_this;
    sout_stream_sys_t *p_sys;
    char psz_local[NI_MAXNUMERICHOST];
    gcry_error_t i_gcrypt_err;
    int i_err = VLC_EGENERIC;
    uint32_t i_session_id;
    uint64_t i_client_instance;

    vlc_gcrypt_init();

    p_sys = var_InheritAddress( p_this, SOUT_CFG_PREFIX "sys" );
    if ( p_sys == NULL )
        return VLC_EGENERIC;

    p_stream->p_sys = p_sys;
    p_stream->ops = &sink_ops;

    /* Open control connection */
    p_sys->i_control_fd = net_Connect( p_stream, p_sys->psz_host,
                                       p_sys->i_port, SOCK_STREAM,
                                       IPPROTO_TCP );
    if ( p_sys->i_control_fd < 0 )
    {
        msg_Err( p_this, "Cannot establish control connection to %s:%d (%s)",
                 p_sys->psz_host, p_sys->i_port, vlc_strerror_c(errno) );
        goto error;
    }

    /* Get local IP address */
    if ( net_GetSockAddress( p_sys->i_control_fd, psz_local, NULL ) )
    {
        msg_Err( p_this, "cannot get local IP address" );
        goto error;
    }

    /* Random session ID */
    vlc_rand_bytes( &i_session_id, sizeof( i_session_id ) );

    /* Random client instance */
    vlc_rand_bytes( &i_client_instance, sizeof( i_client_instance ) );
    if ( asprintf( &p_sys->psz_client_instance, "%016"PRIX64,
                   i_client_instance ) < 0 )
    {
        p_sys->psz_client_instance = NULL;
        i_err = VLC_ENOMEM;
        goto error;
    }

    /* Build session URL */
    if ( asprintf( &p_sys->psz_url, "rtsp://%s/%u",
                   psz_local, i_session_id ) < 0 )
    {
        p_sys->psz_url = NULL;
        i_err = VLC_ENOMEM;
        goto error;
    }

    /* Generate AES key and IV */
    gcry_randomize( p_sys->ps_aes_key, sizeof( p_sys->ps_aes_key ),
                    GCRY_STRONG_RANDOM );
    gcry_randomize( p_sys->ps_aes_iv, sizeof( p_sys->ps_aes_iv ),
                    GCRY_STRONG_RANDOM );

    /* Setup AES */
    i_gcrypt_err = gcry_cipher_open( &p_sys->aes_ctx, GCRY_CIPHER_AES,
                                     GCRY_CIPHER_MODE_CBC, 0 );
    if ( CheckForGcryptError( p_stream, i_gcrypt_err ) )
    {
        p_sys->aes_ctx = NULL;
        goto error;
    }

    /* Set key */
    i_gcrypt_err = gcry_cipher_setkey( p_sys->aes_ctx, p_sys->ps_aes_key,
                                       sizeof( p_sys->ps_aes_key ) );
    if ( CheckForGcryptError( p_stream, i_gcrypt_err ) )
        goto error;

    p_sys->p_sendbuf = malloc( 12 + RAOP_ALAC_FRAME_BYTES );
    if ( p_sys->p_sendbuf == NULL )
        return VLC_ENOMEM;

    /* Open local control + timing UDP sockets so we can advertise their
     * ports in SETUP. The audio socket is opened later, once the receiver
     * has told us its server_port. macOS' getaddrinfo() rejects NULL host
     * with port 0, so bind on the local IP from the RTSP connection. */
    p_sys->i_timing_udp_fd = net_ListenUDP1( p_this, psz_local, 0 );
    if ( p_sys->i_timing_udp_fd < 0 )
    {
        msg_Err( p_this, "Cannot open local timing UDP socket" );
        goto error;
    }
    if ( net_GetSockAddress( p_sys->i_timing_udp_fd, NULL,
                             &p_sys->i_local_timing_port ) )
    {
        msg_Err( p_this, "Cannot read local timing port" );
        goto error;
    }

    p_sys->i_control_udp_fd = net_ListenUDP1( p_this, psz_local, 0 );
    if ( p_sys->i_control_udp_fd < 0 )
    {
        msg_Err( p_this, "Cannot open local control UDP socket" );
        goto error;
    }
    if ( net_GetSockAddress( p_sys->i_control_udp_fd, NULL,
                             &p_sys->i_local_control_port ) )
    {
        msg_Err( p_this, "Cannot read local control port" );
        goto error;
    }

    /* Protocol handshake */
    i_err = AnnounceSDP( p_this, psz_local, i_session_id );
    if ( i_err != VLC_SUCCESS )
        goto error;

    /* ANNOUNCE accepted means any password we acquired was correct. */
    if ( p_sys->psz_password != NULL )
        vlc_credential_store( &p_sys->cred, p_this );

    i_err = SendSetup( p_this );
    if ( i_err != VLC_SUCCESS )
        goto error;

    i_err = SendRecord( p_this );
    if ( i_err != VLC_SUCCESS )
        goto error;

    /* Push title/artist/album/artwork to the receiver if the demux filter
     * has already populated the meta. Failure here is non-fatal. */
    if ( SendDmapMeta( p_this ) != VLC_SUCCESS )
        msg_Warn( p_this, "Failed to push metadata to receiver" );
    if ( SendArtwork( p_this ) != VLC_SUCCESS )
        msg_Warn( p_this, "Failed to push artwork to receiver" );

    LogInfo( p_this );

    /* Audio UDP socket: connected to the receiver's server_port */
    p_sys->i_audio_udp_fd = net_ConnectDgram( p_this, p_sys->psz_host,
                                              p_sys->i_server_audio_port,
                                              -1, IPPROTO_UDP );
    if ( p_sys->i_audio_udp_fd < 0 )
    {
        msg_Err( p_this, "Cannot establish audio UDP connection to %s:%d (%s)",
                 p_sys->psz_host, p_sys->i_server_audio_port,
                 vlc_strerror_c(errno) );
        i_err = VLC_EGENERIC;
        goto error;
    }

    /* If the receiver advertised a control port, connect() the bound local
     * control socket to it so we can send via net_Write. connect() on
     * an already-bound UDP socket sets the default peer without releasing
     * the local port we advertised in SETUP. */
    if ( p_sys->i_server_control_port > 0 )
    {
        struct addrinfo hints = { .ai_socktype = SOCK_DGRAM,
                                  .ai_protocol = IPPROTO_UDP };
        struct addrinfo *res = NULL;
        if ( vlc_getaddrinfo( p_sys->psz_host,
                              p_sys->i_server_control_port,
                              &hints, &res ) != 0 || res == NULL )
        {
            msg_Err( p_this, "Cannot resolve control peer %s:%d",
                     p_sys->psz_host, p_sys->i_server_control_port );
            i_err = VLC_EGENERIC;
            goto error;
        }
        if ( connect( p_sys->i_control_udp_fd,
                      res->ai_addr, res->ai_addrlen ) != 0 )
        {
            msg_Err( p_this, "Cannot connect control UDP socket to %s:%d (%s)",
                     p_sys->psz_host, p_sys->i_server_control_port,
                     vlc_strerror_c(errno) );
            freeaddrinfo( res );
            i_err = VLC_EGENERIC;
            goto error;
        }
        freeaddrinfo( res );
    }

    /* Initialize RTP state */
    vlc_rand_bytes( &p_sys->i_ssrc, sizeof( p_sys->i_ssrc ) );
    p_sys->i_seq = 0;
    p_sys->i_rtp_ts = 0;
    p_sys->b_first_audio_packet = true;
    p_sys->b_first_sync = true;
    p_sys->i_next_packet_tick = 0;
    p_sys->i_pcm_acc = 0;

    /* Schedule periodic SYNC */
    if ( p_sys->i_server_control_port > 0 )
    {
        if ( vlc_timer_create( &p_sys->sync_timer, SyncTimerCallback,
                               p_stream ) == 0 )
        {
            p_sys->b_sync_timer = true;
            vlc_timer_schedule( p_sys->sync_timer, false,
                                VLC_TICK_FROM_MS( 100 ),
                                VLC_TICK_FROM_MS( 1000 ) );
        }
        else
            msg_Warn( p_this, "Cannot create SYNC timer; continuing without" );
    }

    return VLC_SUCCESS;

error:
    SinkCleanup( p_sys );
    return i_err;
}

/*****************************************************************************
 * SinkClose: tear down the RTSP session; the wrapper frees the shared sys
 *****************************************************************************/
static void SinkClose( sout_stream_t *p_stream )
{
    sout_stream_sys_t *p_sys = p_stream->p_sys;
    vlc_object_t *p_this = VLC_OBJECT( p_stream );

    SendFlush( p_this );
    SendTeardown( p_this );

    SinkCleanup( p_sys );
}

static void *SinkAdd( sout_stream_t *p_stream, const es_format_t *p_fmt,
                      const char *es_id )
{
    sout_stream_sys_t *p_sys = p_stream->p_sys;
    sout_stream_id_sys_t *id = NULL;

    VLC_UNUSED( es_id );

    id = calloc( 1, sizeof( *id ) );
    if ( id == NULL )
        return NULL;

    es_format_Copy( &id->fmt, p_fmt );

    if ( id->fmt.i_cat == AUDIO_ES &&
         id->fmt.i_codec == VLC_CODEC_S16B &&
         id->fmt.audio.i_rate == 44100 &&
         id->fmt.audio.i_channels == 2 )
    {
        if ( p_sys->p_audio_stream )
            msg_Warn( p_stream, "ignoring extra audio stream" );
        else
            p_sys->p_audio_stream = id;
    }
    else
    {
        msg_Warn( p_stream, "ignoring stream: need s16b/44100/2ch, "
                            "got %4.4s/%u/%u",
                  (const char *)&id->fmt.i_codec,
                  id->fmt.audio.i_rate, id->fmt.audio.i_channels );
    }

    return id;
}

static void SinkDel( sout_stream_t *p_stream, void *_id )
{
    sout_stream_sys_t *p_sys = p_stream->p_sys;
    sout_stream_id_sys_t *id = _id;

    if ( p_sys->p_audio_stream == id )
        p_sys->p_audio_stream = NULL;

    FreeId( id );
}

static int SinkSend( sout_stream_t *p_stream, void *_id, block_t *p_buffer )
{
    sout_stream_sys_t *p_sys = p_stream->p_sys;
    sout_stream_id_sys_t *id = _id;

    if ( id->fmt.i_cat == AUDIO_ES && id == p_sys->p_audio_stream )
    {
        /* SendAudio takes care of releasing the buffers */
        SendAudio( p_stream, p_buffer );
    }
    else
    {
        block_ChainRelease( p_buffer );
    }

    return VLC_SUCCESS;
}

/*****************************************************************************
 * Wrapper: forwards ES handling through an internal transcode->raop-sink
 * chain. We always transcode to signed 16-bit big-endian PCM at 44.1 kHz
 * stereo: the sink then bit-packs each chunk into a verbatim (uncompressed)
 * ALAC frame, which is what RAOP receivers expect on the wire. We can't
 * passthrough the source's ALAC because the rice params buried in the
 * source's magic cookie may not match the standard fmtp we advertise; and
 * we can't re-encode via FFmpeg because its ALAC params don't match either.
 *****************************************************************************/
typedef struct
{
    sout_stream_sys_t *p_shared;
    sout_stream_t *p_out;
} raop_wrapper_sys_t;

static void *Add( sout_stream_t *p_stream, const es_format_t *p_fmt,
                  const char *es_id )
{
    raop_wrapper_sys_t *p_wrap = p_stream->p_sys;

    if ( p_wrap->p_out == NULL )
    {
        p_wrap->p_out = sout_StreamChainNew( VLC_OBJECT( p_stream ),
            "transcode{acodec=s16b,channels=2,samplerate=44100}:raop-sink",
            NULL );
        if ( p_wrap->p_out == NULL )
        {
            msg_Err( p_stream, "Cannot create internal sout chain" );
            return NULL;
        }
    }

    return sout_StreamIdAdd( p_wrap->p_out, p_fmt, es_id );
}

static void Del( sout_stream_t *p_stream, void *id )
{
    raop_wrapper_sys_t *p_wrap = p_stream->p_sys;
    if ( p_wrap->p_out != NULL )
        sout_StreamIdDel( p_wrap->p_out, id );
}

static int Send( sout_stream_t *p_stream, void *id, block_t *p_buffer )
{
    raop_wrapper_sys_t *p_wrap = p_stream->p_sys;
    if ( p_wrap->p_out == NULL )
    {
        block_ChainRelease( p_buffer );
        return VLC_SUCCESS;
    }
    return sout_StreamIdSend( p_wrap->p_out, id, p_buffer );
}

static void Close( sout_stream_t *p_stream )
{
    raop_wrapper_sys_t *p_wrap = p_stream->p_sys;
    sout_stream_sys_t *p_sys = p_wrap->p_shared;

    if ( p_wrap->p_out != NULL )
        sout_StreamChainDelete( p_wrap->p_out, NULL );
    var_Destroy( p_stream, SOUT_CFG_PREFIX "sys" );
    FreeSys( VLC_OBJECT( p_stream ), p_sys );
    free( p_wrap );
}

static const struct sout_stream_operations wrapper_ops = {
    .add = Add,
    .del = Del,
    .send = Send,
    .close = Close,
};

static int Open( vlc_object_t *p_this )
{
    sout_stream_t *p_stream = (sout_stream_t*)p_this;
    raop_wrapper_sys_t *p_wrap = NULL;
    sout_stream_sys_t *p_sys;
    bool b_sys_var = false;
    int i_err = VLC_EGENERIC;

    config_ChainParse( p_stream, SOUT_CFG_PREFIX, ppsz_sout_options,
                       p_stream->p_cfg );

    p_sys = calloc( 1, sizeof( *p_sys ) );
    if ( p_sys == NULL )
        return VLC_ENOMEM;

    p_sys->i_control_fd = -1;
    p_sys->i_audio_udp_fd = -1;
    p_sys->i_control_udp_fd = -1;
    p_sys->i_timing_udp_fd = -1;
    p_sys->i_volume = var_GetInteger( p_stream, SOUT_CFG_PREFIX "volume" );
    p_sys->i_jack_type = JACK_TYPE_NONE;
    vlc_mutex_init( &p_sys->lock );
    vlc_http_auth_Init( &p_sys->auth );

    p_sys->psz_host = var_GetNonEmptyString( p_stream, SOUT_CFG_PREFIX "ip" );
    if ( p_sys->psz_host == NULL )
    {
        msg_Err( p_this, "Missing host" );
        goto error;
    }

    p_sys->i_port = var_GetInteger( p_stream, SOUT_CFG_PREFIX "port" );
    if ( p_sys->i_port <= 0 || p_sys->i_port > 65535 )
        p_sys->i_port = RAOP_PORT;

    /* Credential URL keys keystore lookups by host+port. The "iTunes"
     * username matches what we send in the Digest header for the legacy
     * "raop" realm and gives the dialog a sensible default. */
    char *psz_cred_url;
    if ( asprintf( &psz_cred_url, "raop://iTunes@%s:%d/",
                   p_sys->psz_host, p_sys->i_port ) < 0 )
    {
        i_err = VLC_ENOMEM;
        goto error;
    }
    int i_url_ret = vlc_UrlParse( &p_sys->cred_url, psz_cred_url );
    free( psz_cred_url );
    if ( i_url_ret != 0 )
        goto error;
    vlc_credential_init( &p_sys->cred, &p_sys->cred_url );
    p_sys->b_cred_init = true;

    var_Create( p_stream, SOUT_CFG_PREFIX "sys", VLC_VAR_ADDRESS );
    var_SetAddress( p_stream, SOUT_CFG_PREFIX "sys", p_sys );
    b_sys_var = true;

    var_AddCallback( p_stream, SOUT_CFG_PREFIX "volume",
                     VolumeCallback, NULL );
    p_sys->b_volume_callback = true;

    /* Expose the metadata-push interface so the matching raop_demux filter
     * can find it via var_InheritAddress. The variable lives two levels up
     * (typically the input_thread) so the demux side, which is in a sibling
     * subtree, can still inherit it. */
    p_sys->common.p_opaque = p_sys;
    p_sys->common.pf_set_meta = SetMetaCb;
    p_sys->p_common_owner = vlc_object_parent( vlc_object_parent( p_stream ) );
    if ( p_sys->p_common_owner != NULL
      && var_Create( p_sys->p_common_owner, RAOP_SHARED_VAR_NAME,
                     VLC_VAR_ADDRESS ) == VLC_SUCCESS )
    {
        var_SetAddress( p_sys->p_common_owner, RAOP_SHARED_VAR_NAME,
                        &p_sys->common );
        p_sys->b_common_var = true;
    }

    p_wrap = malloc( sizeof( *p_wrap ) );
    if ( p_wrap == NULL )
    {
        i_err = VLC_ENOMEM;
        goto error;
    }
    p_wrap->p_shared = p_sys;
    p_wrap->p_out = NULL;   /* built lazily on first Add() */

    p_stream->p_sys = p_wrap;
    p_stream->ops = &wrapper_ops;
    return VLC_SUCCESS;

error:
    free( p_wrap );
    if ( b_sys_var )
        var_Destroy( p_stream, SOUT_CFG_PREFIX "sys" );
    FreeSys( p_this, p_sys );
    return i_err;
}

/*****************************************************************************
 * VolumeCallback: called when the volume is changed on the fly.
 *****************************************************************************/
static int VolumeCallback( vlc_object_t *p_this, char const *psz_cmd,
                           vlc_value_t oldval, vlc_value_t newval,
                           void *p_data )
{
    VLC_UNUSED(p_this);
    VLC_UNUSED(psz_cmd);
    VLC_UNUSED(oldval);
    VLC_UNUSED(newval);
    VLC_UNUSED(p_data);

    /* TODO: Implement volume change */
    return VLC_SUCCESS;
}
