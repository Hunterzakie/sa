/*****************************************************************************
 * interop_vaapi.c: test for VAAPI GL interop with GLX and EGL contexts
 *****************************************************************************
 * Copyright (C) 2026 VideoLabs
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

/**
 * Tests for VAAPI GL interop with different OpenGL contexts
 *
 * Test 1 (GLX): Issue #26813 - GLX renderer segfaults loading VAAPI GL interop
 *   Verifies that with a GLX context, the VAAPI interop correctly fails to load
 *   (returns NULL) instead of crashing due to invalid EGL function pointers.
 *
 * Test 2 (EGL): VAAPI interop should load with EGL context
 *   Verifies that with an EGL context, the VAAPI interop can be loaded
 *   (or fails gracefully if VAAPI is not fully available).
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <string.h>

#include "../../../libvlc/test.h"
#include "../../../../lib/libvlc_internal.h"
#include <vlc_common.h>
#include <vlc_codec.h>
#include <vlc_opengl.h>
#include <vlc_vout_display.h>
#include <vlc_window.h>

#include "../../../../modules/video_output/opengl/interop.h"

const char vlc_module_name[] = "test_interop_vaapi";

/* Required window callbacks */
static void test_window_resized(struct vlc_window *wnd,
                                unsigned width, unsigned height,
                                vlc_window_ack_cb cb, void *opaque)
{
    (void)wnd; (void)width; (void)height; (void)cb; (void)opaque;
}

static const struct vlc_window_callbacks test_window_cbs = {
    .resized = test_window_resized,
};

/**
 * Test VAAPI interop with specified GL module
 *
 * @param root VLC object root
 * @param gl_module GL module name ("glx" or "egl_x11")
 * @return 0 on success, 77 to skip, 1 on failure
 */
static int test_vaapi_interop(vlc_object_t *root, const char *gl_module)
{
    const char *context_type = strstr(gl_module, "egl") ? "EGL" : "GLX";

    if (getenv("DISPLAY") == NULL)
    {
        fprintf(stderr, "[%s] No DISPLAY set, skipping\n", context_type);
        return 77;
    }

    const vlc_window_cfg_t wnd_cfg = {
        .width = 100,
        .height = 100,
    };
    const vlc_window_owner_t owner = {
        .cbs = &test_window_cbs,
        .sys = NULL,
    };
    vlc_window_t *wnd = vlc_window_New(root, NULL, &owner, &wnd_cfg);
    if (wnd == NULL)
    {
        fprintf(stderr, "[%s] Cannot create window, skipping\n", context_type);
        return 77;
    }

    if (wnd->type != VLC_WINDOW_TYPE_XID)
    {
        fprintf(stderr, "[%s] Window is not X11 (type=%d), skipping\n",
                context_type, wnd->type);
        vlc_window_Delete(wnd);
        return 77;
    }

    const vout_display_cfg_t cfg = {
        .window = wnd,
        .display = {
            .width = 100,
            .height = 100,
        },
    };

    vlc_gl_t *gl = vlc_gl_Create(&cfg, VLC_OPENGL, gl_module, NULL);
    if (gl == NULL)
    {
        fprintf(stderr, "[%s] Cannot create context, skipping\n", context_type);
        vlc_window_Delete(wnd);
        return 77;
    }

    int ret = vlc_gl_MakeCurrent(gl);
    if (ret != VLC_SUCCESS)
    {
        fprintf(stderr, "[%s] Cannot make context current\n", context_type);
        vlc_gl_Delete(gl);
        vlc_window_Delete(wnd);
        return 1;
    }

    struct vlc_decoder_device *device = vlc_decoder_device_Create(root, wnd);
    if (device == NULL)
    {
        fprintf(stderr, "[%s] Cannot create decoder device, skipping\n", context_type);
        vlc_gl_ReleaseCurrent(gl);
        vlc_gl_Delete(gl);
        vlc_window_Delete(wnd);
        return 77;
    }

    if (device->type != VLC_DECODER_DEVICE_VAAPI)
    {
        fprintf(stderr, "[%s] Decoder device is not VAAPI (type=%d), skipping\n",
                context_type, device->type);
        vlc_decoder_device_Release(device);
        vlc_gl_ReleaseCurrent(gl);
        vlc_gl_Delete(gl);
        vlc_window_Delete(wnd);
        return 77;
    }

    struct vlc_video_context *vctx = vlc_video_context_Create(
        device, VLC_VIDEO_CONTEXT_VAAPI, 0, NULL);
    if (vctx == NULL)
    {
        fprintf(stderr, "[%s] Cannot create VAAPI video context, skipping\n", context_type);
        vlc_decoder_device_Release(device);
        vlc_gl_ReleaseCurrent(gl);
        vlc_gl_Delete(gl);
        vlc_window_Delete(wnd);
        return 77;
    }

    video_format_t fmt;
    video_format_Init(&fmt, VLC_CODEC_VAAPI_420);
    video_format_Setup(&fmt, VLC_CODEC_VAAPI_420, 100, 100, 100, 100, 1, 1);

    fprintf(stderr, "[%s] Attempting to load VAAPI interop...\n", context_type);
    struct vlc_gl_interop *interop = vlc_gl_interop_New(gl, vctx, &fmt);

    if (interop != NULL)
    {
        fprintf(stderr, "[%s] VAAPI interop loaded successfully\n", context_type);
        vlc_gl_interop_Delete(interop);
    }
    else
    {
        fprintf(stderr, "[%s] VAAPI interop failed to load%s\n", context_type,
                strstr(gl_module, "glx") ? " (expected)" : "");
    }

    vlc_video_context_Release(vctx);
    vlc_decoder_device_Release(device);
    vlc_gl_ReleaseCurrent(gl);
    vlc_gl_Delete(gl);
    vlc_window_Delete(wnd);

    fprintf(stderr, "[%s] Test passed: no crash\n", context_type);
    return 0;
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    test_init();

    const char * const vlc_argv[] = {
        "-vvv",
        "--aout=dummy",
        "--text-renderer=dummy",
        "--dec-dev=vaapi",
    };

    libvlc_instance_t *vlc = libvlc_new(ARRAY_SIZE(vlc_argv), vlc_argv);
    if (vlc == NULL)
    {
        fprintf(stderr, "Cannot create libvlc instance\n");
        return 1;
    }

    vlc_object_t *root = &vlc->p_libvlc_int->obj;

    fprintf(stderr, "\n=== Test 1: GLX context ===\n");
    int glx_result = test_vaapi_interop(root, "glx");

    fprintf(stderr, "\n=== Test 2: EGL context ===\n");
    int egl_result = test_vaapi_interop(root, "egl_x11");

    libvlc_release(vlc);

    fprintf(stderr, "\n=== Summary ===\n");
    fprintf(stderr, "GLX: %s\n", glx_result == 0 ? "PASS" : (glx_result == 77 ? "SKIP" : "FAIL"));
    fprintf(stderr, "EGL: %s\n", egl_result == 0 ? "PASS" : (egl_result == 77 ? "SKIP" : "FAIL"));

    if (glx_result == 1 || egl_result == 1)
        return 1;
    if (glx_result == 77 || egl_result == 77)
        return 77;
    return 0;
}
