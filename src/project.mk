INCLUDE_ROOT = .
ENABLE_INSTALL_STATIC_LIBS = 1
ENABLE_INSTALL_DEBUG_LIBS  = 1
ENABLE_INSTALL_DEBUG_PROGS = 1

# Construct fat binaries on Darwin when using Clang
ifneq ($(REALM_ENABLE_FAT_BINARIES),)
  ifeq ($(OS),Darwin)
    ifeq ($(COMPILER_IS),clang)
      CFLAGS_ARCH += -arch i386 -arch x86_64
    endif
  endif
endif

ifeq ($(OS),Darwin)
  CFLAGS_ARCH += -mmacosx-version-min=10.8 -stdlib=libc++
  VALGRIND_FLAGS += --dsymutil=yes --suppressions=$(GENERIC_MK_DIR)/../test/corefoundation-yosemite.suppress
endif

# FIXME: '-fno-elide-constructors' currently causes Realm to fail
#CFLAGS_DEBUG += -fno-elide-constructors
CFLAGS_PTHREADS += -pthread
CFLAGS_GENERAL += -Wextra -pedantic
CFLAGS_CXX = -std=c++11

# Avoid a warning from Clang when linking on OS X. By default,
# `LDFLAGS_PTHREADS` inherits its value from `CFLAGS_PTHREADS`, so we
# have to override that with an empty value.
ifeq ($(OS),Darwin)
  ifeq ($(LD_IS),clang)
    LDFLAGS_PTHREADS = $(EMPTY)
  endif
endif


# CoreFoundation is required for logging
ifeq ($(OS),Darwin)
  PROJECT_LDFLAGS += -framework CoreFoundation
endif

# Android logging
ifeq ($(REALM_ANDROID),)
  PROJECTS_LDFLAGS += -llog
endif

# Note: While CFLAGS (those specified above) can be overwritten by
# setting the CFLAGS variable on the command line, PROJECT_CFLAGS are
# retained.

ifneq ($(REALM_HAVE_CONFIG),)
  PROJECT_CFLAGS += -DREALM_HAVE_CONFIG
endif

PROJECT_CFLAGS_DEBUG = -DREALM_DEBUG
PROJECT_CFLAGS_COVER = -DREALM_DEBUG -DREALM_COVER

# Load dynamic configuration
ifneq ($(REALM_HAVE_CONFIG),)
  CONFIG_MK = $(GENERIC_MK_DIR)/config.mk
  DEP_MAKEFILES += $(CONFIG_MK)
  include $(CONFIG_MK)
  prefix      = $(INSTALL_PREFIX)
  exec_prefix = $(INSTALL_EXEC_PREFIX)
  includedir  = $(INSTALL_INCLUDEDIR)
  bindir      = $(INSTALL_BINDIR)
  libdir      = $(INSTALL_LIBDIR)
  libexecdir  = $(INSTALL_LIBEXECDIR)
  ifeq ($(ENABLE_REPLICATION),yes)
    REALM_ENABLE_REPLICATION = x
  else
    REALM_ENABLE_REPLICATION = $(EMPTY)
  endif
  ifeq ($(ENABLE_ENCRYPTION),yes)
    PROJECT_CFLAGS += -DREALM_ENABLE_ENCRYPTION
  endif
else
  ifneq ($(REALM_ENABLE_REPLICATION),)
    PROJECT_CFLAGS += -DREALM_ENABLE_REPLICATION
  endif
  ifneq ($(REALM_ENABLE_ALLOC_SET_ZERO),)
    PROJECT_CFLAGS += -DREALM_ENABLE_ALLOC_SET_ZERO
  endif
endif

ifneq ($(REALM_ANDROID),)
  PROJECT_CFLAGS += -fPIC -DPIC -fvisibility=hidden -DANDROID
  CFLAGS_OPTIM = -Os -flto -DNDEBUG
endif
