noinst_LTLIBRARIES  = libvset.la

# Warning overwriting user variable! (Avoid DDD warnings)
CXXFLAGS = @CXXFLAGS@ -Wno-unused-parameter -Wno-overloaded-virtual

# libvset
libvset_la_LIBADD    = ../bignum/libbignum.la
libvset_la_CFLAGS    = $(AM_CFLAGS)
libvset_la_LDFLAGS   = $(AM_LDFLAGS)
libvset_la_CPPFLAGS  = $(AM_CPPFLAGS)

libvset_la_LIBADD   += ../util-lib/libutil.la
libvset_la_LIBADD   += ../hre-io/libhre-io.la

libvset_la_SOURCES   = vector_set.c vector_set.h vdom_object.h
libvset_la_SOURCES  += vset_listdd.c
libvset_la_SOURCES  += vset_listdd64.c

if HAVE_BUDDY
libvset_la_SOURCES  += vset_buddy.c
libvset_la_CFLAGS   += $(AM_CFLAGS) $(BUDDY_CFLAGS)
libvset_la_LIBADD   += $(BUDDY_LIBS)
endif

if HAVE_SYLVAN
libvset_la_SOURCES  += vset_sylvan.c
libvset_la_SOURCES  += vset_lddmc.c
libvset_la_CFLAGS   += $(SYLVAN_CFLAGS)
libvset_la_LIBADD   += $(SYLVAN_LIBS)
endif

if HAVE_LIBMCRL
libvset_la_SOURCES  += vset_atermdd.c
libvset_la_CPPFLAGS += $(MCRL_CPPFLAGS)
libvset_la_LDFLAGS  += $(MCRL_LDFLAGS)
libvset_la_LIBADD   += -lATerm
endif

if HAVE_LIBDDD
libvset_la_SOURCES  += vset_ddd.cpp
nodist_EXTRA_libvset_la_SOURCES = automake-force-linker.cxx
libvset_la_CPPFLAGS += $(DDD_CPPFLAGS)
libvset_la_LDFLAGS  += $(DDD_LDFLAGS)
libvset_la_LIBADD   += $(DDD_LIBS)
endif

