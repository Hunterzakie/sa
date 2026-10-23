/*****************************************************************************
 * stereo_srs_wow.c : SRS WoW-style 3D stereo widening effect
 *****************************************************************************
 * Copyright (C) 2026 VLC authors and VideoLAN
 *
 * Author: Alex Hojin Park <alexhojinpark at gmail dot com>
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

/*
 * 3D stereo widening with psychoacoustic bass enhancement.
 *
 * Signal flow:
 *   Stereo L/R
 *     -> M/S decomposition   M = (L+R)/2,  S = (L-R)/2
 *     -> Bass mono-lock HPF  (high-pass Side, keeps low-end mono)
 *     -> High-shelf EQ on S  (boosts the bass-locked Side above bass_freq)
 *     -> TruBass             (synthesises bass harmonics on Mid)
 *     -> Re-matrix           L' = M + tb + S_eq,  R' = M + tb - S_eq
 *     -> Soft limiter        (gain normalisation, prevents clipping)
 *
 * Biquad coefficients follow the RBJ Audio EQ Cookbook:
 *   https://www.w3.org/TR/audio-eq-cookbook/
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <math.h>
#include <vlc_common.h>
#include <vlc_aout.h>
#include <vlc_filter.h>
#include <vlc_plugin.h>

/*****************************************************************************
 * Local prototypes
 *****************************************************************************/
static int Open( vlc_object_t * );

/*****************************************************************************
 * Module strings
 *****************************************************************************/
#define HELP_TEXT N_( \
    "SRS WoW-style 3D stereo widening using M/S decomposition. " \
    "Boosts the Side (difference) channel above the bass mono-lock " \
    "frequency for a wider stereo image while keeping low frequencies mono." )

#define WIDTH_TEXT      N_("Widening amount")
#define WIDTH_LONGTEXT  N_("How much the stereo image is widened. " \
    "0.0 = bypass, 1.0 = maximum effect.")

#define BASS_FREQ_TEXT      N_("Bass mono-lock frequency (Hz)")
#define BASS_FREQ_LONGTEXT  N_("Frequencies below this threshold are kept " \
    "mono to avoid phase problems on mono playback. Recommended: ~200 Hz.")

#define SIDE_GAIN_TEXT      N_("Side channel shelf gain (dB)")
#define SIDE_GAIN_LONGTEXT  N_("Boost applied to the Side (difference) signal " \
    "above the bass mono-lock frequency.")

#define TRUEBASS_GAIN_TEXT      N_("TruBass amount")
#define TRUEBASS_GAIN_LONGTEXT  N_("Psychoacoustic bass enhancement. Synthesises " \
    "harmonics of the low end so small speakers perceive deeper bass. " \
    "0.0 = off, 1.0 = maximum.")

#define TRUEBASS_FREQ_TEXT      N_("TruBass crossover frequency (Hz)")
#define TRUEBASS_FREQ_LONGTEXT  N_("Bass frequencies below this point are used " \
    "to generate enhancement harmonics. Typically the speaker's low cutoff.")

#define CONFIG_PREFIX "srswow-"

/*****************************************************************************
 * Module descriptor
 *****************************************************************************/
vlc_module_begin ()
    set_shortname( N_("SRS WoW") )
    set_description( N_("SRS WoW 3D stereo widening") )
    set_help( HELP_TEXT )
    set_subcategory( SUBCAT_AUDIO_AFILTER )
    set_capability( "audio filter", 0 )
    set_callback( Open )

    add_float_with_range( CONFIG_PREFIX "width", 0.5f, 0.0f, 1.0f,
        WIDTH_TEXT, WIDTH_LONGTEXT )
    add_float_with_range( CONFIG_PREFIX "bass-freq", 200.f, 20.f, 800.f,
        BASS_FREQ_TEXT, BASS_FREQ_LONGTEXT )
    add_float_with_range( CONFIG_PREFIX "side-gain", 6.f, 0.f, 18.f,
        SIDE_GAIN_TEXT, SIDE_GAIN_LONGTEXT )
    add_float_with_range( CONFIG_PREFIX "truebass-gain", 0.f, 0.f, 1.0f,
        TRUEBASS_GAIN_TEXT, TRUEBASS_GAIN_LONGTEXT )
    add_float_with_range( CONFIG_PREFIX "truebass-freq", 90.f, 40.f, 200.f,
        TRUEBASS_FREQ_TEXT, TRUEBASS_FREQ_LONGTEXT )
