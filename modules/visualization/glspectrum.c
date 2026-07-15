/*****************************************************************************
 * glspectrum.c: spectrum visualization module based on OpenGL
 *****************************************************************************
 * Copyright © 2009-2013 VLC authors and VideoLAN
 *
 * Authors: Adrien Maglo <magsoft@videolan.org>
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

#include <assert.h>

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_aout.h>
#include <vlc_window.h>
#include <vlc_opengl.h>
#include <vlc_filter.h>
#include <vlc_queue.h>
#include <vlc_rand.h>

#include <math.h>

#include "visual/fft.h"
#include "visual/window.h"

#include "../video_output/opengl/gl_common.h"
#include "../video_output/opengl/gl_util.h"


/*****************************************************************************
 * Module descriptor
 *****************************************************************************/
static int Open(vlc_object_t *);
static void Close(filter_t *);

#define WIDTH_TEXT N_("Video width")
#define WIDTH_LONGTEXT N_("The width of the visualization window, in pixels.")

#define HEIGHT_TEXT N_("Video height")
#define HEIGHT_LONGTEXT N_("The height of the visualization window, in pixels.")

#define LOG_OFFSET 0.1

vlc_module_begin()
    set_shortname(N_("glSpectrum"))
    set_description(N_("3D OpenGL spectrum visualization"))
    set_capability("visualization", 0)
    set_subcategory(SUBCAT_AUDIO_VISUAL)

    add_integer("glspectrum-width", 400, WIDTH_TEXT, WIDTH_LONGTEXT)
    add_integer("glspectrum-height", 300, HEIGHT_TEXT, HEIGHT_LONGTEXT)

    add_shortcut("glspectrum")
    set_callback(Open)
vlc_module_end()

#define OPENGL_VTABLE_F(F) \
    /* Rendering state */ \
    F(PFNGLENABLEPROC,                Enable) \
    F(PFNGLCLEARPROC,                 Clear) \
    F(PFNGLCLEARCOLORPROC,            ClearColor) \
    F(PFNGLDRAWARRAYSPROC,            DrawArrays) \
    F(PFNGLDEPTHMASKPROC,             DepthMask) \
    F(PFNGLBLENDFUNCPROC,             BlendFunc) \
    F(PFNGLVIEWPORTPROC,              Viewport) \
    F(PFNGLGETSTRINGPROC,             GetString) \
    /* Shader commands */ \
    F(PFNGLCREATESHADERPROC,          CreateShader) \
    F(PFNGLSHADERSOURCEPROC,          ShaderSource) \
    F(PFNGLCOMPILESHADERPROC,         CompileShader) \
    F(PFNGLDELETESHADERPROC,          DeleteShader) \
    F(PFNGLGETSHADERIVPROC,           GetShaderiv) \
    F(PFNGLGETSHADERINFOLOGPROC,      GetShaderInfoLog) \
    F(PFNGLGETSHADERSOURCEPROC,       GetShaderSource) \
    F(PFNGLATTACHSHADERPROC,          AttachShader) \
    /* Program commands */ \
    F(PFNGLCREATEPROGRAMPROC,         CreateProgram) \
    F(PFNGLLINKPROGRAMPROC,           LinkProgram) \
    F(PFNGLUSEPROGRAMPROC,            UseProgram) \
    F(PFNGLDELETEPROGRAMPROC,         DeleteProgram) \
    F(PFNGLGETPROGRAMIVPROC,          GetProgramiv) \
    F(PFNGLGETPROGRAMINFOLOGPROC,     GetProgramInfoLog) \
    F(PFNGLGETATTACHEDSHADERPROC,     GetAttachedShaders) \
    /* Uniform and attribute commands */ \
    F(PFNGLGETUNIFORMLOCATIONPROC,      GetUniformLocation) \
    F(PFNGLGETATTRIBLOCATIONPROC,       GetAttribLocation) \
    F(PFNGLUNIFORMMATRIX4FVPROC,        UniformMatrix4fv) \
    F(PFNGLUNIFORM4FVPROC,              Uniform4fv) \
    F(PFNGLVERTEXATTRIBPOINTERPROC,     VertexAttribPointer) \
    F(PFNGLENABLEVERTEXATTRIBARRAYPROC, EnableVertexAttribArray) \
    /* Buffer commands */ \
    F(PFNGLGENBUFFERSPROC,            GenBuffers) \
    F(PFNGLDELETEBUFFERSPROC,         DeleteBuffers) \
    F(PFNGLBINDBUFFERPROC,            BindBuffer) \
    F(PFNGLBUFFERDATAPROC,            BufferData) \
    /* Vertex array commands */ \
    F(PFNGLGENVERTEXARRAYSPROC,       GenVertexArrays) \
    F(PFNGLDELETEVERTEXARRAYSPROC,    DeleteVertexArrays) \
    F(PFNGLBINDVERTEXARRAYPROC,       BindVertexArray)

