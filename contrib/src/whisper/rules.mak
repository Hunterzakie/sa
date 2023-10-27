WHISPER_VERSION := 1.9.1
WHISPER_URL := $(GITHUB)/ggml-org/whisper.cpp/archive/refs/tags/v$(WHISPER_VERSION).tar.gz

# Whisper need std::thread that is provided by llvm
ifneq ($(findstring clang, $(shell $(CC) --version 2>/dev/null)),)
HAVE_CLANG := 1
endif

ifndef HAVE_WIN32
  ifeq ($(ARCH),aarch64)
    PKGS += whisper
  else ifeq ($(ARCH),x86_64)
    PKGS += whisper
  endif
else
  ifdef HAVE_CLANG
    PKGS += whisper
  endif
endif

ifdef HAVE_LINUX
  ifeq ($(ARCH),i386)
    PKGS += whisper
  endif
endif

ifeq ($(call need_pkg,"whisper"),)
  PKGS_FOUND += whisper
endif

DEPS_whisper = ggml $(DEPS_ggml)

$(TARBALLS)/whisper.cpp-$(WHISPER_VERSION).tar.gz:
	$(call download_pkg,$(WHISPER_URL),whisper.cpp)

.sum-whisper: whisper.cpp-$(WHISPER_VERSION).tar.gz

whisper: whisper.cpp-$(WHISPER_VERSION).tar.gz .sum-whisper
	$(UNPACK)
	$(APPLY) $(SRC)/whisper/0001-cmake-fix-linking-problems.patch
	$(APPLY) $(SRC)/whisper/0002-fix-whisper-pc-vulkan-pthread.patch
	$(APPLY) $(SRC)/whisper/0003-whisper-add-whisper_vad_set_context-to-set-vad_conte.patch
	$(MOVE)

WHISPER_CONF := -DWHISPER_USE_SYSTEM_GGML=ON
WHISPER_CONF += -DWHISPER_BUILD_EXAMPLES=OFF
WHISPER_CONF += -DWHISPER_BUILD_TESTS=OFF
WHISPER_CONF += -DWHISPER_BUILD_SERVER=OFF

# Activate COREML only when is aarch64
ifdef HAVE_DARWIN_OS
  # CoreML requires macOS 14.4.
  WHISPER_CONF += -DCMAKE_OSX_DEPLOYMENT_TARGET=14.4
  WHISPER_CONF += -DWHISPER_NO_ACCELERATE=OFF
  ifeq ($(ARCH),aarch64)
    WHISPER_CONF += -DWHISPER_COREML=ON
  endif
endif

.whisper: whisper toolchain.cmake
	$(CMAKECLEAN)
	$(HOSTVARS) $(CMAKE) $(WHISPER_CONF)
	+$(CMAKEBUILD)
	$(CMAKEINSTALL)
	touch $@
