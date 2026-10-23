LIBCLOUDSTORAGE_VERSION := 8e68e0000763606e335ac223324a78526c14d576
LIBCLOUDSTORAGE_URL := https://code.videolan.org/videolan/libcloudstorage/-/archive/$(LIBCLOUDSTORAGE_VERSION)/libcloudstorage-$(LIBCLOUDSTORAGE_VERSION).tar.gz

PKGS += libcloudstorage
ifeq ($(call need_pkg,"libcloudstorage"),)
PKGS_FOUND += libcloudstorage
endif

DEPS_libcloudstorage = jsoncpp tinyxml2

LIBCLOUDSTORAGE_CONF := -Dcurl=disabled -Dmicrohttpd=disabled -Dfilesystem=disabled -Dexamples=false -Dfuse=disabled -Dtests=false

$(TARBALLS)/libcloudstorage-$(LIBCLOUDSTORAGE_VERSION).tar.gz:
	$(call download_pkg,$(LIBCLOUDSTORAGE_URL),libcloudstorage)

.sum-libcloudstorage: libcloudstorage-$(LIBCLOUDSTORAGE_VERSION).tar.gz

libcloudstorage: $(TARBALLS)/libcloudstorage-$(LIBCLOUDSTORAGE_VERSION).tar.gz .sum-libcloudstorage
	$(UNPACK)
	$(MOVE)

.libcloudstorage: libcloudstorage crossfile.meson
	$(MESONCLEAN)
	$(MESON) $(LIBCLOUDSTORAGE_CONF)
	+$(MESONBUILD)
	touch $@