/*****************************************************************************
 * Local prototypes
 *****************************************************************************/
typedef struct
{
    vlc_thread_t thread;

    /* Audio data */
    vlc_queue_t queue;
    bool dead;
    unsigned i_channels;
    unsigned i_prev_nb_samples;
    int16_t *p_prev_s16_buff;

    /* Opengl */
    vlc_gl_t *gl;
    opengl_vtable_t vt;

    GLuint program;
    GLuint vao;
    GLuint vbo;

    struct {
        GLint mvp;
        GLint model;
        GLint color;
    } uloc;
    struct {
        GLint position;
        GLint normal;
    } aloc;

    float f_rotationAngle;
    float f_rotationIncrement;

    /* FFT window parameters */
    window_param wind_param;
} filter_sys_t;


static block_t *DoWork(filter_t *, block_t *);
static void *Thread(void *);

#define SPECTRUM_WIDTH 4.f
#define NB_BANDS 20
#define ROTATION_INCREMENT .1f
#define BAR_DECREMENT .075f
#define ROTATION_MAX 20
#define BAR_VERTEX_COUNT (6 * 5)

static const struct vlc_filter_operations filter_ops = {
    .filter_audio = DoWork, .close = Close,
};

static int InitOpenGLResources(filter_t *p_filter);
static void DestroyOpenGLResources(filter_t *p_filter);


/**
 * Open the module.
 * @param p_this: the filter object
 * @return VLC_SUCCESS or vlc error codes
 */
