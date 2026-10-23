/*****************************************************************************
 * vlc_dmabuf.h: dmabuf picture interop for modules
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

#ifndef VLC_DMABUF_H
#define VLC_DMABUF_H 1

#include <stdint.h>

#ifndef HAVE_LIBDRM
# include <drm/drm_fourcc.h>
#else
# include <drm_fourcc.h>
#endif

#include <vlc_common.h>
#include <vlc_picture.h>

/**
 * \defgroup dmabuf Linux dmabuf pictures
 *
 * A picture backed by a resource that can be translated into a linux
 * DRM dmabuf file descriptor. This allows mapping this picture to
 * different consumers that are compatible with the linux Direct
 * Rendering Manager infrastructure and thus provide zero-copy across
 * components and even applications.
 *
 * The producer for the frame and the consumer that will read it are
 * different modules that won't know each other, so we transfer a
 * ::vlc_dmabuf_frame between them, obtained from a picture through a
 * ::vlc_dmabuf_exporter object.
 *
 *     Producer  - - - - - - - - ->  Consumer
 *        \                            /\
 *         \_____ dmabuf exporter _____/
 *
 * Note that there is no specific "VLC_VIDEO_CONTEXT_DMABUF" video
 * context type corresponding to the dmabuf frames, and all have the
 * capacity to have their own ::vlc_dmabuf_exporter. That's the point
 * of the `dmabuf exporter` capability.
 *
 * Any kind of video context type that could provide this can implement
 * the capability in the matching modules, so that intermediate and
 * final consumers can still use the more complete video context type
 * for filtering, properties, etc.
 *
 * The dmabuf descriptors belong to the consumer, which must close them
 * themselves. However, their content is only valid for as long as the
 * consumer holds the picture they came from and the exporter is alive.
 *
 * @{
 *
 * \file
 * dmabuf picture interop for modules
 */

#define VLC_DMABUF_MAX_PLANES 4

/**
 * Reference to one plane in a dmabuf frame.
 *
 * Each plane has its own file descriptor that must be released.
 */
struct vlc_dmabuf_plane
{
    int fd;             /**< dmabuf descriptor, owned by the consumer */
    uint32_t offset;    /**< position of the plane within the descriptor */
    uint32_t lines;     /**< Number of lines, including margins */
    uint32_t pitch;     /**< Number of bytes in a line, including margins */
};

/**
 * dmabuf properties to interpret the descriptor from a consumer.
 * 
 * This is mainly used _after_ the exporter to proceed with the import
 * in a consuming API.
 */
struct vlc_dmabuf_frame
{
    /** DRM pixel format of the frame, see also vlc_dmabuf_format. */
    uint32_t drm_fourcc;

    /** DRM format modifier describing the tiling or compression, or
     * DRM_FORMAT_MOD_LINEAR when there is none. */
    uint64_t modifier;

    unsigned plane_count;
    struct vlc_dmabuf_plane planes[VLC_DMABUF_MAX_PLANES];
};

/**
 * Description of a vlc_dmabuf_frame plane
 */
struct vlc_dmabuf_plane_format
{
    /** DRM pixel format of the plane itself. */
    uint32_t drm_fourcc;

    /** Size of the plane as a fraction of the frame. */
    vlc_rational_t width, height;
};

/**
 * Description of the dmabuf picture with its planes.
 *
 * This provides the binding between a picture that can be mapped as a
 * dmabuf descriptor and how it should be interpreted by the dmabuf
 * consumers.
 */
struct vlc_dmabuf_format
{
    /** DRM pixel format of the whole frame. */
    uint32_t drm_fourcc;

    /** The matching VLC fourcc, so as to define how to interpret the
     * layout information. */
    vlc_fourcc_t chroma;

    unsigned plane_count;
    struct vlc_dmabuf_plane_format planes[VLC_DMABUF_MAX_PLANES];
};

struct vlc_dmabuf_exporter;

struct vlc_dmabuf_exporter_operations
{
    /**
     * Describe one picture of the video context as dmabuf descriptors.
     *
     * A producer that has only implicit synchronization may wait here
     * for the frame to be ready, so this might be blocking.
     *
     * \param exporter the exporter object owning the operations
     * \param pic picture matching the video context, from which the
     *        dmabuf exporter module will export the descriptors
     * \param frame filled in on success, the descriptors in it are
     *        owned by the consumer which must close them and the
     *        content stays valid for as long as the exporter and
     *        \p pic are both held
     * \return VLC_SUCCESS, or an error if this picture cannot be
     *         shared, in which case there is nothing to close.
     *
     * \note Currently, a change in video context should mean a change
     * in the dmabuf exporter since we cannot guarantee proper frame
     * lifecycle otherwise.
     */
    int (*export_frame)(struct vlc_dmabuf_exporter *exporter,
                        picture_t *pic,
                        struct vlc_dmabuf_frame *frame);

    /**
     * Release the exporter, optional
     *
     * \param exporter the exporter object owning the operations
     */
    void (*close)(struct vlc_dmabuf_exporter *exporter);
};

struct vlc_dmabuf_exporter
{
    struct vlc_object_t obj;

    /* Set when opening the dmabuf exporter implementation to provide
     * the context from which to describe the frames. */
    vlc_video_context *vctx;

    video_format_t fmt;

    /* Output dmabuf format for the exporter */
    struct vlc_dmabuf_format format;

    void *sys;
    const struct vlc_dmabuf_exporter_operations *ops;
};

typedef int (*vlc_dmabuf_exporter_open)(struct vlc_dmabuf_exporter *);

#define set_callback_dmabuf_exporter(activate)      \
    {                                               \
        vlc_dmabuf_exporter_open open__ = activate; \
        (void) open__;                              \
        set_callback(activate)                      \
    }

/**
 * Look for an exporter that can process the given video context into
 * dmabuf descriptors.
 *
 * \param parent object to attach the exporter to
 * \param vctx video context of the pictures to export, held until the
 *        exporter is deleted.
 * \param fmt format of the pictures to export, copied into the exporter
 *
 * \return an exporter, or NULL if the pictures from \p vctx cannot be
 *         translated into dmabuf descriptors.
 */
struct vlc_dmabuf_exporter *
vlc_dmabuf_exporter_New(vlc_object_t *parent, vlc_video_context *vctx,
                        const video_format_t *fmt);

void vlc_dmabuf_exporter_Delete(struct vlc_dmabuf_exporter *);

/** \copydoc vlc_dmabuf_exporter_operations::export_frame */
static inline int
vlc_dmabuf_exporter_Export(struct vlc_dmabuf_exporter *exporter,
                           picture_t *pic, struct vlc_dmabuf_frame *frame)
{
    return exporter->ops->export_frame(exporter, pic, frame);
}

/** @} */

#endif /* VLC_DMABUF_H */
