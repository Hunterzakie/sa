/*****************************************************************************
 * raop_demux.c: pushes input-item metadata into the RAOP sout
 *****************************************************************************
 * Copyright (C) 2026 VLC authors and VideoLAN
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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_demux.h>
#include <vlc_meta.h>

#include "raop_common.h"

typedef struct
{
    raop_common *p_renderer;
} raop_demux_sys_t;

static void PushMeta( demux_t *p_demux, raop_common *p_renderer )
{
    input_item_t *p_item = p_demux->s ? p_demux->s->p_input_item : NULL;
    if( p_item == NULL )
        return;

    vlc_meta_t *p_meta = vlc_meta_New();
    if( unlikely( p_meta == NULL ) )
        return;

    for( int i = 0; i < VLC_META_TYPE_COUNT; ++i )
    {
        char *psz = input_item_GetMeta( p_item, (vlc_meta_type_t)i );
        if( psz != NULL )
        {
            vlc_meta_Set( p_meta, (vlc_meta_type_t)i, psz );
            free( psz );
        }
    }

    if( vlc_meta_Get( p_meta, vlc_meta_Title ) == NULL )
    {
        char *psz_name = input_item_GetName( p_item );
        if( psz_name != NULL )
        {
            vlc_meta_Set( p_meta, vlc_meta_Title, psz_name );
            free( psz_name );
        }
    }

    p_renderer->pf_set_meta( p_renderer->p_opaque, p_meta );
}

static int Demux( demux_t *p_demux )
{
    return demux_Demux( p_demux->s );
}

static int Control( demux_t *p_demux, int i_query, va_list args )
{
    raop_demux_sys_t *p_sys = p_demux->p_sys;

    if( i_query == DEMUX_FILTER_ENABLE )
    {
        p_sys->p_renderer = var_InheritAddress( p_demux, RAOP_SHARED_VAR_NAME );
        if( p_sys->p_renderer != NULL )
            PushMeta( p_demux, p_sys->p_renderer );
        return VLC_SUCCESS;
    }

    if( i_query == DEMUX_FILTER_DISABLE )
    {
        p_sys->p_renderer = NULL;
        return VLC_SUCCESS;
    }

    return demux_vaControl( p_demux->s, i_query, args );
}

static int Open( vlc_object_t *p_this )
{
    demux_t *p_demux = (demux_t *)p_this;
    raop_common *p_renderer = var_InheritAddress( p_demux, RAOP_SHARED_VAR_NAME );
    if( p_renderer == NULL )
        return VLC_EGENERIC;

    raop_demux_sys_t *p_sys = malloc( sizeof( *p_sys ) );
    if( unlikely( p_sys == NULL ) )
        return VLC_ENOMEM;

    p_sys->p_renderer = p_renderer;
    p_demux->p_sys = p_sys;
    p_demux->pf_demux = Demux;
    p_demux->pf_control = Control;

    PushMeta( p_demux, p_renderer );
    return VLC_SUCCESS;
}

static void Close( vlc_object_t *p_this )
{
    demux_t *p_demux = (demux_t *)p_this;
    raop_demux_sys_t *p_sys = p_demux->p_sys;

    /* Clear the sout's stored meta on track change. Without this, the next
     * track's PushMeta would happen against a still-populated p_meta in the
     * sout, leaving a stale window from track close to next track Open. */
    if( p_sys->p_renderer != NULL )
        p_sys->p_renderer->pf_set_meta( p_sys->p_renderer->p_opaque, NULL );

    free( p_sys );
}

vlc_module_begin()
    set_shortname( "raop_demux" )
    set_subcategory( SUBCAT_INPUT_DEMUX )
    set_description( N_("RAOP metadata push") )
    set_capability( "demux_filter", 0 )
    add_shortcut( "raop_demux" )
    set_callbacks( Open, Close )
vlc_module_end()
