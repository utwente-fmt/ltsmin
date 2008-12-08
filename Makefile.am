ACLOCAL_AMFLAGS = -I m4 --install

SUBDIRS = nips-vm src doc
EXTRA_DIST = ltsminreconf Doxyfile

doc_DATA = AUTHORS ChangeLog COPYING NEWS README

if DX_COND_doc
mostlyclean-local: doxygen-clean
endif

.DELETE_ON_ERROR:

include m4/amdoxygen.m4
