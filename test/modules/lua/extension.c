/*****************************************************************************
 * extension.c: test for the lua extension module
 *****************************************************************************
 * Copyright (C) 2023 Videolabs
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

/* Define a builtin module for mocked parts */
#define MODULE_NAME test_lua_extension
#undef VLC_DYNAMIC_PLUGIN

#include "../../libvlc/test.h"

#include <vlc/vlc.h>

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_modules.h>
#include <vlc_interface.h>
#include <vlc_playlist.h>
#include <vlc_player.h>
#include <vlc_extensions.h>
#include <vlc_input_item.h>

#include <limits.h>

#include "../lib/libvlc_internal.h"

const char vlc_module_name[] = MODULE_STRING;

static int exitcode = 0;


static int OnLuaEventTriggered(vlc_object_t *obj, const char *name,
        vlc_value_t oldv, vlc_value_t newv, void *opaque)
{
    (void)obj; (void)name; (void)oldv; (void)newv;
    vlc_sem_t *sem = opaque;
    vlc_sem_post(sem);
    return VLC_SUCCESS;
}

typedef struct {
    vlc_sem_t sem;
    const char* var_name;
    void (*exec)(vlc_player_t*,extensions_manager_t*);
} test_cmd_t;

static void test_activate(vlc_player_t*player,extensions_manager_t* mgr){
    extension_Activate(mgr, mgr->extensions.p_elems[0]);
}

static void test_deactivate(vlc_player_t*player, extensions_manager_t* mgr){
    extension_Deactivate(mgr, mgr->extensions.p_elems[0]);
}

static void test_input_changed(vlc_player_t*player, extensions_manager_t* mgr){
    vlc_player_Lock(player);
    input_item_t *item = input_item_New(
            "mock://length=100000000000000000", // TODO: make it infinite
            "lua_test_sample");
    vlc_player_SetCurrentMedia(player, item);
    vlc_player_Start(player);
    vlc_player_Unlock(player);
    input_item_Release(item);
}

static void test_playing_changed(vlc_player_t*player, extensions_manager_t* mgr){
    int state = 1;
    extension_PlayingChanged(mgr, mgr->extensions.p_elems[0], state);
}

static void test_meta_changed(vlc_player_t*player, extensions_manager_t* mgr){
    extension_MetaChanged(mgr, mgr->extensions.p_elems[0]);
}

static void test_trigger_menu(vlc_player_t*player, extensions_manager_t* mgr){
    extension_TriggerMenu(mgr, mgr->extensions.p_elems[0], 0);
}