static int Open(vlc_object_t * p_this)
{
    filter_t *p_filter = (filter_t *)p_this;
    filter_sys_t *p_sys = vlc_obj_malloc(p_this, sizeof (*p_sys));

    if (p_sys == NULL)
        return VLC_ENOMEM;

    p_filter->p_sys = p_sys;

    /* Create the object for the thread */
    p_sys->i_channels = aout_FormatNbChannels(&p_filter->fmt_in.audio);
    p_sys->i_prev_nb_samples = 0;
    p_sys->p_prev_s16_buff = NULL;

    p_sys->f_rotationAngle = 0;
    p_sys->f_rotationIncrement = ROTATION_INCREMENT;

    /* Fetch the FFT window parameters */
    window_get_param( VLC_OBJECT( p_filter ), &p_sys->wind_param );

    /* Create the FIFO for the audio data. */
    vlc_queue_Init(&p_sys->queue, offsetof (block_t, p_next));
    p_sys->dead = false;

    /* Create the openGL provider */
    vlc_window_cfg_t cfg = {
        .is_decorated = true,
        .width = var_InheritInteger(p_filter, "glspectrum-width"),
        .height = var_InheritInteger(p_filter, "glspectrum-height"),
    };

    p_sys->gl = vlc_gl_surface_Create(p_this, &cfg, NULL, NULL);
    if (p_sys->gl == NULL)
        return VLC_EGENERIC;

    if (vlc_gl_MakeCurrent(p_sys->gl) != VLC_SUCCESS)
    {
        msg_Err(p_filter, "Can't attach gl context");
        goto error;
    }

#define LOAD_SYMBOL(type, name) do { \
    p_sys->vt.name = vlc_gl_GetProcAddress(p_sys->gl, "gl" #name); \
    if (p_sys->vt.name == NULL) { \
        msg_Err(p_filter, "Can't load OpenGL symbol gl" #name); \
        vlc_gl_ReleaseCurrent(p_sys->gl); \
        goto error; \
    } \
} while(0);

    OPENGL_VTABLE_F(LOAD_SYMBOL);
#undef LOAD_SYMBOL

    if (InitOpenGLResources(p_filter) != VLC_SUCCESS)
    {
        msg_Err(p_filter, "Can't initialize OpenGL resources");
        vlc_gl_ReleaseCurrent(p_sys->gl);
        goto error;
    }

    vlc_gl_ReleaseCurrent(p_sys->gl);

    /* Create the thread */
    if (vlc_clone(&p_sys->thread, Thread, p_filter)) {
        if (vlc_gl_MakeCurrent(p_sys->gl) == VLC_SUCCESS)
        {
            DestroyOpenGLResources(p_filter);
            vlc_gl_ReleaseCurrent(p_sys->gl);
        }
        vlc_gl_surface_Destroy(p_sys->gl);
        return VLC_ENOMEM;
    }

    p_filter->fmt_in.audio.i_format = VLC_CODEC_FL32;
    p_filter->fmt_out.audio = p_filter->fmt_in.audio;
    p_filter->ops = &filter_ops;

    return VLC_SUCCESS;

error:
    vlc_gl_surface_Destroy(p_sys->gl);
    return VLC_EGENERIC;
}


/**
 * Close the module.
 * @param p_this: the filter object
 */
static void Close(filter_t *p_filter)
{
    filter_sys_t *p_sys = p_filter->p_sys;

    /* Terminate the thread. */
    vlc_queue_Kill(&p_sys->queue, &p_sys->dead);
    vlc_join(p_sys->thread, NULL);

    if (vlc_gl_MakeCurrent(p_sys->gl) == VLC_SUCCESS)
    {
        DestroyOpenGLResources(p_filter);
        vlc_gl_ReleaseCurrent(p_sys->gl);
    }

    /* Free the resources */
    vlc_gl_surface_Destroy(p_sys->gl);
    free(p_sys->p_prev_s16_buff);
}


/**
 * Do the actual work with the new sample.
 * @param p_filter: filter object
 * @param p_in_buf: input buffer
 */
static block_t *DoWork(filter_t *p_filter, block_t *p_in_buf)
{
    filter_sys_t *p_sys = p_filter->p_sys;

    vlc_queue_Enqueue(&p_sys->queue, block_Duplicate(p_in_buf));
    return p_in_buf;
}


/**
  * Init the OpenGL scene.
  **/
static void initOpenGLScene(filter_t *p_filter)
{
    filter_sys_t *p_sys = p_filter->p_sys;
    const opengl_vtable_t *vt = &p_sys->vt;

    vt->ClearColor(0.f, 0.f, 0.f, 0.f);
    vt->Enable(GL_CULL_FACE);
    vt->Enable(GL_DEPTH_TEST);
    vt->DepthMask(GL_TRUE);
    vt->Enable(GL_BLEND);
    vt->BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    vt->UseProgram(p_sys->program);
}


static void Matrix4Identity(float m[static 16])
{
    memcpy(m, MATRIX4_IDENTITY, sizeof(MATRIX4_IDENTITY));
}

static void Matrix4Multiply(float out[static 16],
                            const float a[static 16],
                            const float b[static 16])
{
    float tmp[16];

    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row)
        {
            tmp[col * 4 + row] = 0.f;
            for (int k = 0; k < 4; ++k)
                tmp[col * 4 + row] += a[k * 4 + row] * b[col * 4 + k];
        }

    memcpy(out, tmp, sizeof(tmp));
}

static void Matrix4Frustum(float m[static 16],
                           float left, float right,
                           float bottom, float top,
                           float nearVal, float farVal)
{
    const float rl = right - left;
    const float tb = top - bottom;
    const float fn = farVal - nearVal;

    const float m_values[16] = {
        2.f * nearVal / rl, 0.f, 0.f, 0.f,
        0.f, 2.f * nearVal / tb, 0.f, 0.f,
        (right + left) / rl, (top + bottom) / tb, -(farVal + nearVal) / fn, -1.f,
        0.f, 0.f, -2.f * farVal * nearVal / fn, 0.f,
    };

    memcpy(m, m_values, sizeof(m_values));
}

static void Matrix4Translate(float m[static 16], float x, float y, float z)
{
    float t[16];
    Matrix4Identity(t);
    t[12] = x;
    t[13] = y;
    t[14] = z;
    Matrix4Multiply(m, m, t);
}

static void Matrix4Scale(float m[static 16], float x, float y, float z)
{
    float s[16];
    Matrix4Identity(s);
    s[0] = x;
    s[5] = y;
    s[10] = z;
    Matrix4Multiply(m, m, s);
}

