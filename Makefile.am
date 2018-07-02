ACLOCAL_AMFLAGS = -I m4

SUBDIRS = @subdirs@ lib lemon examples src testsuite
if DX_COND_doc
if HAVE_ASCIIDOC
if HAVE_XMLTO
SUBDIRS += doc
endif
endif
endif

EXTRA_DIST  = ltsminreconf
EXTRA_DIST += Doxyfile
EXTRA_DIST += contrib/bash-completion/ltsmin
EXTRA_DIST += CODING-STANDARDS
EXTRA_DIST += README.md
EXTRA_DIST += m4/gnulib-cache.m4

doc_DATA = AUTHORS COPYING README.md CODING-STANDARDS

if HAVE_SPINS_COMPILER
dist_pkgdata_DATA = spins/spins.jar
endif

MAINTAINERCLEANFILES = spins/spins.jar

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

if HAVE_SPINS_COMPILER
spins/spins.jar:
	test -f $@ || (cd "${top_srcdir}/spins/src" && $(ANT))
endif

.NOTPARALLEL:
.PHONY: $(EXTRA_CHECKS) check-long
$(EXTRA_CHECKS): check
	cd testsuite && $(MAKE) $(AM_MAKEFLAGS) $@
check-long:
	cd testsuite && $(MAKE) $(AM_MAKEFLAGS) $@

