# LASER
LASER_VERSION := 0.2.1

LASER_URL := https://github.com/liblaser/liblaser/releases/download/v$(LASER_VERSION)/liblaser-$(LASER_VERSION).tar.bz2

ifdef BUILD_DISCS
ifdef HAVE_ANDROID
PKGS += laser
endif
endif
ifeq ($(call need_pkg,"liblaser >= 0.2.1"),)
PKGS_FOUND += laser
endif

$(TARBALLS)/liblaser-$(LASER_VERSION).tar.bz2:
	$(call download,$(LASER_URL))

.sum-laser: liblaser-$(LASER_VERSION).tar.bz2

laser: liblaser-$(LASER_VERSION).tar.bz2 .sum-laser
	$(UNPACK)
	$(MOVE)

.laser: laser crossfile.meson
	$(MESONCLEAN)
	$(MESON)
	+$(MESONBUILD)
	touch $@
