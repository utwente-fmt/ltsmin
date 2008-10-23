ACLOCAL_AMFLAGS = -I m4 --install

SUBDIRS = src doc
EXTRA_DIST = ltsminreconf Doxyfile

doc_DATA = AUTHORS ChangeLog COPYING NEWS README

MOSTLYCLEANFILES = $(DX_CLEANFILES)

.DELETE_ON_ERROR:

include m4/amdoxygen.m4
