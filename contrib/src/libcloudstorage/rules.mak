LIBCLOUDSTORAGE_VERSION := master
LIBCLOUDSTORAGE_URL := https://code.videolan.org/videolan/libcloudstorage/-/archive/$(LIBCLOUDSTORAGE_VERSION)/libcloudstorage-$(LIBCLOUDSTORAGE_VERSION).tar.gz

PKGS += libcloudstorage

ifeq ($(call need_pkg,"libcloudstorage"),)
PKGS_FOUND += libcloudstorage
endif

DEPS_libcloudstorage =

LIBCLOUDSTORAGE_CONF =

$(TARBALLS)/libcloudstorage-$(LIBCLOUDSTORAGE_VERSION).tar.gz:
	$(call download_pkg,$(LIBCLOUDSTORAGE_URL),libcloudstorage)

.sum-libcloudstorage: $(TARBALLS)/libcloudstorage-$(LIBCLOUDSTORAGE_VERSION).tar.gz
	@touch $@

libcloudstorage: $(TARBALLS)/libcloudstorage-$(LIBCLOUDSTORAGE_VERSION).tar.gz .sum-libcloudstorage
	$(UNPACK)
	# Autotools project; no upstream CMake
	$(MOVE)

.libcloudstorage: libcloudstorage
	$(RECONF)
	$(MAKEBUILDDIR)
	$(MAKECONFIGURE) $(LIBCLOUDSTORAGE_CONF)
	+$(MAKEBUILD)
	+$(MAKEBUILD) install
	touch $@
