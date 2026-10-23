# Architecture of the OpenGL pipeline{#opengl_architecture}

The OpenGL pipeline is meant to integrate within the usual VLC pipeline
while decoupling dependencies between texture consumers, client code and
context handling. It means that some capabilities and modules exposed
for OpenGL are duplicating, or more precisely, specializing some other
capabilities from the usual VLC video pipeline, while tackling systemic
issues from the design of OpenGL.

It includes the following module capabilities:

- OpenGL providers: `opengl`, `opengl offscreen`, `opengl es2`,
    `opengl es2 offscreen`
- OpenGL interop: `glinterop`
- OpenGL filters: `opengl filter`

And also exposes through those capabilities:

- [vout display](@ref vout_display_t): the `gl` vout display
- [video filter](@ref filter_t): the `opengl` video filter

This document provides some insight of the different element of the
pipeline.

@startuml
object "OpenGL Client" as client
object "OpenGL Interop" as interop
object "OpenGL provider" as vlc_gl
@enduml

## Context handling:

VLC core infrastructure supports both `OpenGL` and `OpenGL ES2`
providers.

Although the headers need to be present to compile the code, and thus a
first choice must be made at compile time between the two, the code is
made to dynamically detect which flavour is used and switch at runtime
to the correct code matching this flavour.

The code itself for the clients doesn't need to be compiled twice and
have different capabilities depending on which flavour is currently in
use, meaning that the code can even be written in a compatible way
between both flavour.

In addition, the core supports both on-screen and off-screen OpenGL
providers. Both are providing the same API, except for swapping which
will be detailed below.

The on-screen flavour is initialized over a native surface represented
by a [vlc_window_t](@ref vlc_window_t) configuration. Swapping will
directly present the rendered picture on the given surface.

The off-screen flavour can be created without window, but swapping will
instead return a [picture_t](@ref picture_t) object that the client can
forward to any other consumer, just like if it were produced by a
decoder or a filter.

## Picture handling

As this documentation is written, there is no "OpenGL picture format"
per se. Each producer of [picture_t](@ref picture_t) that can be
compatible with OpenGL through a specific shader or sampling is instead
directly supported by the OpenGL clients through the [opengl
interop](@ref struct vlc_gl_interop) mechanism.

A [vlc_gl_interop](@ref vlc_gl_interop) basically converts a supported
[picture_t](@ref picture_t) object into a bunch of OpenGL textures
allocated in the currently enabled context, with a new chroma layout
descriptor. It also defines how the raw pixels will be sampled from the
input textures.

So as to correctly interpret the raw pixel values, chroma conversion and
transfer function must be applied before usage though, which is done by
a sampler, as we'll see later in the following.

```c
/**
 * EXAMPLE: Show how an OpenGL interop is used in the core to match a
 *          picture_t object with the corresponding OpenGL textures.
 *
 * DISCLAIMER: error handling is not done here.
 *
 * - `gl` is the provided OpenGL context, see above.
 * - `api` is the virtual table compound of OpenGL functions.
 * - `pic` is the `picture_t` we want to use in our client code.
 */
int some_opengl_client(vlc_gl_t *gl, vlc_gl_api_t *api, picture_t *pic)
{
    int ret;

    struct vlc_video_context = picture_GetVideoContext(pic);
    video_format_t *fmt = &pic->format;

    struct vlc_gl_interop *interop =
        vlc_gl_interop_New(gl, api, context, fmt);
    if (interop == NULL)
        return VLC_EGENERIC;

    GLint textures[MAX_PLANES];
    GLsizei widths[MAX_PLANES], heights[MAX_PLANES];

    ret = vlc_gl_interop_GenerateTextures(interop,
            widths, heights, textures);
    if (ret != VLC_SUCCESS)
        goto release_interop;

    /* This is not usually called directly. */
    ret = interop->ops->update_textures(interop, textures,
            widths, heights, pic, NULL);
    if (ret != VLC_SUCCESS)
        goto release_textures;

    /* Do something with textures / widths / heights. */

release_textures:
    vlc_gl_interop_DeleteTextures(interop, textures);

release_interop:
    vlc_gl_interop_Delete(interop);

    return ret;
}
```

# Implementation of new modules

## Details of what needs to be implemented

### New chroma

A new chroma can be created when a new decoder, filter or OpenGL
offscreen provider is created. When a new kind of picture producer is
created, the newly created chroma will notify the chain --- filters,
video converters and vout display --- which implementation should be
used to interpret the picture data. In particular, in the case of
OpenGL, it will notify the OpenGL pipeline about which OpenGL interop
implementation should be loaded to transform the picture into the
OpenGL texture.

For instance VAAPI buffer images that are output from the decoder might
bear the VLC_CODEC_VAAPI_420 chroma, which will be forwarded to the
client like an OpenGL [vout display](@ref vout_display_t) or
[video filter](@ref filter_t). The client will then probe the OpenGL
interop to find a compatible one with this chroma. The VDPAU interop
will fail because the chroma is unknown to the VDPAU code. A contrario,
the VAAPI interop will succeed if it can initialize itself, because
that's what it's designed for.

