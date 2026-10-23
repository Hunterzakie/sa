# SPIRV-Headers

SPIRV_HEADERS_VERSION := vulkan-sdk-1.4.350.1
SPIRV_HEADERS_URL := $(GITHUB)/KhronosGroup/SPIRV-Headers/archive/refs/tags/$(SPIRV_HEADERS_VERSION).tar.gz

PKGS += spirv-headers

DEPS_spirv-headers =

$(TARBALLS)/SPIRV-Headers-$(SPIRV_HEADERS_VERSION).tar.gz:
	$(call download_pkg,$(SPIRV_HEADERS_URL),spirv-headers)

.sum-spirv-headers: SPIRV-Headers-$(SPIRV_HEADERS_VERSION).tar.gz

spirv-headers: SPIRV-Headers-$(SPIRV_HEADERS_VERSION).tar.gz .sum-spirv-headers
	$(UNPACK)
	$(MOVE)

.spirv-headers: spirv-headers toolchain.cmake
	$(CMAKECLEAN)
	$(HOSTVARS_CMAKE) $(CMAKE)
	$(CMAKEINSTALL)
	touch $@
