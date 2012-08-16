noinst_LTLIBRARIES  = libltsmin.la

# libltsmin
libltsmin_la_SOURCES  =
libltsmin_la_SOURCES += ltsmin-grammar.lemon
libltsmin_la_SOURCES += etf-internal.h
libltsmin_la_SOURCES += etf-objects.c etf-objects.h
libltsmin_la_SOURCES += etf-parser.c
libltsmin_la_SOURCES += etf-util.c etf-util.h
libltsmin_la_SOURCES += ltl2ba-lex.c
libltsmin_la_SOURCES += lts-type.c lts-type.h
libltsmin_la_SOURCES += ltsmin-buchi.h
libltsmin_la_SOURCES += ltsmin-parse-env.h
libltsmin_la_SOURCES += ltsmin-syntax.c ltsmin-syntax.h
libltsmin_la_SOURCES += ltsmin-tl.c ltsmin-tl.h
libltsmin_la_SOURCES += ltsmin-grammar.c ltsmin-grammar.h
libltsmin_la_SOURCES += ltsmin-lexer.c ltsmin-lexer.h

libltsmin_la_LIBADD   = $(top_builddir)/ltl2ba/libltl2ba.la
libltsmin_la_CPPFLAGS = -DNXT
libltsmin_la_CPPFLAGS+= -I${top_srcdir}/ltl2ba $(AM_CPPFLAGS)

# lemon parser
EXTRA_DIST  = ltsmin-lexer.c ltsmin-lexer.h

BUILT_SOURCES  = ltsmin-grammar.c ltsmin-grammar.h
BUILT_SOURCES += ltsmin-lexer.c ltsmin-lexer.h

CLEANFILES =
CLEANFILES += ltsmin-grammar.c ltsmin-grammar.h
CLEANFILES += ltsmin-lexer.c ltsmin-lexer.h
CLEANFILES += ltsmin-grammar.out
MAINTAINERCLEANFILES =
MAINTAINERCLEANFILES += ltsmin-tokens.c ltsmin-tokens.h
LEMON_FLAGS = -s

%.c %.h: %.lemon lempar.c
	cd $(top_builddir)/lemon && $(MAKE) $(AM_MAKEFLAGS) lemon$(EXEEXT)
	$(top_builddir)/lemon/lemon$(EXEEXT) $(LEMON_FLAGS) $<
	@if test -f $@; then touch $@; fi

%.c %.h: %.l
	$(am__skiplex) $(LEX) $<