The new chroma can either represent some accessible memory mapping
storing the picture planes or represent some abstract resources that can
be used to convert into an OpenGL texture, usually coming from a graphic
driver.

In the former case, the chroma can directly be added to the
`interop_sw.c` file which handles only memory mapped access, so there is
not much to do except matching the description of the planes with a
description of the OpenGL format to use and which chroma is used in
OpenGL afterwards.

In the latter case, if the chroma doesn't match any of the existing
interop modules, it might need a dedicated interop module to be created.
The interop will be in charge of handling the upload, the live-binding
or the copy of the input picture against the matching OpenGL textures.

A new implementation for an interop will need the following interface to
be implemented:

```c
struct vlc_gl_interop_ops {
    int (*allocate_textures)(const struct vlc_gl_interop *interop,
                         GLuint textures[], const GLsizei tex_width[],
                         const GLsizei tex_height[]);
    void (*deallocate_textures)(const struct vlc_gl_interop *interop,
                         uint32_t textures[])
    int (*update_textures)(const struct vlc_gl_interop *interop,
                       GLuint textures[], const GLsizei tex_width[],
                       const GLsizei tex_height[], picture_t *pic,
                       const size_t plane_offsets[]);
    const float * (*get_transform_matrix)(
                       const struct vlc_gl_interop *interop);
    void (*close)(struct vlc_gl_interop *interop);
};
```

 - [allocate_textures](@ref vlc_gl_interop_ops::allocate_textures)
     is called whenever new OpenGL textures needs to be
     allocated by the interop. This implementation is optional and is
     designed for interop that might already allocate and configure
     texture of their own. The default implementation (`= NULL`) will
     allocate usual OpenGL 2D texture. When the texture will actually
     be created or fetched during `update_texture`, the boolean
     `interop->handle_texs_gen` can be set to `true` and no textures
     will be created at all. It would happens in particular when
     integrating an existing high-level interop mechanism.

 - [deallocate_textures](@ref vlc_gl_interop_ops::deallocate_textures)
     is called when the OpenGL textures generated by the interop are not
     needed anymore.

 - [update_textures](@ref vlc_gl_interop_ops::update_textures)
     is doing the actual work of uploading / live-binding / copying the
     input `picture_t` into the OpenGL texture objects previously
     created.

 - [get_transform_matrix](@ref vlc_gl_interop_ops::get_transform_matrix)
     can be used to specify an interop-specific texture transformation
     which can specify how the picture is actually stored, thus how it
     should be sampled at the texture coordinate level.

 - [close](@ref vlc_gl_interop_ops::close)
     is the implementation destructor, freeing resources before the
     interop object is removed.

Most interop should expect to call either a `glTexImage2D` function
(with the standarded prototype being extended to handle the external
data) or something like a `eglCreateImageKHR` function (an external API
function linking an external resource with live-binding to a given
texture) to bind the input picture data to an OpenGL texture.

When this is not possible, a copy is likely to be necessary somehow.

### New platform

A new platform might want to implement dedicated `opengl provider` and
`opengl offscreen provider` (or the `opengl es2` variant), depending on
what it can provide.

Creating a [vlc_gl_t](@ref vlc_gl_t) on-screen and offscreen module
de-facto enables the platform to use the whole rendering pipeline for
both filtering and display with existing compatible chroma (and thus at
least with UMA chromas).

Sometimes, the [vlc_gl_t](@ref vlc_gl_t) might not fit into a `opengl`
module and will need to be implemented by another capability (like
[vout_display_t](@ref vout_display_t)) which will then require
additional work to reimplement this part of the pipeline. In general,
everything should be done to have [vlc_gl_t](@ref vlc_gl_t)
implementation be written in a `opengl` or `opengl es2` implementation
to avoid code duplication.

Offscreen implementation are usually more likely to fit the API though,
since threading is not an issue for almost all possible underlying API.

Whenever possible, writing the off-screen module and extending it when
implementing the on-screen module will give the less possible
duplication of code and should be favored. In particular, it tends
towards display modules which are efficiently separated from the OpenGL
resource providers, and are focused on presenting the rendered picture
on-screen instead of forcing a rendering (and thus, additional
composition step) to present a picture on-screen.

An alternative solution is to provide an EGL implementation through the
`egl display` capability. An EGL display can upgrade into a full-blown
EGL implementation

### New OpenGL processing filter

To implement a new OpenGL processing pass, a module with capability
[opengl filter](@ref struct vlc_gl_filter) must be implemented, and
eventually a shortcut to this module as [video filter](@ref filter_t)
can be implemented.

To implement such shortcut, the `opengl` wrapper video filter can be
used in the following way:

```c
static int OpenVideoFilter(vlc_object_t *obj)
{
    filter_t *filter = (filter_t*)obj;

    module_t *module = vlc_gl_WrapOpenGLFilter(filter, "mymodulename");
    if (module == NULL)
        return VLC_EGENERIC;

    return VLC_SUCCESS;
}
```
