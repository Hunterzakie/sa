/*****************************************************************************
 * interop_dmabuf.c: OpenGL interop for pictures exported as a dmabuf
 *****************************************************************************
 * Copyright (C) 2017 VLC authors and VideoLAN
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
# include "config.h"
#endif

#include <unistd.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <vlc_common.h>
#include <vlc_plugin.h>

#include "gl_util.h"
#include "interop.h"
#include "../../hw/dmabuf/vlc_dmabuf.h"

/* From https://www.khronos.org/registry/OpenGL/extensions/OES/OES_EGL_image.txt
 * The extension is an OpenGL ES extension but can (and usually is) available on
 * OpenGL implementations. */
#ifndef GL_OES_EGL_image
#define GL_OES_EGL_image 1
typedef void *GLeglImageOES;
typedef void (*PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)(GLenum target, GLeglImageOES image);
#endif

/**
 * It maps the formats from the DRM infrastructure to the attributes 
 * expected by OpenGL to allocate textures.
 */
static const struct {
    uint32_t drm_fourcc;
    int32_t internal;
    uint32_t format, type;
} plane_tex_cfgs[] = {
    { DRM_FORMAT_R8,           GL_RED,      GL_RED,  GL_UNSIGNED_BYTE },
    { DRM_FORMAT_GR88,         GL_RG,       GL_RG,   GL_UNSIGNED_BYTE },
    { DRM_FORMAT_R16,          GL_R16,      GL_RED,  GL_UNSIGNED_SHORT },
    { DRM_FORMAT_GR1616,       GL_RG16,     GL_RG,   GL_UNSIGNED_SHORT },
    { DRM_FORMAT_ABGR16161616, GL_RGBA16,   GL_RGBA, GL_UNSIGNED_SHORT },
    { DRM_FORMAT_XYUV8888,     GL_RGBA,     GL_RGBA, GL_UNSIGNED_BYTE },
    { DRM_FORMAT_Y412,         GL_RGBA16,   GL_RGBA, GL_UNSIGNED_SHORT },
    { DRM_FORMAT_Y410,         GL_RGB10_A2, GL_RGBA,
      GL_UNSIGNED_INT_2_10_10_10_REV },
};

struct priv
{
    struct vlc_dmabuf_exporter *exporter;

    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES;

    struct
    {
        EGLDisplay display;
        EGLDisplay (*getCurrentDisplay)(void);
        const char *(*queryString)(EGLDisplay, EGLint);
        EGLImage (*createImageKHR)(EGLDisplay, EGLContext, EGLenum target, EGLClientBuffer buffer,
                const EGLint *attrib_list);
        void (*destroyImageKHR)(EGLDisplay, EGLImage image);
    } egl;

    struct
    {
        PFNGLBINDTEXTUREPROC BindTexture;
    } gl;

    struct {
        picture_t *                 pic;
        EGLImageKHR                 egl_images[VLC_DMABUF_MAX_PLANES];
    } last;
};

static EGLImageKHR
CreatePlaneImage(const struct vlc_gl_interop *interop, EGLint w, EGLint h,
                 uint32_t drm_fourcc, const struct vlc_dmabuf_plane *plane,
                 uint64_t modifier)
{
    struct priv *priv = interop->priv;
    const EGLint attribs[] = {
        EGL_WIDTH, w,
        EGL_HEIGHT, h,
        EGL_LINUX_DRM_FOURCC_EXT, (EGLint)drm_fourcc,
        EGL_DMA_BUF_PLANE0_FD_EXT, plane->fd,
        EGL_DMA_BUF_PLANE0_OFFSET_EXT, plane->offset,
        EGL_DMA_BUF_PLANE0_PITCH_EXT, plane->pitch,
        EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT, modifier & 0xffffffff,
        EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT, modifier >> 32,
        EGL_NONE
    };

    return priv->egl.createImageKHR(priv->egl.display,
            EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL, attribs);
}

static void
CloseFrame(const struct vlc_dmabuf_frame *frame)
{
    for (unsigned i = 0; i < frame->plane_count; ++i)
        close(frame->planes[i].fd);
}

