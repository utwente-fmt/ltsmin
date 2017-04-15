.NOTPARALLEL:

noinst_LTLIBRARIES  = libandl.la

libandl_la_YFLAGS   = -d

BUILT_SOURCES  = andl-lexer.c andl-lexer.h
BUILT_SOURCES += libandl_la-andl-parser.c libandl_la-andl-parser.h

CLEANFILES  = andl-lexer.c andl-lexer.h
CLEANFILES += libandl_la-andl-parser.c libandl_la-andl-parser.h

# libprob
libandl_la_SOURCES  = andl-lexer.l andl-parser.y
libandl_la_LDFLAGS  = $(AM_LDFLAGS)
libandl_la_CPPFLAGS = $(AM_CPPFLAGS)
