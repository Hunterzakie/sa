# ggml

GGML_VERSION := 0.15.3
GGML_URL := $(GITHUB)/ggml-org/ggml/archive/refs/tags/v$(GGML_VERSION).tar.gz

# ggml has no VLC module of its own: it is only built when whisper, llama
# or sam3 pull it in through DEPS_*.

ifeq ($(call need_pkg,"ggml"),)
PKGS_FOUND += ggml
endif

ifndef HAVE_DARWIN_OS
DEPS_ggml = vulkan-loader $(DEPS_vulkan-loader) glslc $(DEPS_glslc) spirv-headers $(DEPS_spirv-headers)
endif

$(TARBALLS)/ggml-$(GGML_VERSION).tar.gz:
	$(call download_pkg,$(GGML_URL),ggml)

.sum-ggml: ggml-$(GGML_VERSION).tar.gz

ggml: ggml-$(GGML_VERSION).tar.gz .sum-ggml
	$(UNPACK)
	$(APPLY) $(SRC)/ggml/0001-Use-LoadPackagedLibrary-in-UWP-builds.patch
	$(APPLY) $(SRC)/ggml/0002-ggml-fix-mingw-cross-prefix.patch
	$(APPLY) $(SRC)/ggml/0003-ggml-increase-GGML_MAX_NAME-from-64-to-96.patch
	$(APPLY) $(SRC)/ggml/0004-ggml-pc-list-the-backend-libraries.patch
	$(APPLY) $(SRC)/ggml/0005-ggml-export-the-GGML_USE_-backend-macros.patch
	$(MOVE)

GGML_CONF := \
	-DGGML_OPENMP=OFF \
	-DGGML_NATIVE=OFF \
	-DGGML_LLAMAFILE=OFF \
	-DGGML_CCACHE=OFF \
	-DGGML_RPC=OFF \
	-DGGML_BUILD_TESTS=OFF \
	-DGGML_BUILD_EXAMPLES=OFF

ifdef HAVE_DARWIN_OS
# Metal requires macOS 14.4.
GGML_CONF += -DCMAKE_OSX_DEPLOYMENT_TARGET=14.4
GGML_CONF += -DGGML_METAL=ON
GGML_CONF += -DGGML_METAL_EMBED_LIBRARY=ON
GGML_CONF += -DGGML_ACCELERATE=ON
else
GGML_CONF += -DGGML_VULKAN=ON
endif

.ggml: ggml toolchain.cmake
	$(CMAKECLEAN)
	$(HOSTVARS_CMAKE) $(CMAKE) $(GGML_CONF)
	+$(CMAKEBUILD)
	$(CMAKEINSTALL)
	touch $@