static void
ReleaseLastPicture(const struct vlc_gl_interop *interop)
{
    struct priv *priv = interop->priv;

    for (unsigned i = 0; i < interop->tex_count; ++i)
        priv->egl.destroyImageKHR(priv->egl.display, priv->last.egl_images[i]);

    picture_Release(priv->last.pic);
}

static int
Update(const struct vlc_gl_interop *interop, uint32_t textures[],
                const int32_t tex_width[], const int32_t tex_height[],
                picture_t *pic, const size_t *plane_offset)
{
    (void) plane_offset;
    struct priv *priv = interop->priv;

    if (pic == priv->last.pic)
    {
        for (unsigned i = 0; i < interop->tex_count; ++i)
        {
            priv->gl.BindTexture(interop->tex_target, textures[i]);
            priv->glEGLImageTargetTexture2DOES(interop->tex_target,
                                               priv->last.egl_images[i]);
        }
        return VLC_SUCCESS;
    }

    struct vlc_dmabuf_frame frame;
    if (vlc_dmabuf_exporter_Export(priv->exporter, pic, &frame) != VLC_SUCCESS)
        return VLC_EGENERIC;

    if (frame.plane_count != interop->tex_count)
    {
        CloseFrame(&frame);
        return VLC_EGENERIC;
    }

    const struct vlc_dmabuf_format *format = &priv->exporter->format;
    EGLImageKHR egl_images[VLC_DMABUF_MAX_PLANES] = { NULL };

    for (size_t i = 0; i < frame.plane_count; ++i)
    {
        egl_images[i] = CreatePlaneImage(interop, tex_width[i], tex_height[i],
                                         format->planes[i].drm_fourcc,
                                         &frame.planes[i], frame.modifier);
        if (egl_images[i] == NULL)
            goto error;
    }

    CloseFrame(&frame);

    for (size_t i = 0; i < frame.plane_count; ++i)
    {
        priv->gl.BindTexture(interop->tex_target, textures[i]);
        priv->glEGLImageTargetTexture2DOES(interop->tex_target, egl_images[i]);
    }

    if (priv->last.pic != NULL)
        ReleaseLastPicture(interop);
    priv->last.pic = picture_Hold(pic);

    for (size_t i = 0; i < frame.plane_count; ++i)
        priv->last.egl_images[i] = egl_images[i];

    return VLC_SUCCESS;

error:
    for (unsigned i = 0; i < VLC_DMABUF_MAX_PLANES && egl_images[i] != NULL; ++i)
        priv->egl.destroyImageKHR(priv->egl.display, egl_images[i]);

    CloseFrame(&frame);
    return VLC_EGENERIC;
}

static void
Close(struct vlc_gl_interop *interop)
{
    struct priv *priv = interop->priv;

    if (priv->last.pic != NULL)
        ReleaseLastPicture(interop);

    vlc_dmabuf_exporter_Delete(priv->exporter);
    free(priv);
}

/**
 * Setup the interop texture sampling infos from the input frame format.
 */
static int
ConfigureTextures(struct vlc_gl_interop *interop,
                  const struct vlc_dmabuf_format *format)
{
    if (format->plane_count > ARRAY_SIZE(interop->texs))
        return VLC_EGENERIC;

    for (size_t i = 0; i < format->plane_count; i++)
    {
        const struct vlc_dmabuf_plane_format *plane = &format->planes[i];
        const size_t cfg = ARRAY_SIZE(plane_tex_cfgs);
        size_t j;

        for (j = 0; j < cfg; j++)
            if (plane_tex_cfgs[j].drm_fourcc == plane->drm_fourcc)
                break;

        if (j == cfg)
        {
            msg_Dbg(interop->gl, "no texture for a %4.4s plane",
                    (const char *)&plane->drm_fourcc);
            return VLC_EGENERIC;
        }

        interop->texs[i] = (struct vlc_gl_tex_cfg) {
            .w = plane->width,
            .h = plane->height,
            .internal = plane_tex_cfgs[j].internal,
            .format = plane_tex_cfgs[j].format,
            .type = plane_tex_cfgs[j].type,
        };
    }

    interop->tex_count = (unsigned)format->plane_count;
    return VLC_SUCCESS;
}

/**
 * Whether we can allocate the textures at the requested precision
 */
