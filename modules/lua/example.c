#include <vlc_common.h>
#include <vlc_interface.h>
#include <vlc_extensions.h>
#include <vlc_threads.h>
#include <vlc_atomic.h>
#include <vlc_arrays.h>
#include <vlc_plugin.h>

struct extension_state {
    extension_t manifest;
    bool enabled;
};

struct module_state {
    vlc_mutex_t lock;
    vlc_atomic_rc_t rc;
    bool initialized;
    struct extension_state extensions[2];
};
static struct module_state state;

static void EnableExtension(struct extension_state *ext)
{
    vlc_mutex_assert(&state.lock);
    fprintf(stderr, "ENABLING EXTENSION %s\n", ext->manifest.psz_name);
    ext->enabled = true;
}

static void DisableExtension(struct extension_state *ext)
{
    vlc_mutex_assert(&state.lock);
    fprintf(stderr, "DISABLING EXTENSION %s\n", ext->manifest.psz_name);
    ext->enabled = false;
}

static int ExtensionControl(
    extensions_manager_t *p_mgr,
    int query,
    extension_t *ext,
    va_list args)
{
    switch (query) {
        case EXTENSION_ACTIVATE: {
            vlc_mutex_lock(&state.lock);
            struct extension_state *extension = ext->p_sys;
            EnableExtension(extension);
            vlc_mutex_unlock(&state.lock);
            break;
        }
        case EXTENSION_DEACTIVATE: {
            vlc_mutex_lock(&state.lock);
            struct extension_state *extension = ext->p_sys;
            DisableExtension(extension);
            vlc_mutex_unlock(&state.lock);
            break;
        }
        case EXTENSION_IS_ACTIVATED: {
            bool *enabled = va_arg(args, bool*);
            vlc_mutex_lock(&state.lock);
            struct extension_state *extension = ext->p_sys;
            *enabled = extension->enabled;
            vlc_mutex_unlock(&state.lock);
            break;
        }
    }
    return VLC_SUCCESS;
}

static void StateInit(void)
{
    state.extensions[0].enabled = false;
    state.extensions[0].manifest.psz_name = strdup("Example extension 1");
    state.extensions[0].manifest.psz_title = strdup("Example extension 1");
    state.extensions[0].manifest.p_sys = &state.extensions[0];

    state.extensions[1].enabled = false;
    state.extensions[1].manifest.psz_name = strdup("Example extension 2");
    state.extensions[1].manifest.psz_title = strdup("Example extension 2");
    state.extensions[1].manifest.p_sys = &state.extensions[1];

    state.initialized = true;
}


static void CloseExtension(vlc_object_t *obj)
{
    extensions_manager_t *mgr = (extensions_manager_t*)obj;
    if (vlc_atomic_rc_dec(&state.rc))
    {
        // Disable extensions and release state/free memory
    }
}

static int OpenExtension(vlc_object_t *obj)
{
    extensions_manager_t *mgr = (extensions_manager_t*)obj;
    vlc_mutex_lock(&state.lock);
    if (!state.initialized)
        StateInit();
    else
        vlc_atomic_rc_inc(&state.rc);

    ARRAY_APPEND(mgr->extensions, &state.extensions[0].manifest);
    ARRAY_APPEND(mgr->extensions, &state.extensions[1].manifest);

    vlc_mutex_unlock(&state.lock);
    mgr->pf_control = ExtensionControl;
    return VLC_SUCCESS;
}

static int AutorunStart(intf_thread_t *intf)
{
    vlc_mutex_lock(&state.lock);
    if (!state.initialized)
        StateInit();
    else
        vlc_atomic_rc_inc(&state.rc);

    // Enable an extension for the example
    EnableExtension(&state.extensions[1]);
    vlc_mutex_unlock(&state.lock);
    return VLC_SUCCESS;
}

static void AutorunStop(intf_thread_t *intf)
{
    if (vlc_atomic_rc_dec(&state.rc))
    {
        // Disable extensions and release state/free memory
    }
}

vlc_module_begin ()
    set_shortname("Example Extension")
    set_description("Example Extension")
    add_shortcut("exampleextension")
    set_capability("extension", 10000000)
    set_callbacks(OpenExtension, CloseExtension)

    add_submodule()
    set_capability("interface", 0)
    add_shortcut("exampleextension")
    set_callbacks(AutorunStart, AutorunStop)
vlc_module_end()