# Spatialaudio

SPATIALAUDIO_VERSION := 0.4.0
SPATIALAUDIO_URL = $(GITHUB)/videolan/libspatialaudio/releases/download/$(SPATIALAUDIO_VERSION)/libspatialaudio-$(SPATIALAUDIO_VERSION).tar.xz

DEPS_spatialaudio = mysofa $(DEPS_mysofa)

PKGS += spatialaudio

ifeq ($(call need_pkg,"spatialaudio"),)
PKGS_FOUND += spatialaudio
endif

$(TARBALLS)/libspatialaudio-$(SPATIALAUDIO_VERSION).tar.xz:
	$(call download_pkg,$(SPATIALAUDIO_URL),spatialaudio)

.sum-spatialaudio: libspatialaudio-$(SPATIALAUDIO_VERSION).tar.xz

spatialaudio: libspatialaudio-$(SPATIALAUDIO_VERSION).tar.xz .sum-spatialaudio
	$(UNPACK)
	$(APPLY) $(SRC)/spatialaudio/remove-incorrect-configh-install.patch
	$(APPLY) $(SRC)/spatialaudio/add-test-option.patch
	$(MOVE)

SPATIALAUDIO_CONF := -DHAVE_MIT_HRTF=OFF

.spatialaudio: spatialaudio toolchain.cmake
	$(RM) -Rf $(PREFIX)/include/spatialaudio
	$(CMAKECLEAN)
	$(HOSTVARS_CMAKE) $(CMAKE) $(SPATIALAUDIO_CONF)
	+$(CMAKEBUILD)
	$(CMAKEINSTALL)
	touch $@
