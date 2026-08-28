/*****************************************************************************
 * exporter.c: Reading dmabuf descriptors from pictures
 *****************************************************************************
 * Copyright (C) 2026 Alexandre Janniaux <ajanni@videolabs.io>
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
# include <config.h>
#endif

#include <vlc_common.h>
#include <vlc_modules.h>
#include <vlc_objects.h>

#include "vlc_dmabuf.h"

static int OpenExporter(void *func, bool forced, va_list ap)
{
    vlc_dmabuf_exporter_open open = func;
    struct vlc_dmabuf_exporter *exporter =
        va_arg(ap, struct vlc_dmabuf_exporter *);

    (void) forced;
    return open(exporter);
}

struct vlc_dmabuf_exporter *
vlc_dmabuf_exporter_New(vlc_object_t *parent, vlc_video_context *vctx,
                        const video_format_t *fmt)
{
    if (vctx == NULL)
        return NULL;

    struct vlc_dmabuf_exporter *exporter =
        vlc_object_create(parent, sizeof (*exporter));
    if (unlikely(exporter == NULL))
        return NULL;

    exporter->vctx = vlc_video_context_Hold(vctx);
    exporter->sys = NULL;
    exporter->ops = NULL;
    exporter->format = (struct vlc_dmabuf_format) { 0 };

    if (unlikely(video_format_Copy(&exporter->fmt, fmt) != VLC_SUCCESS))
        goto error1;

    module_t *module = vlc_module_load(vlc_object_logger(exporter),
                                       "dmabuf exporter", NULL,
                                       false, OpenExporter, exporter);
    if (module == NULL)
        goto error2;

    return exporter;

error2:
    video_format_Clean(&exporter->fmt);

error1:
    vlc_video_context_Release(exporter->vctx);
    vlc_object_delete(exporter);
    return NULL;
}

void vlc_dmabuf_exporter_Delete(struct vlc_dmabuf_exporter *exporter)
{
    if (exporter->ops->close != NULL)
        exporter->ops->close(exporter);

    video_format_Clean(&exporter->fmt);
    vlc_video_context_Release(exporter->vctx);
    vlc_object_delete(exporter);
}
