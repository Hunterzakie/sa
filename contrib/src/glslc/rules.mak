# shaderc / glslc - native Vulkan shader compiler tool

SHADERC_VERSION := 2024.3
# Dependency commits from shaderc v2024.3 DEPS file:
SHADERC_GLSLANG_COMMIT := 467ce01c71e38cf01814c48987a5c0dadd914df4
SHADERC_SPIRV_TOOLS_COMMIT := 01c8438ee4ac52c248119b7e03e0b021f853b51a
SHADERC_SPIRV_HEADERS_COMMIT := 2a9b6f951c7d6b04b6c21fe1bf3f475b68b84801

SHADERC_URL := $(GITHUB)/google/shaderc/archive/refs/tags/v$(SHADERC_VERSION).tar.gz
SHADERC_GLSLANG_URL := $(GITHUB)/KhronosGroup/glslang/archive/$(SHADERC_GLSLANG_COMMIT).tar.gz
SHADERC_SPIRV_TOOLS_URL := $(GITHUB)/KhronosGroup/SPIRV-Tools/archive/$(SHADERC_SPIRV_TOOLS_COMMIT).tar.gz
SHADERC_SPIRV_HEADERS_URL := $(GITHUB)/KhronosGroup/SPIRV-Headers/archive/$(SHADERC_SPIRV_HEADERS_COMMIT).tar.gz

# Pure build tool: only built natively for the build machine
PKGS_TOOLS += glslc
PKGS.tools += glslc
PKGS.tools.glslc.config-tool = glslc
PKGS.tools.glslc.path = $(BUILDBINDIR)/glslc

# Skip build if glslc is already available in PATH
ifneq ($(shell PATH="$(SYSTEM_PATH)" glslc --version 2>/dev/null),)
PKGS_FOUND += glslc
endif

# --- Download targets ---

$(TARBALLS)/shaderc-$(SHADERC_VERSION).tar.gz:
	$(call download_pkg,$(SHADERC_URL),glslc)

$(TARBALLS)/glslang-shaderc-$(SHADERC_GLSLANG_COMMIT).tar.gz:
	$(call download_pkg,$(SHADERC_GLSLANG_URL),glslc)

$(TARBALLS)/spirv-tools-$(SHADERC_SPIRV_TOOLS_COMMIT).tar.gz:
	$(call download_pkg,$(SHADERC_SPIRV_TOOLS_URL),glslc)

$(TARBALLS)/spirv-headers-$(SHADERC_SPIRV_HEADERS_COMMIT).tar.gz:
	$(call download_pkg,$(SHADERC_SPIRV_HEADERS_URL),glslc)

# .sum-glslc covers all 4 archives
.sum-glslc: shaderc-$(SHADERC_VERSION).tar.gz \
            glslang-shaderc-$(SHADERC_GLSLANG_COMMIT).tar.gz \
            spirv-tools-$(SHADERC_SPIRV_TOOLS_COMMIT).tar.gz \
            spirv-headers-$(SHADERC_SPIRV_HEADERS_COMMIT).tar.gz

# --- Extract + arrange step ---
# shaderc expects its deps as source trees under third_party/
# Layout:
#   third_party/glslang/
#   third_party/spirv-tools/
#   third_party/spirv-tools/external/spirv-headers/

glslc: UNPACK_DIR=shaderc-$(SHADERC_VERSION)
glslc: shaderc-$(SHADERC_VERSION).tar.gz \
       glslang-shaderc-$(SHADERC_GLSLANG_COMMIT).tar.gz \
       spirv-tools-$(SHADERC_SPIRV_TOOLS_COMMIT).tar.gz \
       spirv-headers-$(SHADERC_SPIRV_HEADERS_COMMIT).tar.gz \
       .sum-glslc
	$(RM) -Rf $@ $(UNPACK_DIR)
	mkdir -p $(UNPACK_DIR)
	tar $(TAR_VERBOSE)xzfo $(TARBALLS)/shaderc-$(SHADERC_VERSION).tar.gz \
		-C $(UNPACK_DIR) --strip-components=1
	mkdir -p $(UNPACK_DIR)/third_party/glslang
	tar $(TAR_VERBOSE)xzfo $(TARBALLS)/glslang-shaderc-$(SHADERC_GLSLANG_COMMIT).tar.gz \
		-C $(UNPACK_DIR)/third_party/glslang --strip-components=1
	mkdir -p $(UNPACK_DIR)/third_party/spirv-tools
	tar $(TAR_VERBOSE)xzfo $(TARBALLS)/spirv-tools-$(SHADERC_SPIRV_TOOLS_COMMIT).tar.gz \
		-C $(UNPACK_DIR)/third_party/spirv-tools --strip-components=1
	mkdir -p $(UNPACK_DIR)/third_party/spirv-tools/external/spirv-headers
	tar $(TAR_VERBOSE)xzfo $(TARBALLS)/spirv-headers-$(SHADERC_SPIRV_HEADERS_COMMIT).tar.gz \
		-C $(UNPACK_DIR)/third_party/spirv-tools/external/spirv-headers --strip-components=1
	$(MOVE)

# --- Native CMake build ---

GLSLC_CONF := \
	-DSHADERC_SKIP_TESTS=ON \
	-DSHADERC_SKIP_EXAMPLES=ON \
	-DSHADERC_SKIP_COPYRIGHT_CHECK=ON \
	-DSHADERC_ENABLE_SHARED_CRT=OFF \
	-DBUILD_SHARED_LIBS=OFF \
	-DSPIRV_SKIP_TESTS=ON \
	-DSPIRV_SKIP_EXECUTABLES=ON \
	-DENABLE_GLSLANG_BINARIES=OFF \
	-DENABLE_CTEST=OFF

.glslc: BUILD_DIR=$</vlc_native
.glslc: glslc
	$(CMAKECLEAN)
	$(BUILDVARS) $(CMAKE_NATIVE) $(GLSLC_CONF)
	+$(CMAKEBUILD)
	mkdir -p -- $(BUILDBINDIR)
	install -m 0755 -- $(BUILD_DIR)/glslc/glslc $(BUILDBINDIR)/glslc
	touch $@
