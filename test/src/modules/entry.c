/*****************************************************************************
 * entry.c: test for plugin entrypoint
 *****************************************************************************
 * Copyright (C) 2024 Videolabs
 *
 * Authors: Alexandre Janniaux <ajanni@videolabs.io>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 2.1 of the License, or
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
# include <config.h>
#endif

#include <vlc_common.h>
#include <vlc_plugin.h>

#include <modules/modules.h>

#include <assert.h>
#include <string.h>

const char vlc_module_name[] = __FILE__;
int vlc_entry__core(vlc_set_cb vlc_set, void *opaque);
int vlc_entry__core(vlc_set_cb vlc_set, void *opaque)
{
    (void)vlc_set; (void)opaque;
    return VLC_SUCCESS;
}

#undef MODULE_NAME
#undef MODULE_STRING
#define MODULE_NAME second_module
#define MODULE_STRING "second_module"
VLC_DECL_MODULE_ENTRY(second_module);
vlc_module_begin()
vlc_module_end()

#undef MODULE_NAME
#undef MODULE_STRING
#define MODULE_NAME test
#define MODULE_STRING "test"
VLC_DECL_MODULE_ENTRY(test);
vlc_module_begin()
    vlc_entry__second_module(vlc_set, opaque);
vlc_module_end()

void test_module_scope_successful(void)
{
    vlc_plugin_t *plugin;

    plugin = vlc_plugin_describe(vlc_entry__second_module);
    assert(plugin != NULL);
    assert(plugin->module != NULL);
    assert(!strcmp(plugin->module->psz_longname, "second_module"));
    vlc_plugin_destroy(plugin);

    plugin = vlc_plugin_describe(vlc_entry__test);
    assert(plugin != NULL);
    assert(plugin->module != NULL);
    assert(!strcmp(plugin->module->psz_longname, "test"));
    assert(plugin->module->next!= NULL);
    assert(!strcmp(plugin->module->next->psz_longname, "second_module"));
    vlc_plugin_destroy(plugin);
}

int main(int argc, char **argv)
{
    test_module_scope_successful();
}
