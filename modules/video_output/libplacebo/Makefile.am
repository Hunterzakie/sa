### libplacebo ###

LIBPLACEBO_COMMONSOURCES = video_output/libplacebo/utils.h \
	video_output/libplacebo/instance.h

libplacebo_utils_la_SOURCES = $(LIBPLACEBO_COMMONSOURCES) \
			      video_output/libplacebo/utils.c
libplacebo_utils_la_CFLAGS = $(AM_CFLAGS) $(LIBPLACEBO_CFLAGS)
libplacebo_utils_la_LIBADD = $(LIBPLACEBO_LIBS)
libplacebo_utils_la_LDFLAGS = -static -undefined

noinst_LTLIBRARIES += libplacebo_utils.la

libplacebo_plugin_la_SOURCES = $(LIBPLACEBO_COMMONSOURCES) \
			  video_output/libplacebo/instance.c \
			  video_output/libplacebo/display.c
libplacebo_plugin_la_CFLAGS = $(AM_CFLAGS) $(LIBPLACEBO_CFLAGS)
libplacebo_plugin_la_LIBADD = $(LIBPLACEBO_LIBS) libplacebo_utils.la
if HAVE_VULKAN
libplacebo_plugin_la_CFLAGS += $(VULKAN_CFLAGS) -DHAVE_VULKAN
libplacebo_plugin_la_LIBADD += libplacebo_vkinterop.la
endif

vout_PLUGINS += libplacebo_plugin.la

### vkinterop loader (hardware decoder interop) ###
libplacebo_vkinterop_la_SOURCES = \
	video_output/libplacebo/vk_interop.h \
	video_output/libplacebo/vk_interop.c \
	video_output/vulkan/common.h \
	video_output/vulkan/common.c \
	video_output/vulkan/device.h \
	video_output/vulkan/device.c \
	video_output/vulkan/device_utils.h \
	video_output/vulkan/device_utils.c \
	video_output/vulkan/vk_funcs.h
libplacebo_vkinterop_la_CFLAGS = $(AM_CFLAGS) $(LIBPLACEBO_CFLAGS) $(VULKAN_CFLAGS)
libplacebo_vkinterop_la_LIBADD = $(LIBPLACEBO_LIBS)
libplacebo_vkinterop_la_LDFLAGS = -static -undefined

if HAVE_VULKAN
noinst_LTLIBRARIES += libplacebo_vkinterop.la
endif

### NVDEC interop
libplacebo_nvdec_plugin_la_SOURCES = \
	video_output/libplacebo/interop/nvdec.c \
	hw/nvdec/nvdec_fmt.h \
	hw/nvdec/nvdec_priv.h
libplacebo_nvdec_plugin_la_CFLAGS = $(AM_CFLAGS) $(LIBPLACEBO_CFLAGS) \
				  $(VULKAN_CFLAGS) $(FFNVCODEC_CFLAGS)
libplacebo_nvdec_plugin_la_LIBADD = $(LIBPLACEBO_LIBS) $(LIBDL) \
				  libplacebo_vkinterop.la
libplacebo_nvdec_plugin_la_LDFLAGS = $(AM_LDFLAGS) $(vout_RPATH)

if HAVE_VULKAN
if HAVE_NVDEC
vout_PLUGINS += libplacebo_nvdec_plugin.la
endif
endif

### VA-API interop
libplacebo_vaapi_plugin_la_SOURCES = \
	video_output/libplacebo/interop/vaapi.c \
	hw/vaapi/vlc_vaapi.c \
	hw/vaapi/vlc_vaapi.h
libplacebo_vaapi_plugin_la_CFLAGS = $(AM_CFLAGS) $(LIBPLACEBO_CFLAGS) \
				  $(VULKAN_CFLAGS) $(LIBVA_CFLAGS)
libplacebo_vaapi_plugin_la_LIBADD = $(LIBPLACEBO_LIBS) $(LIBVA_LIBS) \
				  libplacebo_vkinterop.la
libplacebo_vaapi_plugin_la_LDFLAGS = $(AM_LDFLAGS) $(vout_RPATH)

if HAVE_VULKAN
if HAVE_VAAPI
vout_PLUGINS += libplacebo_vaapi_plugin.la
endif
endif

### Software interop
libplacebo_sw_plugin_la_SOURCES = $(LIBPLACEBO_COMMONSOURCES) \
				  video_output/libplacebo/interop/sw.c
libplacebo_sw_plugin_la_CFLAGS = $(AM_CFLAGS) $(LIBPLACEBO_CFLAGS) $(VULKAN_CFLAGS)
libplacebo_sw_plugin_la_LIBADD = $(LIBPLACEBO_LIBS) libplacebo_utils.la \
				  libplacebo_vkinterop.la
libplacebo_sw_plugin_la_LDFLAGS = $(AM_LDFLAGS) $(vout_RPATH)

if HAVE_VULKAN
vout_PLUGINS += libplacebo_sw_plugin.la
endif

### Vulkan ###
libplacebo_vk_plugin_la_SOURCES = $(LIBPLACEBO_COMMONSOURCES) \
				  video_output/vulkan/platform.h \
				  video_output/vulkan/platform.c \
				  video_output/libplacebo/instance_vulkan.h \
				  video_output/libplacebo/instance_vulkan.c
libplacebo_vk_plugin_la_CFLAGS = $(AM_CFLAGS) $(LIBPLACEBO_CFLAGS) $(VULKAN_CFLAGS)
libplacebo_vk_plugin_la_LIBADD = $(LIBPLACEBO_LIBS) $(VULKAN_LIBS)
libplacebo_vk_plugin_la_LDFLAGS = $(AM_LDFLAGS) $(vout_RPATH)

if HAVE_VULKAN
vout_PLUGINS += $(LTLIBplacebo_vk)
EXTRA_LTLIBRARIES += libplacebo_vk_plugin.la
endif

### OpenGL ###
libplacebo_gl_plugin_la_SOURCES = $(LIBPLACEBO_COMMONSOURCES) \
				  video_output/libplacebo/instance_opengl.c
libplacebo_gl_plugin_la_CFLAGS = $(AM_CFLAGS) $(LIBPLACEBO_CFLAGS)
libplacebo_gl_plugin_la_LIBADD = $(LIBPLACEBO_LIBS) libvlc_opengl.la
libplacebo_gl_plugin_la_LDFLAGS = $(AM_LDFLAGS) $(vout_RPATH)

libplacebo_gles2_plugin_la_SOURCES = $(LIBPLACEBO_COMMONSOURCES) \
				     video_output/libplacebo/instance_opengl.c
libplacebo_gles2_plugin_la_CFLAGS = $(AM_CFLAGS) $(LIBPLACEBO_CFLAGS) -DUSE_OPENGL_ES2
libplacebo_gles2_plugin_la_LIBADD = $(LIBPLACEBO_LIBS) libvlc_opengles.la
libplacebo_gles2_plugin_la_LDFLAGS = $(AM_LDFLAGS) $(vout_RPATH)

if HAVE_GL
vout_PLUGINS += $(LTLIBplacebo_gl)
EXTRA_LTLIBRARIES += libplacebo_gl_plugin.la
endif

if HAVE_GLES2
vout_PLUGINS += $(LTLIBplacebo_gles2)
EXTRA_LTLIBRARIES += libplacebo_gles2_plugin.la
endif