static bool
CanAllocateTexture(struct vlc_gl_interop *interop)
{
    for (unsigned i = 0; i < interop->tex_count; ++i)
    {
        const struct vlc_gl_tex_cfg *tex = &interop->texs[i];

        if (tex->type == GL_UNSIGNED_SHORT &&
            vlc_gl_interop_GetTexFormatSize(interop, GL_TEXTURE_2D, tex->format,
                                            tex->internal, tex->type) != 16)
            return false;
    }

    return true;
}

static int
Open(struct vlc_gl_interop *interop)
{
    if (interop->vctx == NULL)
        return VLC_EGENERIC;

    struct vlc_gl_extension_vt extension_vt;
    vlc_gl_LoadExtensionFunctions(interop->gl, &extension_vt);

    if (!vlc_gl_HasExtension(&extension_vt, "GL_OES_EGL_image"))
        return VLC_EGENERIC;

    struct priv *priv = interop->priv = calloc(1, sizeof(struct priv));
    if (unlikely(priv == NULL))
        return VLC_ENOMEM;

    priv->exporter = vlc_dmabuf_exporter_New(VLC_OBJECT(interop),
                                             interop->vctx, &interop->fmt_in);
    if (priv->exporter == NULL)
        goto error;

    if (ConfigureTextures(interop, &priv->exporter->format) != VLC_SUCCESS)
        goto error;

    if (!CanAllocateTexture(interop))
        goto error;

    priv->egl.getCurrentDisplay = vlc_gl_GetProcAddress(interop->gl, "eglGetCurrentDisplay");
    if (priv->egl.getCurrentDisplay == EGL_NO_DISPLAY)
        goto error;

    priv->egl.display = priv->egl.getCurrentDisplay();
    if (priv->egl.display == EGL_NO_DISPLAY)
        goto error;

    priv->egl.queryString = vlc_gl_GetProcAddress(interop->gl, "eglQueryString");
    if (priv->egl.queryString == NULL)
        goto error;

    /* EGL_EXT_image_dma_buf_import implies EGL_KHR_image_base */
    const char *eglexts = priv->egl.queryString(priv->egl.display, EGL_EXTENSIONS);
    if (eglexts == NULL || !vlc_gl_StrHasToken(eglexts, "EGL_EXT_image_dma_buf_import"))
        goto error;

    priv->egl.createImageKHR =
        vlc_gl_GetProcAddress(interop->gl, "eglCreateImageKHR");
    if (priv->egl.createImageKHR == NULL)
        goto error;

    priv->egl.destroyImageKHR =
        vlc_gl_GetProcAddress(interop->gl, "eglDestroyImageKHR");
    if (priv->egl.destroyImageKHR == NULL)
        goto error;

    priv->glEGLImageTargetTexture2DOES =
        vlc_gl_GetProcAddress(interop->gl, "glEGLImageTargetTexture2DOES");
    if (priv->glEGLImageTargetTexture2DOES == NULL)
        goto error;

    priv->gl.BindTexture =
        vlc_gl_GetProcAddress(interop->gl, "glBindTexture");
    if (priv->gl.BindTexture == NULL)
        goto error;

    /* The pictures are uploaded upside-down */
    video_format_TransformBy(&interop->fmt_out, TRANSFORM_VFLIP);

    interop->tex_target = GL_TEXTURE_2D;
    interop->fmt_out.i_chroma = priv->exporter->format.chroma;
    interop->fmt_out.space = interop->fmt_in.space;

    static const struct vlc_gl_interop_ops ops = {
        .update_textures = Update,
        .close = Close,
    };
    interop->ops = &ops;

    return VLC_SUCCESS;
error:
    if (priv->exporter != NULL)
        vlc_dmabuf_exporter_Delete(priv->exporter);
    free(priv);
    return VLC_EGENERIC;
}

vlc_module_begin()
    set_description("dmabuf OpenGL interop")
    set_capability("glinterop", 1)
    set_callback(Open)
    set_subcategory(SUBCAT_VIDEO_VOUT)

    // keep the previous interop name that are merged into this module
    // for backward compatibility
    add_shortcut("dmabuf", "vaapi")
vlc_module_end()
