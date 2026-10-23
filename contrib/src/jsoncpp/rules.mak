# jsoncpp

JSONCPP_VERSION := 1.9.5
JSONCPP_URL := https://github.com/open-source-parsers/jsoncpp/archive/refs/tags/$(JSONCPP_VERSION).tar.gz

ifeq ($(call need_pkg,"jsoncpp >= 1.9.0"),)
PKGS_FOUND += jsoncpp
endif

$(TARBALLS)/jsoncpp-$(JSONCPP_VERSION).tar.gz:
	$(call download_pkg,$(JSONCPP_URL),jsoncpp)

.sum-jsoncpp: jsoncpp-$(JSONCPP_VERSION).tar.gz

jsoncpp: jsoncpp-$(JSONCPP_VERSION).tar.gz .sum-jsoncpp
	$(UNPACK)
	$(MOVE)

.jsoncpp: jsoncpp toolchain.cmake
	$(CMAKECLEAN)
	$(HOSTVARS_CMAKE) $(CMAKE) -DJSONCPP_WITH_TESTS=OFF -DJSONCPP_WITH_POST_BUILD_UNITTEST=OFF -DJSONCPP_WITH_PKGCONFIG_SUPPORT=ON -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON -DCMAKE_POSITION_INDEPENDENT_CODE=ON
	+$(CMAKEBUILD)
	$(CMAKEINSTALL)
	touch $@
