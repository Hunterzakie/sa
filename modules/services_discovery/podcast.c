/*****************************************************************************
 * podcast.c:  Podcast services discovery module
 *****************************************************************************
 * Copyright (C) 2005-2009 the VideoLAN team
 *
 * Authors: Antoine Cellerier <dionoea -at- videolan -dot- org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

/*****************************************************************************
 * Includes
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#define VLC_MODULE_LICENSE VLC_LICENSE_GPL_2_PLUS
#include <vlc_common.h>
#include <vlc_configuration.h>
#include <vlc_memstream.h>
#include <vlc_plugin.h>
#include <vlc_services_discovery.h>
#include <vlc_vector.h>

#include <string.h>

/*****************************************************************************
 * Module descriptor
 *****************************************************************************/

static int  Open ( vlc_object_t * );
static void Close( vlc_object_t * );

#define CONFIG_KEY "podcast-urls"

VLC_SD_PROBE_HELPER_CAP("podcast", N_("Podcasts"), SD_CAT_INTERNET,
                        SD_CAP_CONFIGURABLE)

#define URLS_TEXT N_("Podcast URLs list")
#define URLS_LONGTEXT N_("Enter the list of podcasts to retrieve, " \
                         "separated by '|' (pipe)." )

vlc_module_begin ()
    set_shortname( "Podcast")
    set_description( N_("Podcasts") )
    set_subcategory( SUBCAT_PLAYLIST_SD )

    add_string( CONFIG_KEY, NULL, URLS_TEXT, URLS_LONGTEXT )

    set_capability( "services_discovery", 0 )
    set_callbacks( Open, Close )

    VLC_SD_PROBE_SUBMODULE

vlc_module_end ()

/*****************************************************************************
 * Local structures
 *****************************************************************************/

typedef struct
{
    int id;
    char *uri;
    input_item_t *item;
} entry_t;

typedef struct VLC_VECTOR(entry_t) entry_vector_t;

typedef struct
{
    entry_vector_t entries;
    int next_id;

    vlc_mutex_t lock;
} services_discovery_sys_t;

static const vlc_sd_field_desc_t fields[] = {
    {
        .key = "uri",
        .label = N_("URL"),
        .type = SD_FIELD_TEXT,
        .required = true,
    },
};

/*****************************************************************************
 * Entry list
 *****************************************************************************/

static bool AddEntry( services_discovery_t *p_sd, entry_t *entry )
{
    services_discovery_sys_t *p_sys = p_sd->p_sys;

    if( entry->uri == NULL || entry->uri[0] == '\0' )
        return false;

    entry->item = input_item_NewDirectory( entry->uri, entry->uri, ITEM_NET );
    if( entry->item == NULL )
        return false;

    entry->id = p_sys->next_id;
    if( !vlc_vector_push( &p_sys->entries, *entry ) )
    {
        input_item_Release( entry->item );
        entry->item = NULL;
        return false;
    }

    p_sys->next_id++;
    services_discovery_AddItem( p_sd, entry->item );
    return true;
}

static bool RemoveEntry( services_discovery_t *p_sd, int id )
{
    services_discovery_sys_t *p_sys = p_sd->p_sys;

    for( size_t i = 0; i < p_sys->entries.size; i++ )
    {
        if( p_sys->entries.data[i].id != id )
            continue;

        entry_t entry = p_sys->entries.data[i];
        vlc_vector_remove( &p_sys->entries, i );

        services_discovery_RemoveItem( p_sd, entry.item );
        input_item_Release( entry.item );
        free( entry.uri );
        return true;
    }
    return false;
}

static void SaveConfig( services_discovery_t *p_sd )
{
    services_discovery_sys_t *p_sys = p_sd->p_sys;
    struct vlc_memstream ms;

    if( vlc_memstream_open( &ms ) != 0 )
        return;

    for( size_t i = 0; i < p_sys->entries.size; i++ )
    {
        if( i != 0 )
            vlc_memstream_putc( &ms, '|' );
        vlc_memstream_puts( &ms, p_sys->entries.data[i].uri );
    }

    if( vlc_memstream_close( &ms ) != 0 )
        return;

    config_PutPsz( CONFIG_KEY, ms.ptr );
    free( ms.ptr );
}

static int LoadConfig( services_discovery_t *p_sd )
{
    char *urls = var_InheritString( p_sd, CONFIG_KEY );
    if( urls == NULL )
        return VLC_SUCCESS; /* no configuration yet: not an error */

    int ret = VLC_SUCCESS;
    char *url = urls;

    while( url != NULL )
    {
        char *sep = strchr( url, '|' );

        if( sep != NULL )
        {
            *sep = '\0';
            sep++;
        }

        if( *url != '\0' )
        {
            entry_t entry = { .uri = strdup( url ) };

            if( !AddEntry( p_sd, &entry ) )
            {
                free( entry.uri );
                ret = VLC_ENOMEM;
                break;
            }
        }

        url = sep;
    }

    free( urls );
    return ret;
}

/*****************************************************************************
 * Control
 *****************************************************************************/

