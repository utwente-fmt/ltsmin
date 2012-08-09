
noinst_LTLIBRARIES  = libvset.la

# libraries
libvset_la_SOURCES   = vector_set.c vector_set.h vdom_object.h
libvset_la_SOURCES  += vset_listdd.c
libvset_la_SOURCES  += vset_buddy.c
libvset_la_CPPFLAGS  = $(BUDDY_CPPFLAGS)
libvset_la_LDFLAGS   = $(BUDDY_LDFLAGS)
libvset_la_LIBADD    = $(BUDDY_LIBS)

if HAVE_SYLVAN
libvset_la_SOURCES  += vset_sylvan.c
libvset_la_CPPFLAGS += $(SYLVAN_CPPFLAGS) 
libvset_la_LDFLAGS  += $(SYLVAN_LDFLAGS) 
libvset_la_LIBADD   += $(SYLVAN_LIBS)
endif

if HAVE_LIBMCRL
libvset_la_SOURCES  += vset_atermdd.c
libvset_la_CPPFLAGS += $(MCRL_CPPFLAGS)
libvset_la_LDFLAGS  += $(MCRL_LDFLAGS)
libvset_la_LIBADD   += -lATerm
endif

if HAVE_LIBDDD
CCLD = $(CXXLD)
libvset_la_SOURCES  += vset_ddd.cpp
libvset_la_CPPFLAGS += $(DDD_CPPFLAGS)
libvset_la_LDFLAGS  += $(DDD_LDFLAGS)
libvset_la_LIBADD   += $(DDD_LIBS)
endif

libvset_la_CPPFLAGS += $(AM_CPPFLAGS) -I ${top_srcdir}/src
libvset_la_LDFLAGS  += $(AM_LDFLAGS)
libvset_la_LIBADD   += ../bignum/libbignum.la