static void Matrix4RotateY(float m[static 16], float angle_deg)
{
    const float rad = angle_deg * (float)M_PI / 180.f;
    const float c = cosf(rad);
    const float s = sinf(rad);
    float r[16];

    Matrix4Identity(r);
    r[0] = c;
    r[2] = s;
    r[8] = -s;
    r[10] = c;
    Matrix4Multiply(m, m, r);
}

static void getBarColor(float f_height, float color[static 4])
{
    float r, b;

#define BAR_MAX_HEIGHT 4.2f
    r = -1.f + 2 / BAR_MAX_HEIGHT * f_height;
    b = 2.f - 2 / BAR_MAX_HEIGHT * f_height;
#undef BAR_MAX_HEIGHT

    r = VLC_CLIP(r, 0.f, 1.f);
    b = VLC_CLIP(b, 0.f, 1.f);

    color[0] = r;
    color[1] = 0.f;
    color[2] = b;
    color[3] = 1.f;
}

static int InitShaderProgram(filter_t *p_filter)
{
    filter_sys_t *p_sys = p_filter->p_sys;
    const opengl_vtable_t *vt = &p_sys->vt;
    char *glsl_version = NULL;

    static const char *const VERTEX_SHADER_SRC =
        "#ifdef GL_ES\n"
        "precision mediump float;\n"
        "#endif\n"
        "#if __VERSION__ < 300\n"
        "attribute vec3 a_position;\n"
        "attribute vec3 a_normal;\n"
        "varying vec3 v_normal;\n"
        "varying vec4 v_color;\n"
        "#else\n"
        "in vec3 a_position;\n"
        "in vec3 a_normal;\n"
        "out vec3 v_normal;\n"
        "out vec4 v_color;\n"
        "#endif\n"
        "uniform mat4 u_mvp;\n"
        "uniform mat4 u_model;\n"
        "uniform vec4 u_color;\n"
        "void main() {\n"
        "  gl_Position = u_mvp * vec4(a_position, 1.0);\n"
        "  v_normal = mat3(u_model) * a_normal;\n"
        "  v_color = u_color;\n"
        "}\n";

    static const char *const FRAGMENT_SHADER_SRC =
        "#ifdef GL_ES\n"
        "precision mediump float;\n"
        "#endif\n"
        "#if __VERSION__ < 300\n"
        "#define FragColor gl_FragColor\n"
        "varying vec3 v_normal;\n"
        "varying vec4 v_color;\n"
        "#else\n"
        "out vec4 FragColor;\n"
        "in vec3 v_normal;\n"
        "in vec4 v_color;\n"
        "#endif\n"
        "void main() {\n"
        "  vec3 normal = normalize(v_normal);\n"
        "  vec3 light_dir = normalize(vec3(0.0, 3.0, 10.0));\n"
        "  float diffuse = max(dot(normal, light_dir), 0.0);\n"
        "  vec3 rgb = v_color.rgb * (0.2 + 0.8 * diffuse);\n"
        "  FragColor = vec4(rgb, v_color.a);\n"
        "}\n";

    if (p_sys->gl->api_type == VLC_OPENGL_ES2)
        glsl_version = strdup("#version 100\n");
    else
    {
        int glsl = 120;
        const GLubyte *str = vt->GetString(GL_SHADING_LANGUAGE_VERSION);
        if (str != NULL)
        {
            int major, minor;
            if (sscanf((const char *)str, "%d.%d", &major, &minor) == 2)
                glsl = major * 100 + minor;
        }
        int target_version = __MIN(glsl, 410);
        if (asprintf(&glsl_version, "#version %d\n", target_version) <= 0)
            return VLC_ENOMEM;
    }
    if (glsl_version == NULL)
        return VLC_ENOMEM;

    const char *vertex_shader[] = { glsl_version, VERTEX_SHADER_SRC };
    const char *fragment_shader[] = { glsl_version, FRAGMENT_SHADER_SRC };

    p_sys->program = vlc_gl_BuildProgram(VLC_OBJECT(p_filter), vt,
                                         2, vertex_shader,
                                         2, fragment_shader);
    free(glsl_version);

    if (!p_sys->program)
        return VLC_EGENERIC;

#define GET_LOC(type, x, str) do { \
    x = vt->Get##type##Location(p_sys->program, str); \
    if (x == -1) { \
        msg_Err(p_filter, "Unable to Get"#type"Location(%s)", str); \
        return VLC_EGENERIC; \
    } \
} while (0)
#define GET_ULOC(x, str) GET_LOC(Uniform, x, str)
#define GET_ALOC(x, str) GET_LOC(Attrib, x, str)

    GET_ULOC(p_sys->uloc.mvp, "u_mvp");
    GET_ULOC(p_sys->uloc.model, "u_model");
    GET_ULOC(p_sys->uloc.color, "u_color");
    GET_ALOC(p_sys->aloc.position, "a_position");
    GET_ALOC(p_sys->aloc.normal, "a_normal");

#undef GET_ULOC
#undef GET_ALOC
#undef GET_LOC

    return VLC_SUCCESS;
}

