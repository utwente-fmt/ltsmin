noinst_LTLIBRARIES  = libltsmin.la
noinst_LTLIBRARIES += libmucalc.la

pkginclude_HEADERS = lts-type.h ltsmin-standard.h

# for gnulib
AM_CPPFLAGS += -I$(top_srcdir)/lib -I$(top_builddir)/lib

# libltsmin
libltsmin_la_SOURCES  =
libltsmin_la_SOURCES += ltsmin-grammar.lemon
libltsmin_la_SOURCES += etf-internal.h
libltsmin_la_SOURCES += etf-objects.c etf-objects.h
libltsmin_la_SOURCES += etf-parser.c
libltsmin_la_SOURCES += etf-util.c etf-util.h
libltsmin_la_SOURCES += ltl2ba-lex.c ltl2ba-lex.h
libltsmin_la_SOURCES += ltl2ba-lex-helper.c ltl2ba-lex-helper.h

libltsmin_la_SOURCES += lts-type.c lts-type.h
libltsmin_la_SOURCES += ltsmin-buchi.h
libltsmin_la_SOURCES += ltsmin-parse-env.h
libltsmin_la_SOURCES += ltsmin-syntax.c ltsmin-syntax.h
libltsmin_la_SOURCES += ltsmin-tl.c ltsmin-tl.h
libltsmin_la_SOURCES += ltsmin-grammar.c ltsmin-grammar.h
libltsmin_la_SOURCES += ltsmin-lexer.c ltsmin-lexer.h
libltsmin_la_SOURCES += ltsmin-lexer.l
libltsmin_la_SOURCES += ltsmin-standard.h
libltsmin_la_SOURCES += ltsmin-type-system.c ltsmin-type-system.h

libltsmin_la_LIBADD   = $(top_builddir)/ltl2ba/libltl2ba.la
libltsmin_la_LIBADD  += ../dm/libdm.la
libltsmin_la_LIBADD  += $(top_builddir)/lib/libgnu.la

libltsmin_la_CXXFLAGS = $(AM_CXXFLAGS)

libltsmin_la_CPPFLAGS = -DNXT
libltsmin_la_CPPFLAGS+= -I${top_srcdir}/ltl2ba $(AM_CPPFLAGS) 
libltsmin_la_CPPFLAGS+= -I${top_srcdir}

# spot
if HAVE_SPOT
libltsmin_la_SOURCES    += ltl2spot.cpp ltl2spot.h
libltsmin_la_LIBADD     += $(SPOT_LDFLAGS) $(SPOT_LIBS)
libltsmin_la_CPPFLAGS   += $(SPOT_CPPFLAGS)
libltsmin_la_CXXFLAGS   += $(SPOT_CFLAGS)
endif

# mu-calculus parser
libmucalc_la_SOURCES  =
libmucalc_la_SOURCES += mucalc-grammar.lemon
libmucalc_la_SOURCES += mucalc-parse-env.h
libmucalc_la_SOURCES += mucalc-syntax.c mucalc-syntax.h
libmucalc_la_SOURCES += mucalc-lexer.c mucalc-lexer.h
libmucalc_la_SOURCES += mucalc-lexer.l
libmucalc_la_SOURCES += mucalc-grammar.c mucalc-grammar.h

# lemon parser
EXTRA_DIST  = ltsmin-lexer.c ltsmin-lexer.h
EXTRA_DIST += mucalc-lexer.c mucalc-lexer.h

BUILT_SOURCES  = ltsmin-grammar.c ltsmin-grammar.h
BUILT_SOURCES += ltsmin-lexer.c ltsmin-lexer.h
BUILT_SOURCES += mucalc-grammar.c mucalc-grammar.h
BUILT_SOURCES += mucalc-lexer.c mucalc-lexer.h

CLEANFILES =
CLEANFILES += ltsmin-grammar.c ltsmin-grammar.h
CLEANFILES += ltsmin-lexer.c ltsmin-lexer.h
CLEANFILES += ltsmin-grammar.out
CLEANFILES += mucalc-grammar.c mucalc-grammar.h
CLEANFILES += mucalc-lexer.c mucalc-lexer.h
CLEANFILES += mucalc-grammar.out

MAINTAINERCLEANFILES =
MAINTAINERCLEANFILES += ltsmin-tokens.c ltsmin-tokens.h
LEMON_FLAGS = -s

.NOTPARALLEL:

%.c %.h: %.lemon lempar.c
	$(MAKE) $(MAKE_AMFLAGS) -C $(top_builddir)/lemon
	$(top_builddir)/lemon/lemon$(BUILD_EXEEXT) $(LEMON_FLAGS) $<
	@if test -f $@; then touch $@; fi

%.c %.h: %.l
	$(am__skiplex) $(LEX) $<
