/*****************************************************************************
 * Copyright (C) 2026 VLC authors and VideoLAN
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * ( at your option ) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#include "windoweffects_module.hpp"

#include <QGuiApplication>
#include <QRegion>
#include <QWindow>
#include <QHash>
#include <QCache>

#if QT_VERSION < QT_VERSION_CHECK(6, 9, 0)
#ifndef QT_GUI_PRIVATE
#error "If Qt version is lower than 6.9.0, private gui headers are required."
#endif
#include <QtGui/qpa/qplatformnativeinterface.h>
#include <QtGui/qpa/qplatformwindow.h>
#endif

#include <vlc_plugin.h>

#include <wayland-client.h>

#include "ext-background-effect-v1.h"

// #define REGIONCACHE_DEBUG

struct Region
{
    wl_region *const region;

public:
    explicit Region(wl_region* region)
        : region(region)
    {
        assert(region);
    }

    ~Region()
    {
        wl_region_destroy(region);
    }
};

struct ExtBackgroundEffect
{
    ext_background_effect_manager_v1* extBackgroundEffectManager = nullptr;

    // Note that we use one manager to manage background effect for multiple surfaces:
    QHash<const QWindow*, QPair<wl_surface*, ext_background_effect_surface_v1*>> surfaceHash;

    // We currently do not signal the change for this:
    std::optional<uint32_t> extBackgroundEffectCapabilities;

    wl_compositor* compositor = nullptr;
    wl_display* display = nullptr;
    wl_event_queue* queue = nullptr;
    wl_registry* registry = nullptr;
    uint32_t registryId = 0;

    bool initialized = false;

    // If we assume size of `QRegion` to be (4 * sizeof(int)), this would take
    // (4 * sizeof(int) * 128) bytes, which is 2 KB if sizeof(int) is 4 bytes.
    // Note that the size can theoretically be infinite, because region can
    // consist of multiple rectangles (which can be the case with rounded
    // rectangle window with CSD), but we prevent using unlimited memory by
    // using the rectangle count as cost during insertion.
    QCache<QRegion, Region> regionCache {128};

#ifdef REGIONCACHE_DEBUG
    size_t cacheHit = 0;
    size_t cacheMiss = 0;
    size_t cacheTotal = 0;
#endif

    // Empty region is special and is always cached:
    std::optional<wl_region*> emptyRegion;
};

// Qt lacks `qHash()` for `QRegion`:
inline size_t qHash(const QRegion &key, size_t seed)
{
    const auto rects = key.rects();
    return qHashRangeCommutative(rects.cbegin(), rects.cend(), seed);
}

static bool SupportsPartialArea(WindowEffectsModule&, WindowEffectsModule::Effect effect)
{
    Q_UNUSED(effect);
    return true;
}

static bool BlurBehindRequiresOpacityLayer(WindowEffectsModule&)
{
    static const auto ret = []() {
        return qEnvironmentVariable("XDG_SESSION_DESKTOP") == QLatin1String("GNOME");
    }();

    return ret;
}

static void ext_background_effect_manager_capabilities_cb(void *data,
                                                          struct ext_background_effect_manager_v1 *,
                                                          uint32_t flags)
{
    const auto obj = static_cast<WindowEffectsModule*>(data);
    assert(obj);
    const auto sys = static_cast<ExtBackgroundEffect*>(obj->p_sys);
    assert(sys);

    sys->extBackgroundEffectCapabilities = flags;
}

static const struct ext_background_effect_manager_v1_listener ext_background_effect_manager_cbs = {
    .capabilities = ext_background_effect_manager_capabilities_cb,
};

static void registry_global_cb(void* data, struct wl_registry* registry,
                               uint32_t id, const char* iface, uint32_t version)
{
    const auto obj = static_cast<WindowEffectsModule*>(data);
    assert(obj);
    const auto sys = static_cast<ExtBackgroundEffect*>(obj->p_sys);
    assert(sys);
    assert(sys->queue);

    if (!strcmp(iface, "ext_background_effect_manager_v1"))
    {
        sys->extBackgroundEffectManager = static_cast<ext_background_effect_manager_v1*>(wl_registry_bind(registry,
                                                                                                          id,
                                                                                                          &ext_background_effect_manager_v1_interface,
                                                                                                          std::min(version, static_cast<uint32_t>(ext_background_effect_manager_v1_interface.version))));

        if (sys->extBackgroundEffectManager)
        {
            sys->registryId = id;
            ext_background_effect_manager_v1_add_listener(sys->extBackgroundEffectManager, &ext_background_effect_manager_cbs, data);
        }
    }
}

static void Deinit(WindowEffectsModule& obj);

static void registry_global_remove_cb(void* data, struct wl_registry*, uint32_t id)
{
    const auto obj = static_cast<WindowEffectsModule*>(data);
    assert(obj);
    const auto sys = static_cast<ExtBackgroundEffect*>(obj->p_sys);
    assert(sys);

    if (id && sys->registryId == id)
        Deinit(*obj);
}

static const struct wl_registry_listener registry_cbs = {
    registry_global_cb,
    registry_global_remove_cb,
};

static bool IsEffectAvailable(WindowEffectsModule& obj, const QWindow*, WindowEffectsModule::Effect effect)
{
    const auto sys = static_cast<ExtBackgroundEffect*>(obj.p_sys);
    assert(sys);

    assert(sys->initialized);
    assert(sys->extBackgroundEffectCapabilities);

    switch (effect)
    {
    case WindowEffectsModule::BlurBehind:
        return (*sys->extBackgroundEffectCapabilities & EXT_BACKGROUND_EFFECT_MANAGER_V1_CAPABILITY_BLUR);
    default:
        return false;
    }
}

static bool InitializeExtBackgroundEffectSurface(WindowEffectsModule& obj, const QWindow* window)
{
    const auto sys = static_cast<ExtBackgroundEffect*>(obj.p_sys);
    assert(sys);

    assert(window);
    assert(!sys->surfaceHash.contains(window));

#if QT_VERSION < QT_VERSION_CHECK(6, 9, 0)
    QPlatformNativeInterface *nativeInterface = QGuiApplication::platformNativeInterface();
    assert(nativeInterface);
    const auto wlSurface = reinterpret_cast<wl_surface*>(nativeInterface->nativeResourceForWindow("surface", window));
#else
    const auto wlSurface = reinterpret_cast<wl_surface*>(window->winId());
#endif

    assert(sys->initialized);

    assert(sys->extBackgroundEffectManager);
    assert(wlSurface);

    const auto bgEffectSurface = ext_background_effect_manager_v1_get_background_effect(sys->extBackgroundEffectManager, wlSurface);

    if (!bgEffectSurface)
    {
        msg_Err(&obj, "ext_background_effect_manager_v1_get_background_effect() returned null pointer!");
        return false;
    }

    sys->surfaceHash[window] = qMakePair(wlSurface, bgEffectSurface);

    return true;
}

static void SetBlurBehind(WindowEffectsModule& obj, QWindow* window, bool enable, const QRegion& region)
{
    const auto sys = static_cast<ExtBackgroundEffect*>(obj.p_sys);
    assert(sys);

    if (!sys->surfaceHash.contains(window))
        InitializeExtBackgroundEffectSurface(obj, window);

    assert(sys->surfaceHash.contains(window));
    assert(sys->surfaceHash[window].first);
    assert(sys->surfaceHash[window].second);

    if (enable)
    {
        const size_t rectCount = region.rectCount();
        const auto rects = region.rects();

        if (rectCount > 0)
        {
            if (sys->regionCache.contains(region))
            {
#ifdef REGIONCACHE_DEBUG
                ++sys->cacheHit;
                qDebug() << "SetBlurBehind(): Cache hit:" << region;
#endif

                const auto wlRegion = sys->regionCache[region]->region;
                assert(wlRegion);
                ext_background_effect_surface_v1_set_blur_region(sys->surfaceHash[window].second, wlRegion);
            }
            else
            {
#ifdef REGIONCACHE_DEBUG
                ++sys->cacheMiss;
                qDebug() << "SetBlurBehind(): Cache miss:" << region;
#endif

                const auto wlRegion = new Region(wl_compositor_create_region(sys->compositor));

                for (const auto& rect : rects)
                {
                    wl_region_add(wlRegion->region, rect.x(),
                                                    rect.y(),
                                                    rect.width(),
                                                    rect.height());
                }

                // `ext_background_effect_manager_v1` protocol specifies:
                // "Setting the pending blur region has copy semantics, and the wl_region object can be destroyed immediately".
                ext_background_effect_surface_v1_set_blur_region(sys->surfaceHash[window].second, wlRegion->region);

                // `QCache` deletes the object if insertion did not succeed, we do not have leak here:
                sys->regionCache.insert(region, wlRegion, rectCount);
            }

#ifdef REGIONCACHE_DEBUG
            ++sys->cacheTotal;
            qDebug() << "SetBlurBehind(): Cache ratio (%): hit:" << (100 * sys->cacheHit / sys->cacheTotal)
                     << "miss:" << (100 * sys->cacheMiss / sys->cacheTotal);
#endif
        }
        else
        {
            // We have a simplified route for empty region:
            // Note that NULL region is not equal to empty region.
            if (!sys->emptyRegion)
            {
                sys->emptyRegion = wl_compositor_create_region(sys->compositor);

                // FIXME: This should normally not be necessary, but KWin 6.7.2 does
                //        not make the whole surface blurred with empty region:
                wl_region_add(*sys->emptyRegion, 0, 0, INT32_MAX, INT32_MAX);
            }

            assert(*sys->emptyRegion);
            ext_background_effect_surface_v1_set_blur_region(sys->surfaceHash[window].second, *sys->emptyRegion);
        }
    }
    else
    {
        ext_background_effect_surface_v1_set_blur_region(sys->surfaceHash[window].second, NULL);
    }

    wl_surface_commit(sys->surfaceHash[window].first);
}

static bool Init(WindowEffectsModule& obj)
{
    const auto sys = static_cast<ExtBackgroundEffect*>(obj.p_sys);
    assert(sys);

    assert(!sys->initialized);

    if (Q_UNLIKELY(!qApp))
    {
        msg_Err(&obj, "QGuiApplication::instance() returned null pointer!");
        return false;
    }

#if QT_CONFIG(wayland) && QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    const auto waylandApp = qApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    if (!waylandApp)
    {
        msg_Warn(&obj, "Application is not Wayland application.");
        return false;
    }
#endif

#if QT_CONFIG(wayland) && QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    sys->display = waylandApp->display();
#else
    QPlatformNativeInterface* native = QGuiApplication::platformNativeInterface();
    assert(native);
    sys->display = reinterpret_cast<wl_display*>(native->nativeResourceForIntegration("wl_display"));
#endif
    if (Q_UNLIKELY(!sys->display))
    {
        msg_Err(&obj, "QNativeInterface::QWaylandApplication::display() returned null pointer!");
        return false;
    }

#if QT_CONFIG(wayland) && QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    sys->compositor = waylandApp->compositor();
#else
    sys->compositor = reinterpret_cast<wl_compositor*>(native->nativeResourceForIntegration("compositor"));
#endif
    if (Q_UNLIKELY(!sys->compositor))
    {
        msg_Err(&obj, "QNativeInterface::QWaylandApplication::compositor() returned null pointer!");
        return false;
    }

    sys->queue = wl_display_create_queue(sys->display);
    if (Q_UNLIKELY(!sys->queue))
    {
        msg_Err(&obj, "wl_display_create_queue() returned null pointer!");
        return false;
    }

    const auto displayWrapper = wl_proxy_create_wrapper(sys->display);
    if (Q_UNLIKELY(!displayWrapper))
    {
        msg_Err(&obj, "wl_proxy_create_wrapper() returned null pointer!");
        return false;
    }

    wl_proxy_set_queue(static_cast<wl_proxy*>(displayWrapper), sys->queue);

    sys->registry = wl_display_get_registry(static_cast<wl_display*>(displayWrapper));
    if (Q_UNLIKELY(!sys->registry))
    {
        msg_Err(&obj, "wl_display_get_registry() returned null pointer!");
        return false;
    }

    wl_proxy_wrapper_destroy(displayWrapper);

    wl_registry_add_listener(sys->registry, &registry_cbs, &obj);

    // To initialize `ext_background_effect_manager_v1`:
    wl_display_roundtrip_queue(sys->display, sys->queue);
    if (!sys->extBackgroundEffectManager)
    {
        msg_Err(&obj, "Could not create ext_background_effect_manager_v1, the compositor possibly does not support the background effect protocol.");
        return false;
    }

    // To initialize capabilities:
    wl_display_roundtrip_queue(sys->display, sys->queue);
    if (!sys->extBackgroundEffectCapabilities)
    {
        msg_Err(&obj, "Could not fetch capabilities through ext_background_effect_manager_v1!");
        return false;
    }

    sys->initialized = true;

    return true;
}

static void Deinit(WindowEffectsModule& obj)
{
    const auto sys = static_cast<ExtBackgroundEffect*>(obj.p_sys);
    assert(sys);

#ifdef REGIONCACHE_DEBUG
    sys->cacheHit = 0;
    sys->cacheMiss = 0;
    sys->cacheTotal = 0;
#endif

    if (sys->display)
        wl_display_flush(sys->display);

    for (const auto& i : sys->surfaceHash)
    {
        assert(i.first);
        assert(i.second);

        ext_background_effect_surface_v1_destroy(i.second);
        wl_surface_commit(i.first);
    }

    sys->surfaceHash.clear();

    if (sys->extBackgroundEffectManager)
    {
        ext_background_effect_manager_v1_destroy(sys->extBackgroundEffectManager);
        sys->extBackgroundEffectManager = nullptr;
    }

    sys->extBackgroundEffectCapabilities.reset();

    if (sys->emptyRegion)
    {
        wl_region_destroy(*sys->emptyRegion);
        sys->emptyRegion.reset();
    }

    sys->regionCache.clear();

    if (sys->registry)
    {
        wl_registry_destroy(sys->registry);
        sys->registry = nullptr;
    }

    sys->compositor = nullptr;
    sys->display = nullptr;

    sys->initialized = false;

    if (sys->queue)
    {
        wl_event_queue_destroy(sys->queue);
        sys->queue = nullptr;
    }
}

static void Close(vlc_object_t* const p_this)
{
    const auto obj = reinterpret_cast<WindowEffectsModule*>(p_this);
    assert(obj);
    const auto sys = static_cast<ExtBackgroundEffect*>(obj->p_sys);
    assert(sys);

    Deinit(*obj);

    delete sys;
}

static int Open(vlc_object_t* const p_this) try
{
    assert(p_this);

    const auto sys = new ExtBackgroundEffect;

    const auto obj = reinterpret_cast<WindowEffectsModule*>(p_this);

    obj->setBlurBehind = SetBlurBehind;
    obj->isEffectAvailable = IsEffectAvailable;
    obj->supportsPartialArea = SupportsPartialArea;
    obj->blurBehindRequiresOpacityLayer = BlurBehindRequiresOpacityLayer;

    obj->p_sys = sys;

    if (!Init(*obj))
        goto error;

    // If none of the effects are available
    // simply fail here, so that another
    // potentially compatible module can be
    // loaded instead:
    if (!IsEffectAvailable(*obj, nullptr, WindowEffectsModule::BlurBehind))
        goto error;

    return VLC_SUCCESS;

error:
    Close(p_this);
    return VLC_EGENERIC;
}
catch (const std::bad_alloc&)
{
    return VLC_ENOMEM;
}

vlc_module_begin()
    add_shortcut("QtWaylandExtBackgroundEffect")
    set_description("Provides window effects through ext-background-effect protocol on Wayland.")
    // Has higher priority than `QtKWindowSystem`:
    set_capability("qtwindoweffects", 20)
    set_callbacks(Open, Close)
vlc_module_end()