static int InitBarGeometry(filter_t *p_filter)
{
    filter_sys_t *p_sys = p_filter->p_sys;
    const opengl_vtable_t *vt = &p_sys->vt;
    const float w = SPECTRUM_WIDTH / NB_BANDS - 0.05f;

    const GLfloat vertexCoords[] = {
        0.f, 0.f, 0.f,     w, 0.f, 0.f,   0.f, 1.f, 0.f,
        0.f, 1.f, 0.f,     w, 0.f, 0.f,     w, 1.f, 0.f,

        0.f, 0.f, -w,    0.f, 0.f, 0.f,   0.f, 1.f,  -w,
        0.f, 1.f, -w,    0.f, 0.f, 0.f,   0.f, 1.f, 0.f,

          w, 0.f, 0.f,     w, 0.f,  -w,     w, 1.f, 0.f,
          w, 1.f, 0.f,     w, 0.f,  -w,     w, 1.f,  -w,

          w, 0.f,  -w,   0.f, 0.f,  -w,   0.f, 1.f,  -w,
        0.f, 1.f,  -w,     w, 1.f,  -w,     w, 0.f,  -w,

        0.f, 1.f, 0.f,     w, 1.f, 0.f,     w, 1.f,  -w,
        0.f, 1.f, 0.f,     w, 1.f,  -w,   0.f, 1.f,  -w,
    };

    const GLfloat normals[] = {
        0.f, 0.f, 1.f,   0.f, 0.f, 1.f,   0.f, 0.f, 1.f,
        0.f, 0.f, 1.f,   0.f, 0.f, 1.f,   0.f, 0.f, 1.f,

        -1.f, 0.f, 0.f,   -1.f, 0.f, 0.f,   -1.f, 0.f, 0.f,
        -1.f, 0.f, 0.f,   -1.f, 0.f, 0.f,   -1.f, 0.f, 0.f,

        1.f, 0.f, 0.f,   1.f, 0.f, 0.f,   1.f, 0.f, 0.f,
        1.f, 0.f, 0.f,   1.f, 0.f, 0.f,   1.f, 0.f, 0.f,

        0.f, 0.f, -1.f,   0.f, 0.f, -1.f,   0.f, 0.f, -1.f,
        0.f, 0.f, -1.f,   0.f, 0.f, -1.f,   0.f, 0.f, -1.f,

        0.f, 1.f, 0.f,   0.f, 1.f, 0.f,   0.f, 1.f, 0.f,
        0.f, 1.f, 0.f,   0.f, 1.f, 0.f,   0.f, 1.f, 0.f,
    };

    struct bar_vertex {
        float position[3];
        float normal[3];
    } vertices[BAR_VERTEX_COUNT];

    for (unsigned i = 0; i < BAR_VERTEX_COUNT; ++i)
    {
        memcpy(vertices[i].position, &vertexCoords[i * 3], sizeof(vertices[i].position));
        memcpy(vertices[i].normal, &normals[i * 3], sizeof(vertices[i].normal));
    }

    vt->GenVertexArrays(1, &p_sys->vao);
    vt->GenBuffers(1, &p_sys->vbo);

    vt->BindVertexArray(p_sys->vao);
    vt->BindBuffer(GL_ARRAY_BUFFER, p_sys->vbo);
    vt->BufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    vt->VertexAttribPointer(p_sys->aloc.position, 3, GL_FLOAT, GL_FALSE,
                            sizeof(struct bar_vertex),
                            (const void *)offsetof(struct bar_vertex, position));
    vt->EnableVertexAttribArray(p_sys->aloc.position);

    vt->VertexAttribPointer(p_sys->aloc.normal, 3, GL_FLOAT, GL_FALSE,
                            sizeof(struct bar_vertex),
                            (const void *)offsetof(struct bar_vertex, normal));
    vt->EnableVertexAttribArray(p_sys->aloc.normal);

    vt->BindVertexArray(0);
    vt->BindBuffer(GL_ARRAY_BUFFER, 0);

    return VLC_SUCCESS;
}

