/*****************************************************************************
 * instance.c: libplacebo instance abstraction
 *****************************************************************************
 * Copyright (C) 2021 Niklas Haas
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
#include <vlc_configuration.h>
#include <vlc_modules.h>
#include <vlc_fs.h>

#include "instance.h"
#include "utils.h"

// 20 MB is plenty of space to cache all shaders plus several 3DLUTs, while
// still taking negligible amounts of time to parse on startup (<5ms)
#define VLC_PL_MAX_CACHE_SIZE (20 << 20)

static int vlc_placebo_start(void *func, bool forced, va_list ap)
{
    int (*activate)(vlc_placebo_t *, const vout_display_cfg_t *,
                    const struct vlc_placebo_create_params *) = func;
    vlc_placebo_t *pl = va_arg(ap, vlc_placebo_t *);
    const vout_display_cfg_t *cfg = va_arg(ap, const vout_display_cfg_t *);
    const struct vlc_placebo_create_params *params =
        va_arg(ap, const struct vlc_placebo_create_params *);

    int ret = activate(pl, cfg, params);
    /* TODO: vlc_objres_clear, which is not in the public API. */
    (void)forced;
    return ret;
}

#if PL_API_VER >= 320
static FILE *vlc_placebo_OpenCache(const char *mode)
{
    char *filename;
    char *cachedir = config_GetUserDir(VLC_CACHE_DIR);
    if (!cachedir)
        return NULL;
    int ret = asprintf(&filename, "%s" DIR_SEP "libplacebo", cachedir);
    free(cachedir);
    if (ret < 0)
        return NULL;
    FILE *file = vlc_fopen(filename, mode);
    free(filename);
    return file;
}
#endif

/**
 * Allocates an unconnected libplacebo context.
 *
 * @param cfg vout display cfg to use as the swapchain source
 * @param name module name for libplacebo GPU provider (or NULL for auto)
 * @return a new context, or NULL on failure
 */
vlc_placebo_t *vlc_placebo_New(const vout_display_cfg_t *cfg, const char *name)
{
    vlc_object_t *parent = VLC_OBJECT(cfg->window);
    vlc_placebo_t *pl = vlc_object_create(parent, sizeof (*pl));
    if (unlikely(pl == NULL))
        return NULL;

    pl->sys = NULL;
    pl->ops = NULL;
    pl->cfg = cfg;
    pl->name = NULL;

    if (name != NULL) {
        pl->name = strdup(name);
        if (unlikely(pl->name == NULL))
            goto delete_pl;
    }

    pl->log = vlc_placebo_CreateLog(VLC_OBJECT(pl));
    if (pl->log == NULL)
        goto delete_pl;

    return pl;

delete_pl:
    free(pl->name);
    vlc_object_delete(pl);
    return NULL;
}

/**
 * Loads a "libplacebo gpu" backend and creates its instance, device and swapchain
 */
int vlc_placebo_Connect(vlc_placebo_t *pl,
                        const struct vlc_placebo_create_params *params)
{
    module_t *module = vlc_module_load(vlc_object_logger(VLC_OBJECT(pl)),
                                       "libplacebo gpu", pl->name, false,
                                       vlc_placebo_start, pl, pl->cfg, params);
    if (module == NULL)
        return VLC_EGENERIC;

#if PL_API_VER >= 320
    pl->cache = pl_cache_create(pl_cache_params(
        .log = pl->log,
        .max_total_size = VLC_PL_MAX_CACHE_SIZE,
    ));
    assert(pl->gpu);
    pl_gpu_set_cache(pl->gpu, pl->cache);
    FILE *cache = vlc_placebo_OpenCache("rb");
    if (cache) {
        pl_cache_load_file(pl->cache, cache);
        fclose(cache);
    }
#endif

    return VLC_SUCCESS;
}

/**
 * Creates a libplacebo context, and swapchain, tied to a window
 */
vlc_placebo_t *vlc_placebo_Create(const vout_display_cfg_t *cfg, const char *name)
{
    vlc_placebo_t *pl = vlc_placebo_New(cfg, name);
    if (pl == NULL)
        return NULL;

    if (vlc_placebo_Connect(pl, NULL) != VLC_SUCCESS) {
        vlc_placebo_Release(pl);
        return NULL;
    }

    return pl;
}

const struct vlc_placebo_vk_handles *vlc_placebo_GetVkHandles(vlc_placebo_t *pl)
{
    if (pl->ops == NULL || pl->ops->get_vk_handles == NULL)
        return NULL;
    return pl->ops->get_vk_handles(pl);
}

void vlc_placebo_Release(vlc_placebo_t *pl)
{
    if (pl->ops)
        pl->ops->close(pl);

#if PL_API_VER >= 320
    if (pl->cache) {
        FILE *cache = vlc_placebo_OpenCache("wb");
        if (cache) {
            pl_cache_save_file(pl->cache, cache);
            fclose(cache);
        }
        pl_cache_destroy(&pl->cache);
    }
#endif

    pl_log_destroy(&pl->log);

    free(pl->name);

    vlc_object_delete(pl);
}

int vlc_placebo_MakeCurrent(vlc_placebo_t * pl)
{
    if (pl->ops->make_current)
        return pl->ops->make_current(pl);

    return VLC_SUCCESS;
}

void vlc_placebo_ReleaseCurrent(vlc_placebo_t *pl)
{
    if (pl->ops->release_current)
        pl->ops->release_current(pl);
}

void vlc_placebo_Resize(vlc_placebo_t *pl, unsigned width, unsigned height)
{
    if (pl->ops->resize)
        pl->ops->resize(pl, width, height);
}
