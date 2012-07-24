ACLOCAL_AMFLAGS = -I m4

SUBDIRS = @subdirs@ lemon src
if HAVE_ASCIIDOC
if HAVE_XMLTO
SUBDIRS += doc
endif
endif
EXTRA_DIST  = ltsminreconf
EXTRA_DIST += Doxyfile
EXTRA_DIST += contrib/bash-completion/ltsmin
EXTRA_DIST += CODING-STANDARDS

doc_DATA = AUTHORS COPYING README CODING-STANDARDS

dist_pkgdata_DATA = spinja/spinja.jar

MAINTAINERCLEANFILES = spinja/spinja.jar

if DX_COND_doc
mostlyclean-local: doxygen-clean
endif

.DELETE_ON_ERROR:

include m4/amdoxygen.m4

distcheck-hook:
# sample check whether manpages are present in generated tarball
	test -f $(top_distdir)/doc/etf.5
	test -f $(top_distdir)/doc/gcf.1

libtool: $(LIBTOOL_DEPS)
	$(SHELL) ./config.status --recheck

spinja/spinja.jar:
	test -f $@ || (cd "${top_srcdir}/spinja/src" && $(ANT))