static void DestroyOpenGLResources(filter_t *p_filter)
{
    filter_sys_t *p_sys = p_filter->p_sys;
    const opengl_vtable_t *vt = &p_sys->vt;

    if (p_sys->vbo)
        vt->DeleteBuffers(1, &p_sys->vbo);
    if (p_sys->vao)
        vt->DeleteVertexArrays(1, &p_sys->vao);
    if (p_sys->program)
        vt->DeleteProgram(p_sys->program);

    p_sys->vbo = 0;
    p_sys->vao = 0;
    p_sys->program = 0;
}

static int InitOpenGLResources(filter_t *p_filter)
{
    int ret = InitShaderProgram(p_filter);
    if (ret != VLC_SUCCESS)
        return ret;

    ret = InitBarGeometry(p_filter);
    if (ret != VLC_SUCCESS)
    {
        DestroyOpenGLResources(p_filter);
        return ret;
    }

    return VLC_SUCCESS;
}


/**
 * Draw all the bars of the spectrum.
 * @param heights the heights of all the bars.
 */
static void drawBars(filter_t *p_filter, const float heights[static NB_BANDS])
{
    filter_sys_t *p_sys = p_filter->p_sys;
    const opengl_vtable_t *vt = &p_sys->vt;

    float projection[16];
    float view[16];
    float model[16];
    float mvp[16];

    Matrix4Frustum(projection, -1.f, 1.f, -1.f, 1.f, 0.5f, 10.f);

    Matrix4Identity(view);
    Matrix4Translate(view, 0.f, -2.f, -2.f);
    Matrix4RotateY(view, p_sys->f_rotationAngle);

    vt->BindVertexArray(p_sys->vao);

    const float w = SPECTRUM_WIDTH / NB_BANDS;
    for (unsigned i = 0; i < NB_BANDS; ++i)
    {
        float bar_model[16];

        Matrix4Identity(bar_model);
        Matrix4Translate(bar_model, -2.f + i * w, 0.f, 0.f);
        Matrix4Scale(bar_model, 1.f, heights[i], 1.f);

        Matrix4Multiply(model, view, bar_model);

        Matrix4Multiply(mvp, projection, model);

        float color[4];
        getBarColor(heights[i], color);

        vt->UniformMatrix4fv(p_sys->uloc.mvp, 1, GL_FALSE, mvp);
        vt->UniformMatrix4fv(p_sys->uloc.model, 1, GL_FALSE, model);
        vt->Uniform4fv(p_sys->uloc.color, 1, color);

        vt->DrawArrays(GL_TRIANGLES, 0, BAR_VERTEX_COUNT);
    }

    vt->BindVertexArray(0);
}


/**
 * Update thread which do the rendering
 * @param p_this: the p_thread object
 */
