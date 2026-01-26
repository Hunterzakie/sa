# llama.cpp version - using a stable release tag
LLAMA_VERSION := b9897
LLAMA_URL := $(GITHUB)/ggml-org/llama.cpp/archive/refs/tags/$(LLAMA_VERSION).tar.gz

# llama.cpp needs std::thread that is provided by llvm
ifneq ($(findstring clang, $(shell $(CC) --version 2>/dev/null)),)
HAVE_CLANG := 1
endif

ifndef HAVE_WIN32
  ifeq ($(ARCH),aarch64)
    PKGS += llama
  else ifeq ($(ARCH),x86_64)
    PKGS += llama
  endif
else
  ifdef HAVE_CLANG
    PKGS += llama
  endif
endif

ifdef HAVE_LINUX
  ifeq ($(ARCH),i386)
    PKGS += llama
  endif
endif

ifeq ($(call need_pkg,"llama"),)
  PKGS_FOUND += llama
endif

DEPS_llama = ggml $(DEPS_ggml)

$(TARBALLS)/llama.cpp-$(LLAMA_VERSION).tar.gz:
	$(call download_pkg,$(LLAMA_URL),llama.cpp)

.sum-llama: llama.cpp-$(LLAMA_VERSION).tar.gz

llama: llama.cpp-$(LLAMA_VERSION).tar.gz .sum-llama
	$(UNPACK)
	$(APPLY) $(SRC)/llama/0001-fix-llama-pc-ggml-deps.patch
	$(MOVE)

LLAMA_CONF := -DLLAMA_USE_SYSTEM_GGML=ON
LLAMA_CONF += -DLLAMA_BUILD_EXAMPLES=OFF
LLAMA_CONF += -DLLAMA_BUILD_TESTS=OFF
LLAMA_CONF += -DLLAMA_BUILD_SERVER=OFF
LLAMA_CONF += -DLLAMA_BUILD_TOOLS=OFF
LLAMA_CONF += -DLLAMA_BUILD_COMMON=OFF
LLAMA_CONF += -DLLAMA_BUILD_APP=OFF
LLAMA_CONF += -DLLAMA_CURL=OFF
LLAMA_CONF += -DBUILD_SHARED_LIBS=OFF

ifdef HAVE_DARWIN_OS
# ggml is built against the Metal API, which requires macOS 14.4.
LLAMA_CONF += -DCMAKE_OSX_DEPLOYMENT_TARGET=14.4
endif

.llama: llama toolchain.cmake
	$(CMAKECLEAN)
	$(HOSTVARS) $(CMAKE) $(LLAMA_CONF)
	+$(CMAKEBUILD)
	$(CMAKEINSTALL)
	touch $@
