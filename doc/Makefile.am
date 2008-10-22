ASCIIDOC = @ASCIIDOC@
XMLTO    = @XMLTO@
ASCIIDOC_FLAGS = -a version=$(VERSION)

doc_ASCIIDOC = instantiator-mpi.txt ltsmin-mpi.txt ltsmin-convert.txt
EXTRA_DIST = asciidoc.conf callouts.xsl $(doc_ASCIIDOC)

if HAVE_ASCIIDOC
if HAVE_XMLTO
dist_man_MANS = instantiator-mpi.1 ltsmin-mpi.1 ltsmin-convert.1
endif
HTML      = $(doc_ASCIIDOC:%.txt=%.html)
endif
dist_html_DATA = $(HTML)


CLEANFILES = $(doc_ASCIIDOC:.txt=.xml)
MAINTAINERCLEANFILES = $(dist_man_MANS) $(HTML)

docs: html-local $(man_MANS)
html-local: $(HTML)

.PHONY: docs

.txt.xml:
	$(ASCIIDOC.xml)

.txt.html:
	$(ASCIIDOC.html)

%.1 %.2 %.3 %.4 %.5 %.6 %.7 %.8: %.xml
	$(ASCIIDOC.man)

.DELETE_ON_ERROR:

define ASCIIDOC.xml
  $(ASCIIDOC) $(OUTPUT_OPTION) -b docbook -d manpage $(ASCIIDOC_FLAGS) $^
endef

define ASCIIDOC.html
  $(ASCIIDOC) $(OUTPUT_OPTION) -d manpage $(ASCIIDOC_FLAGS) $^
endef

define ASCIIDOC.man
  $(XMLTO) -m callouts.xsl man $^
endef