vlc_module_end ()

/* Biquad filter -- Transposed Direct Form II */
typedef struct
{
    float b0, b1, b2;   /* feedforward coefficients */
    float a1, a2;       /* feedback coefficients    */
    float w1, w2;       /* filter state             */
} biquad_t;

static inline float biquad_process( biquad_t *f, float x )
{
    float y  = f->b0 * x + f->w1;
    f->w1    = f->b1 * x - f->a1 * y + f->w2;
    f->w2    = f->b2 * x - f->a2 * y;
    return y;
}

/* High-shelf: boosts Side channel above shelf_freq by gain_db */
static void biquad_highshelf( biquad_t *f, float shelf_freq,
                              float gain_db, unsigned rate )
{
    float A     = powf( 10.f, gain_db / 40.f );
    float w0    = 2.f * (float)M_PI * shelf_freq / (float)rate;
    float cosw  = cosf( w0 );
    float S     = 1.f; /* shelf slope - 1.0 = maximally flat transition */
    float alpha = sinf( w0 ) / 2.f
                  * sqrtf( (A + 1.f / A) * (1.f / S - 1.f) + 2.f );

    /* a0 is the normalisation divisor, applied once below */
    float b0 =        A * ( (A+1.f) + (A-1.f)*cosw + 2.f*sqrtf(A)*alpha );
    float b1 = -2.f * A * ( (A-1.f) + (A+1.f)*cosw                      );
    float b2 =        A * ( (A+1.f) + (A-1.f)*cosw - 2.f*sqrtf(A)*alpha );
    float a0 =            ( (A+1.f) - (A-1.f)*cosw + 2.f*sqrtf(A)*alpha );
    float a1 =  2.f *     ( (A-1.f) - (A+1.f)*cosw                      );
    float a2 =            ( (A+1.f) - (A-1.f)*cosw - 2.f*sqrtf(A)*alpha );

    f->b0 = b0/a0; f->b1 = b1/a0; f->b2 = b2/a0;
    f->a1 = a1/a0; f->a2 = a2/a0;
    f->w1 = f->w2 = 0.f;
}

/* 2nd-order Butterworth LPF -- isolates the bass fundamental for TruBass */
static void biquad_lpf( biquad_t *f, float cutoff_freq, unsigned rate )
{
    float w0    = 2.f * (float)M_PI * cutoff_freq / (float)rate;
    float alpha = sinf( w0 ) / ( 2.f * 0.70710678f ); /* Q = 1/sqrt(2) */

    float b0 =  (1.f - cosf(w0)) * 0.5f;
    float b1 =   1.f - cosf(w0);
    float b2 =  (1.f - cosf(w0)) * 0.5f;

    float a0 =   1.f + alpha;
    float a1 =  -2.f * cosf(w0);
    float a2 =   1.f - alpha;

    f->b0 = b0/a0; f->b1 = b1/a0; f->b2 = b2/a0;
    f->a1 = a1/a0; f->a2 = a2/a0;
    f->w1 = f->w2 = 0.f;
}

/* 2nd-order Butterworth HPF -- bass mono-lock on the Side, and TruBass
 * fundamental/DC removal (two in series there). */
static void biquad_hpf( biquad_t *f, float cutoff_freq, unsigned rate )
{
    float w0    = 2.f * (float)M_PI * cutoff_freq / (float)rate;
    float alpha = sinf( w0 ) / ( 2.f * 0.70710678f ); /* Q = 1/sqrt(2) */

    float b0 =  (1.f + cosf(w0)) * 0.5f;
    float b1 = -(1.f + cosf(w0));
    float b2 =  (1.f + cosf(w0)) * 0.5f;

    float a0 =   1.f + alpha;
    float a1 =  -2.f * cosf(w0);
    float a2 =   1.f - alpha;

    f->b0 = b0/a0; f->b1 = b1/a0; f->b2 = b2/a0;
    f->a1 = a1/a0; f->a2 = a2/a0;
    f->w1 = f->w2 = 0.f;
}

/* TruBass NLD: missing-fundamental bass enhancement. Blends the 2nd (x*x)
 * and 3rd (x*x*x) harmonics; the DC and fundamental it also produces are
 * removed downstream by the HPF pair. */
static inline float trubass_generate( float x )
{
    return x * x + x * x * x;   /* 2nd + 3rd harmonic generation */
}

/* Soft limiter: transparent below the threshold, tanh saturation above, so
 * the stacked shelf and TruBass gains approach but never exceed +-1.0. */
static inline float soft_limit( float x )
{
    const float T = 0.8f; /* linear region: |x| <= T */
    float a = fabsf( x );
    if( a <= T ) return x;
    float over = ( a - T ) / ( 1.f - T ); /* normalised overshoot    */
    float comp = T + ( 1.f - T ) * tanhf( over ); /* compressed into [T,1)   */
    return x < 0.f ? -comp : comp;
}

/* One-pole asymmetric smoother: rises with attack, falls with release. */
static inline float ar_average( float state, float level, float attack, float release )
{
    return state + ( ( level > state ) ? attack : release ) * ( level - state );
}

/*****************************************************************************
 * filter_sys_t -- DSP state
 *****************************************************************************/
typedef struct
{
    /* Parameters and their derived coefficients are read on the audio thread
     * and written on the control thread, so one mutex guards the group. */
    vlc_mutex_t lock;
    float    f_width;           /* widening amount     [0.0 - 1.0]  */
    float    f_bass_freq;       /* mono-lock cutoff Hz [20  - 500]  */
    float    f_side_gain_db;    /* shelf boost dB      [0   - 18]   */
    float    f_truebass_gain;   /* TruBass mix amount  [0.0 - 1.0]  */
    float    f_truebass_freq;   /* TruBass crossover Hz [40 - 200]  */

    /* Biquad filter state (L and R tracked separately). The Side is
     * high-passed (bass mono-lock) and then shelved, in series. */
    biquad_t hpf_l;             /* bass mono-lock HPF, Side left    */
    biquad_t hpf_r;             /* bass mono-lock HPF, Side right   */
    biquad_t shelf_l;           /* high-shelf EQ, Side left         */
    biquad_t shelf_r;           /* high-shelf EQ, Side right        */

    /* TruBass on the Mid: the LPF pair isolates the bass, the HPF pair strips
     * the fundamental and DC from the generated harmonics (both 4th order). */
    biquad_t tb_lpf;            /* isolate bass from Mid (stage 1)  */
    biquad_t tb_lpf2;           /* isolate bass from Mid (stage 2)  */
    biquad_t tb_hpf;            /* strip fundamental (stage 1)      */
    biquad_t tb_hpf2;           /* strip fundamental (stage 2)      */

    /* Level control for the NLD: normalise by the peak before shaping, then
     * re-apply the envelope, so the harmonics stay level-independent. */
    float    tb_env;           /* smoothed |bass| (state)          */
    float    tb_peak;          /* fast peak follower (state)       */
    float    tb_attack;        /* attack coef (fast rise)          */
    float    tb_release;       /* release coef (slow fall)         */
    float    tb_peak_rel;      /* peak follower release coef       */

    unsigned i_rate;           /* sample rate, for coefficient calculation */
} filter_sys_t;

/* Recompute coefficients from the parameters. Caller holds p_sys->lock
 * (except in Open, before processing starts). */
static void UpdateFilters( filter_sys_t *p_sys )
{
    /* Scale shelf gain by width so width=0.0 produces no EQ change */
    float effective_gain = p_sys->f_side_gain_db * p_sys->f_width;

    /* Side mono-lock high-pass and widening shelf, both at f_bass_freq */
    biquad_hpf( &p_sys->hpf_l, p_sys->f_bass_freq, p_sys->i_rate );
    biquad_hpf( &p_sys->hpf_r, p_sys->f_bass_freq, p_sys->i_rate );
    biquad_highshelf( &p_sys->shelf_l, p_sys->f_bass_freq,
                      effective_gain, p_sys->i_rate );
    biquad_highshelf( &p_sys->shelf_r, p_sys->f_bass_freq,
                      effective_gain, p_sys->i_rate );

    /* TruBass: isolate bass below the crossover, then keep only the
     * harmonics it generates above the crossover. */
    biquad_lpf( &p_sys->tb_lpf, p_sys->f_truebass_freq, p_sys->i_rate );
    biquad_lpf( &p_sys->tb_lpf2, p_sys->f_truebass_freq, p_sys->i_rate );
    biquad_hpf( &p_sys->tb_hpf, p_sys->f_truebass_freq, p_sys->i_rate );
    biquad_hpf( &p_sys->tb_hpf2, p_sys->f_truebass_freq, p_sys->i_rate );

    /* Follower time constants: envelope 5 ms attack / 100 ms release,
     * peak instant attack / 200 ms release. */
    p_sys->tb_attack   = 1.f - expf( -1.f / ( 0.005f * (float)p_sys->i_rate ) );
    p_sys->tb_release  = 1.f - expf( -1.f / ( 0.100f * (float)p_sys->i_rate ) );
    p_sys->tb_peak_rel = 1.f - expf( -1.f / ( 0.200f * (float)p_sys->i_rate ) );
}

/*****************************************************************************
 * paramCallback: update parameters live while filter is running
 *****************************************************************************/
static int paramCallback( vlc_object_t *p_this, char const *psz_var,
                          vlc_value_t oldval, vlc_value_t newval,
                          void *p_data )
{
    filter_sys_t *p_sys = p_data;
    VLC_UNUSED( p_this );
    VLC_UNUSED( oldval );

    vlc_mutex_lock( &p_sys->lock );

    if( !strcmp( psz_var, CONFIG_PREFIX "width" ) )
        p_sys->f_width = newval.f_float;
    else if( !strcmp( psz_var, CONFIG_PREFIX "bass-freq" ) )
        p_sys->f_bass_freq = newval.f_float;
    else if( !strcmp( psz_var, CONFIG_PREFIX "side-gain" ) )
        p_sys->f_side_gain_db = newval.f_float;
    else if( !strcmp( psz_var, CONFIG_PREFIX "truebass-gain" ) )
        p_sys->f_truebass_gain = newval.f_float;
    else if( !strcmp( psz_var, CONFIG_PREFIX "truebass-freq" ) )
        p_sys->f_truebass_freq = newval.f_float;

    /* Parameters are interdependent, so recompute all coefficients as a
     * group while still holding the lock. */
    UpdateFilters( p_sys );

    vlc_mutex_unlock( &p_sys->lock );
    return VLC_SUCCESS;
}

/*****************************************************************************
 * Filter: Mid/Side decomposition -> bass mono-lock -> side EQ -> TruBass -> re-matrix
 *****************************************************************************/
static block_t *Filter( filter_t *p_filter, block_t *p_block )
{
    filter_sys_t *p_sys = p_filter->p_sys;
    float        *p_out = (float *)p_block->p_buffer;

    /* Hold the lock for the whole block so parameters cannot change mid-way. */
    vlc_mutex_lock( &p_sys->lock );

    /* width or shelf gain of 0 bypasses the Side chain (an identity shelf
     * would still leave the mono-lock high-pass colouring the Side). */
    const bool b_widen   = p_sys->f_side_gain_db * p_sys->f_width > 0.f;
    const bool b_trubass = p_sys->f_truebass_gain > 0.f;

    if( !b_widen && !b_trubass )
    {   /* Nothing enabled: leave the block untouched */
        vlc_mutex_unlock( &p_sys->lock );
        return p_block;
    }

    for( unsigned i = p_block->i_nb_samples; i > 0; i-- )
    {
        float L = p_out[0];
        float R = p_out[1];

        /* Step 1: M/S decomposition */
        float M = ( L + R ) * 0.5f;    /* Mid  - mono-compatible centre */
        float S = ( L - R ) * 0.5f;    /* Side - stereo difference      */

        float S_eq_l, S_eq_r;
        if( b_widen )
        {
            /* Step 2: bass mono-lock -- high-pass the Side so lows stay mono
             * (hpf_r runs the sign-inverted Side for the R output). */
            float S_hp_l = biquad_process( &p_sys->hpf_l,  S );
            float S_hp_r = biquad_process( &p_sys->hpf_r, -S );

            /* Step 3: high-shelf EQ on the mono-locked Side (widening) */
            S_eq_l = biquad_process( &p_sys->shelf_l, S_hp_l );
            S_eq_r = biquad_process( &p_sys->shelf_r, S_hp_r );
        }
        else
        {   /* No widening requested: the Side passes through unchanged */
            S_eq_l = S;
            S_eq_r = -S;
        }

        /* Step 4: TruBass -- synthesise bass harmonics on the Mid (bass is
         * non-directional, so it stays centred). */
        float tb = 0.f;
        if( b_trubass )
        {
            float bass = biquad_process( &p_sys->tb_lpf,  M );    /* isolate */
            bass = biquad_process( &p_sys->tb_lpf2, bass );

            float level = fabsf( bass );
            p_sys->tb_env  = ar_average( p_sys->tb_env,  level,
                                         p_sys->tb_attack, p_sys->tb_release );
            p_sys->tb_peak = ar_average( p_sys->tb_peak, level,
                                         1.f, p_sys->tb_peak_rel );

            // Use tb_peak instead of tb_env to compute norm
            float norm = bass / fmaxf( p_sys->tb_peak, 1e-4f );
            if( norm >  1.f ) norm =  1.f;
            else if( norm < -1.f ) norm = -1.f;

            float harm = trubass_generate( norm );              /* synthesise */
            /* Two high-passes in series */
            harm = biquad_process( &p_sys->tb_hpf,  harm );
            harm = biquad_process( &p_sys->tb_hpf2, harm );
            tb = harm * p_sys->tb_env * p_sys->f_truebass_gain; /* dynamics   */
        }

        /* Step 5: re-matrix (S_eq_r already sign-flipped, tb is centred),
         * then soft_limit so the stacked gains cannot clip. */
        p_out[0] = soft_limit( M + tb + S_eq_l );
        p_out[1] = soft_limit( M + tb + S_eq_r );

        p_out += 2;
    }

    vlc_mutex_unlock( &p_sys->lock );
    return p_block;
}

/*****************************************************************************
 * Close
 *****************************************************************************/
static void Close( filter_t *p_filter )
{
    vlc_object_t *p_aout = vlc_object_parent( p_filter );
    filter_sys_t *p_sys  = p_filter->p_sys;

#define DEL_VAR( var ) \
    var_DelCallback( p_aout, var, paramCallback, p_sys ); \
    var_Destroy( p_aout, var );

    DEL_VAR( CONFIG_PREFIX "width" )
    DEL_VAR( CONFIG_PREFIX "bass-freq" )
    DEL_VAR( CONFIG_PREFIX "side-gain" )
    DEL_VAR( CONFIG_PREFIX "truebass-gain" )
    DEL_VAR( CONFIG_PREFIX "truebass-freq" )
#undef DEL_VAR

    free( p_sys );
}

/*****************************************************************************
 * Open
 *****************************************************************************/
static int Open( vlc_object_t *obj )
{
    filter_t     *p_filter = (filter_t *)obj;
    vlc_object_t *p_aout   = vlc_object_parent( p_filter );

    /* format check */
    if( p_filter->fmt_in.audio.i_format != VLC_CODEC_FL32 ||
        !AOUT_FMTS_IDENTICAL( &p_filter->fmt_in.audio,
                              &p_filter->fmt_out.audio ) )
        return VLC_EGENERIC;

    /* stereo check */
    if( p_filter->fmt_in.audio.i_channels != 2 )
    {
        msg_Err( p_filter, "srswow requires stereo input" );
        return VLC_EGENERIC;
    }

    filter_sys_t *p_sys = malloc( sizeof( filter_sys_t ) );
    if( unlikely( !p_sys ) )
        return VLC_ENOMEM;
    p_filter->p_sys = p_sys;

    p_sys->i_rate = p_filter->fmt_in.audio.i_rate;
    p_sys->tb_env = p_sys->tb_peak = 0.f;  /* followers start at silence */
    vlc_mutex_init( &p_sys->lock );

#define CREATE_VAR( field, var ) \
    p_sys->field = var_CreateGetFloat( p_aout, var ); \
    var_AddCallback( p_aout, var, paramCallback, p_sys );

    CREATE_VAR( f_width,         CONFIG_PREFIX "width" )
    CREATE_VAR( f_bass_freq,     CONFIG_PREFIX "bass-freq" )
    CREATE_VAR( f_side_gain_db,  CONFIG_PREFIX "side-gain" )
    CREATE_VAR( f_truebass_gain, CONFIG_PREFIX "truebass-gain" )
    CREATE_VAR( f_truebass_freq, CONFIG_PREFIX "truebass-freq" )
#undef CREATE_VAR

    UpdateFilters( p_sys );

    static const struct vlc_filter_operations filter_ops =
    {
        .filter_audio = Filter, .close = Close,
    };
    p_filter->ops = &filter_ops;
    return VLC_SUCCESS;
}
