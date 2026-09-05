# ZSTD
ZSTD_VERSION := 1.5.7
ZSTD_URL := $(GITHUB)/facebook/zstd/releases/download/v$(ZSTD_VERSION)/zstd-$(ZSTD_VERSION).tar.gz

PKGS += zstd
ifeq ($(call need_pkg,"zstd"),)
PKGS_FOUND += zstd
endif

$(TARBALLS)/zstd-$(ZSTD_VERSION).tar.gz:
	$(call download_pkg,$(ZSTD_URL),zstd)

.sum-zstd: zstd-$(ZSTD_VERSION).tar.gz

zstd: zstd-$(ZSTD_VERSION).tar.gz .sum-zstd
	$(UNPACK)
	$(MOVE)

ZSTD_CONF = -DZSTD_BUILD_STATIC=ON \
			-DZSTD_BUILD_SHARED=OFF \
			-DZSTD_BUILD_PROGRAMS=OFF

.zstd: zstd toolchain.cmake
	$(CMAKECLEAN)
	$(HOSTVARS) $(CMAKE) -S $</build/cmake $(ZSTD_CONF)
	+$(CMAKEBUILD)
	$(CMAKEINSTALL)
	touch $@