static void *Thread( void *p_data )
{
    vlc_thread_set_name("vlc-glspectrum");

    filter_t  *p_filter = (filter_t*)p_data;
    filter_sys_t *p_sys = p_filter->p_sys;
    vlc_gl_t *gl = p_sys->gl;
    block_t *block;

    if (vlc_gl_MakeCurrent(gl) != VLC_SUCCESS)
    {
        msg_Err(p_filter, "Can't attach gl context");
        return NULL;
    }

    const opengl_vtable_t *vt = &p_sys->vt;

    initOpenGLScene(p_filter);
    float height[NB_BANDS] = {0};

    while ((block = vlc_queue_DequeueKillable(&p_sys->queue, &p_sys->dead)))
    {
        unsigned win_width, win_height;

        if (vlc_gl_surface_CheckSize(gl, &win_width, &win_height))
            vt->Viewport(0, 0, win_width, win_height);

        /* Horizontal scale for 20-band equalizer */
        const unsigned xscale[] = {0,1,2,3,4,5,6,7,8,11,15,20,27,
                                   36,47,62,82,107,141,184,255};

        fft_state *p_state = NULL; /* internal FFT data */
        DEFINE_WIND_CONTEXT(wind_ctx); /* internal window data */

        unsigned i, j;
        float p_output[FFT_BUFFER_SIZE];           /* Raw FFT Result  */
        int16_t p_buffer1[FFT_BUFFER_SIZE];        /* Buffer on which we perform
                                                      the FFT (first channel) */
        uint16_t p_dest[FFT_BUFFER_SIZE];          /* Adapted FFT result */
        float *p_buffl = (float*)block->p_buffer;  /* Original buffer */

        int16_t  *p_buffs;                         /* int16_t converted buffer */
        int16_t  *p_s16_buff;                      /* int16_t converted buffer */

        if (!block->i_nb_samples) {
            msg_Err(p_filter, "no samples yet");
            goto release;
        }

        /* Allocate the buffer only if the number of samples change */
        if (block->i_nb_samples != p_sys->i_prev_nb_samples)
        {
            free(p_sys->p_prev_s16_buff);
            p_sys->p_prev_s16_buff = malloc(block->i_nb_samples *
                                            p_sys->i_channels *
                                            sizeof(int16_t));
            if (!p_sys->p_prev_s16_buff)
                goto release;
            p_sys->i_prev_nb_samples = block->i_nb_samples;
        }
        p_buffs = p_s16_buff = p_sys->p_prev_s16_buff;

        /* Convert the buffer to int16_t
           Pasted from float32tos16.c */
        for (i = block->i_nb_samples * p_sys->i_channels; i--;)
        {
            union {float f; int32_t i;} u;

            u.f = *p_buffl + 384.f;
            if (u.i > 0x43c07fff)
                *p_buffs = 32767;
            else if (u.i < 0x43bf8000)
                *p_buffs = -32768;
            else
                *p_buffs = u.i - 0x43c00000;

            p_buffl++; p_buffs++;
        }
        p_state = visual_fft_init();
        if (!p_state)
        {
            msg_Err(p_filter,"unable to initialize FFT transform");
            goto release;
        }
        if (!window_init(FFT_BUFFER_SIZE, &p_sys->wind_param, &wind_ctx))
        {
            msg_Err(p_filter,"unable to initialize FFT window");
            goto release;
        }
        p_buffs = p_s16_buff;
        for (i = 0 ; i < FFT_BUFFER_SIZE; i++)
        {
            p_output[i] = 0;
            p_buffer1[i] = *p_buffs;

            p_buffs += p_sys->i_channels;
            if (p_buffs >= &p_s16_buff[block->i_nb_samples * p_sys->i_channels])
                p_buffs = p_s16_buff;
        }
        window_scale_in_place (p_buffer1, &wind_ctx);
        fft_perform (p_buffer1, p_output, p_state);

        for( i = 0; i< FFT_BUFFER_SIZE ; i++ )
        {
            /* Scale the output between 0 and UINT16MAX */
            p_dest[i] = p_output[i] * UINT16_MAX / FFT_SCALING_VALUE;
        }

        for (i = 0 ; i < NB_BANDS; i++)
        {
            /* Decrease the previous size of the bar. */
            height[i] -= BAR_DECREMENT;
            if (height[i] < 0)
                height[i] = 0;

            int y = 0;
            /* We search the maximum on one scale
               to determine the current size of the bar. */
            for (j = xscale[i]; j < xscale[i + 1]; j++)
            {
                if (p_dest[j] > y)
                     y = p_dest[j];
            }
            /* Calculate the height of the bar
               This log_offset makes it possible to display low values */
            float new_height = y != 0 ? logf( y + LOG_OFFSET ) * 0.4f : 0;
            height[i] = new_height > height[i]
                        ? new_height : height[i];
        }

        /* Determine the camera rotation angle. */
        p_sys->f_rotationAngle += p_sys->f_rotationIncrement;
        if (p_sys->f_rotationAngle <= -ROTATION_MAX)
            p_sys->f_rotationIncrement = ROTATION_INCREMENT;
        else if (p_sys->f_rotationAngle >= ROTATION_MAX)
            p_sys->f_rotationIncrement = -ROTATION_INCREMENT;

        /* Render the frame. */
        vt->Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        drawBars(p_filter, height);

        /* Wait to swapp the frame on time. */
        vlc_tick_wait(block->i_pts + (block->i_length / 2));
        vlc_gl_Swap(gl);

release:
        window_close(&wind_ctx);
        fft_close(p_state);
        block_Release(block);
    }
    vlc_gl_ReleaseCurrent(gl);

    return NULL;
}
