# tinyxml2

TINYXML2_VERSION := 9.0.0
TINYXML2_URL := https://github.com/leethomason/tinyxml2/archive/refs/tags/$(TINYXML2_VERSION).tar.gz

ifeq ($(call need_pkg,"tinyxml2 >= 9.0.0"),)
PKGS_FOUND += tinyxml2
endif

$(TARBALLS)/tinyxml2-$(TINYXML2_VERSION).tar.gz:
	$(call download_pkg,$(TINYXML2_URL),tinyxml2)

.sum-tinyxml2: tinyxml2-$(TINYXML2_VERSION).tar.gz

tinyxml2: tinyxml2-$(TINYXML2_VERSION).tar.gz .sum-tinyxml2
	$(UNPACK)
	$(MOVE)

.tinyxml2: tinyxml2 toolchain.cmake
	$(CMAKECLEAN)
	$(HOSTVARS_CMAKE) $(CMAKE) -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DTINYXML2_BUILD_TESTING=OFF
	+$(CMAKEBUILD)
	$(CMAKEINSTALL)
	touch $@
