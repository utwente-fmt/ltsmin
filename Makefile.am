ACLOCAL_AMFLAGS = -I m4

SUBDIRS = buddy nips-vm src doc
EXTRA_DIST = ltsminreconf Doxyfile

doc_DATA = AUTHORS COPYING README

if DX_COND_doc
mostlyclean-local: doxygen-clean
endif

.DELETE_ON_ERROR:

include m4/amdoxygen.m4

distcheck-hook:
# sample check whether manpages are present in generated tarball
	test -f $(top_distdir)/doc/etf.5
	test -f $(top_distdir)/doc/gcf.1