static const char *FindField( const vlc_sd_input_t *input, const char *key )
{
    for( size_t i = 0; i < input->field_count; i++ )
        if( strcmp( input->fields[i].key, key ) == 0 )
            return input->fields[i].value;
    return NULL;
}

/* Build the entry list handed to the configuration UI. */
static vlc_sd_input_t *ListEntries( services_discovery_t *p_sd, size_t *count )
{
    services_discovery_sys_t *p_sys = p_sd->p_sys;
    size_t i = 0;

    *count = 0;
    if( p_sys->entries.size == 0 )
        return NULL;

    vlc_sd_input_t *p_inputs = calloc( p_sys->entries.size, sizeof( *p_inputs ) );
    if( p_inputs == NULL )
        return NULL;

    for( i = 0; i < p_sys->entries.size; i++ )
    {
        vlc_sd_field_t *p_fields = calloc( 1, sizeof( *p_fields ) );
        if( p_fields == NULL )
            goto error;

        p_inputs[i].id = p_sys->entries.data[i].id;
        p_inputs[i].fields = p_fields;

        char *key = strdup( "uri" );
        char *value = strdup( p_sys->entries.data[i].uri );
        if( key == NULL || value == NULL )
        {
            free( key );
            free( value );
            goto error;
        }

        p_fields[0].key = key;
        p_fields[0].type = SD_FIELD_TEXT;
        p_fields[0].value = value;
        p_inputs[i].field_count = 1;
    }

    *count = i;
    return p_inputs;
error:
    vlc_sd_input_ListFree( p_inputs, i + 1 );
    return NULL;
}

static const vlc_sd_field_desc_t *ConfigDescribe( services_discovery_t *p_sd,
                                              size_t *count )
{
    VLC_UNUSED( p_sd );
    *count = ARRAY_SIZE( fields );
    return fields;
}

static vlc_sd_input_t *ConfigList( services_discovery_t *p_sd, size_t *count )
{
    services_discovery_sys_t *p_sys = p_sd->p_sys;

    vlc_mutex_lock( &p_sys->lock );
    vlc_sd_input_t *p_inputs = ListEntries( p_sd, count );
    vlc_mutex_unlock( &p_sys->lock );
    return p_inputs;
}

static int ConfigAdd( services_discovery_t *p_sd, const vlc_sd_input_t *input )
{
    services_discovery_sys_t *p_sys = p_sd->p_sys;

    const char *uri = FindField( input, "uri" );
    if( uri == NULL || uri[0] == '\0' )
        return VLC_EGENERIC;

    entry_t entry = { .uri = strdup( uri ) };

    vlc_mutex_lock( &p_sys->lock );
    int ret = VLC_EGENERIC;
    if( AddEntry( p_sd, &entry ) )
    {
        SaveConfig( p_sd );
        ret = entry.id;
    }
    else
        free( entry.uri );
    vlc_mutex_unlock( &p_sys->lock );
    return ret;
}

static int ConfigRemove( services_discovery_t *p_sd, int id )
{
    services_discovery_sys_t *p_sys = p_sd->p_sys;

    vlc_mutex_lock( &p_sys->lock );
    if( RemoveEntry( p_sd, id ) )
        SaveConfig( p_sd );
    vlc_mutex_unlock( &p_sys->lock );
    return VLC_SUCCESS;
}

static const struct vlc_sd_config_operations config_ops = {
    .describe = ConfigDescribe,
    .list     = ConfigList,
    .add      = ConfigAdd,
    .remove   = ConfigRemove,
};

/*****************************************************************************
 * Open: initialize and create stuff
 *****************************************************************************/
static int Open( vlc_object_t *p_this )
{
    if( strcmp( vlc_object_typename(vlc_object_parent(p_this)), "media-source-provider" ) )
        return VLC_EGENERIC; /* FIXME: support LibVLC SD too! */

    services_discovery_t *p_sd = ( services_discovery_t* )p_this;
    services_discovery_sys_t *p_sys = malloc( sizeof( *p_sys ) );
    if( p_sys == NULL )
        return VLC_ENOMEM;

    vlc_vector_init( &p_sys->entries );
    p_sys->next_id = 1;
    vlc_mutex_init( &p_sys->lock );

    p_sd->p_sys = p_sys;
    p_sd->config_ops = &config_ops;
    p_sd->description = _("Podcasts");

    if( LoadConfig( p_sd ) != VLC_SUCCESS )
    {
        Close( p_this );
        return VLC_ENOMEM;
    }

    return VLC_SUCCESS;
}

/*****************************************************************************
 * Close:
 *****************************************************************************/
static void Close( vlc_object_t *p_this )
{
    services_discovery_t *p_sd = ( services_discovery_t* )p_this;
    services_discovery_sys_t *p_sys = p_sd->p_sys;

    entry_t *entry;
    vlc_vector_foreach_ref( entry, &p_sys->entries )
    {
        input_item_Release( entry->item );
        free( entry->uri );
    }
    vlc_vector_destroy( &p_sys->entries );
    free( p_sys );
}