static int OpenIntf(vlc_object_t *root)
{
    
    vlc_object_t *libvlc = (vlc_object_t*)vlc_object_instance(root);

    /* create tests here */
    test_cmd_t test_cmds [] = {
        {.exec=test_activate, .var_name="test-lua-activate"},
        {.exec=test_input_changed, .var_name="test-lua-input-changed"},
        {.exec=test_playing_changed, .var_name="test-lua-playing-changed" },
        {.exec=test_meta_changed, .var_name="test-lua-meta-changed" },
        {.exec=test_trigger_menu, .var_name="test-lua-trigger-menu"},
        {.exec=test_deactivate, .var_name="test-lua-deactivate" },
    };


    intf_thread_t *intf = (intf_thread_t*)root;
    extensions_manager_t *mgr =
        vlc_object_create(root, sizeof *mgr);
    assert(mgr);

    setenv("VLC_USERDATA_PATH", TOP_SRCDIR "/test/modules/", 1);

    vlc_playlist_t *playlist = vlc_intf_GetMainPlaylist(intf);
    vlc_player_t *player = vlc_playlist_GetPlayer(playlist);
    mgr->player = player;
    mgr->p_module = module_need(mgr, "extension", "lua", true);

    if (mgr->p_module == NULL)
    {
        exitcode = 77;
        goto end;
    }

    vlc_sem_t sem_close;
    vlc_sem_init(&sem_close,0);
    var_Create(libvlc, "test-lua-close", VLC_VAR_STRING | VLC_VAR_ISCOMMAND);
    var_AddCallback(libvlc, "test-lua-close", OnLuaEventTriggered,&sem_close);

    for (size_t i = 0; i < ARRAY_SIZE(test_cmds); i++){
        vlc_sem_init(&test_cmds[i].sem,0);  
        var_Create(libvlc, test_cmds[i].var_name, VLC_VAR_STRING | VLC_VAR_ISCOMMAND);
        var_AddCallback(libvlc, test_cmds[i].var_name, OnLuaEventTriggered,&test_cmds[i].sem);
    }

    /* Check that the extension from the test is correctly probed. */
    assert(mgr->extensions.i_size == 1);

    /* run tests, -1 is temporarily added to not include deactiavte until better testing abstractions been made */
    for (size_t i = 0; i < ARRAY_SIZE(test_cmds)-1; i++){
        test_cmds[i].exec(player, mgr);
        vlc_sem_wait(&test_cmds[i].sem);
    }

    // create dialog
    extension_dialog_t *p_dlg = calloc( 1, sizeof( extension_dialog_t ) );
    if( !p_dlg ){
        exitcode = 77;
        goto end;
    }
    p_dlg->p_object = (vlc_object_t*)mgr;
    p_dlg->psz_title = strdup( "test dialog" );
    p_dlg->b_kill = false;
    p_dlg->p_sys = mgr->extensions.p_elems[0];

    /*
     adds widget to dialog
     NOTE: too much of a hassle to register callback 
     so warning will trigger from undefined callback */
    ARRAY_INIT( p_dlg->widgets );
    extension_widget_t *p_widget = calloc( 1, sizeof( extension_widget_t ) );
    p_widget->type = EXTENSION_WIDGET_CHECK_BOX;
    p_widget->psz_text = strdup( "test widget" );
    p_widget->p_dialog = p_dlg;

    ARRAY_APPEND( p_dlg->widgets, p_widget );

    extension_WidgetClicked(p_dlg, p_widget);
    extension_DialogClosed(p_dlg);
    vlc_sem_wait(&sem_close);


    //clean up 
    ARRAY_REMOVE(p_dlg->widgets, 0);
    free(p_widget->psz_text);
    free(p_widget);
    free( p_dlg->psz_title );
    free( p_dlg );

    // !temporary, deactivates extension
    test_cmds[ARRAY_SIZE(test_cmds)-1].exec(player, mgr);
    vlc_sem_wait(&test_cmds[ARRAY_SIZE(test_cmds)-1].sem);

    var_DelCallback(libvlc, "test-lua-close", OnLuaEventTriggered,&sem_close);

    for (size_t i = 0; i < ARRAY_SIZE(test_cmds); i++){
        var_DelCallback(libvlc, test_cmds[i].var_name, OnLuaEventTriggered,&test_cmds[i].sem);
    }

    module_unneed(mgr, mgr->p_module);
end:
    vlc_object_delete(mgr);
    return VLC_SUCCESS;
}

/** Inject the mocked modules as a static plugin: **/
vlc_module_begin()
    set_callback(OpenIntf)
    set_capability("interface", 0)
vlc_module_end()

VLC_EXPORT const vlc_plugin_cb vlc_static_modules[] = {
    VLC_SYMBOL(vlc_entry),
    NULL
};


int main(void)
{
    test_init();

    const char * const args[] = {
        "-vvv", "--vout=dummy", "--aout=dummy", "--text-renderer=dummy",
        "--no-auto-preparse",
    };

    libvlc_instance_t *vlc = libvlc_new(ARRAY_SIZE(args), args);

    libvlc_InternalAddIntf(vlc->p_libvlc_int, MODULE_STRING);
    libvlc_InternalPlay(vlc->p_libvlc_int);

    libvlc_release(vlc);
    return 0;
}
