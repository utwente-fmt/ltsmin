ASCIIDOC = @ASCIIDOC@
XMLTO    = @XMLTO@
ASCIIDOC_FLAGS = -a version=$(VERSION)

doc_ASCIIDOC = ltsmin-mpi.txt ltsmin-convert.txt	\
	ltsmin.txt gcf.txt lpo2lts-mpi.txt lps2lts-mpi.txt nips2lts-mpi.txt \
	lpo2lts-grey.txt lps2lts-grey.txt nips2lts-grey.txt \
	lpo-reach.txt lps-reach.txt nips-reach.txt
EXTRA_DIST = asciidoc.conf callouts.xsl spec2lts-mpi.txt spec2lts-grey.txt \
	spec-reach.txt $(doc_ASCIIDOC)

MANPAGES = ltsmin-mpi.1 ltsmin-convert.1 gcf.1       \
        lpo-reach.1 lps-reach.1 nips-reach.1 lpo2lts-grey.1             \
        lps2lts-grey.1 nips2lts-grey.1 lpo2lts-mpi.1 lps2lts-mpi.1      \
        nips2lts-mpi.1 ltsmin.5

HTML = $(doc_ASCIIDOC:%.txt=%.html)

if HAVE_ASCIIDOC
dist_html_DATA = $(HTML)
if HAVE_XMLTO
dist_man_MANS  = $(MANPAGES)
endif
endif

MOSTLYCLEANFILES     = $(doc_ASCIIDOC:.txt=.xml)
CLEANFILES =
if HAVE_ASCIIDOC
CLEANFILES += $(HTML)
if HAVE_XMLTO
CLEANFILES += $(MANPAGES)
endif
endif
MAINTAINERCLEANFILES = $(HTML) $(MANPAGES)

html-local: $(HTML)

if HAVE_ASCIIDOC
lpo2lts-mpi.html lpo2lts-mpi.xml: spec2lts-mpi.txt
lps2lts-mpi.html lps2lts-mpi.xml: spec2lts-mpi.txt
nips2lts-mpi.html nips2lts-mpi.xml: spec2lts-mpi.txt

lpo2lts-grey.html lpo2lts-grey.xml: spec2lts-grey.txt
lps2lts-grey.html lps2lts-grey.xml: spec2lts-grey.txt
nips2lts-grey.html nips2lts-grey.xml: spec2lts-grey.txt

lpo-reach.html lpo-reach.xml: spec-reach.txt
lps-reach.html lps-reach.xml: spec-reach.txt
nips-reach.html nips-reach.xml: spec-reach.txt

.txt.html:
	$(ASCIIDOC.html)

.txt.xml:
	$(ASCIIDOC.xml)

if HAVE_XMLTO
%.1 %.2 %.3 %.4 %.5 %.6 %.7 %.8: %.xml
	$(ASCIIDOC.man)
endif
endif

define ASCIIDOC.xml
  $(ASCIIDOC) $(OUTPUT_OPTION) -b docbook -d manpage $(ASCIIDOC_FLAGS) $<
endef

define ASCIIDOC.html
  $(ASCIIDOC) $(OUTPUT_OPTION) -d manpage $(ASCIIDOC_FLAGS) $<
endef

define ASCIIDOC.man
  $(XMLTO) -m $(srcdir)/callouts.xsl man $<
endef

.DELETE_ON_ERROR:
