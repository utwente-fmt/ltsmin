ASCIIDOC = @ASCIIDOC@
XMLTO    = @XMLTO@
ASCIIDOC_FLAGS = -a version=$(VERSION)

doc_ASCIIDOC = instantiator-mpi.txt ltsmin-mpi.txt ltsmin-convert.txt	\
    ltsmin.txt
EXTRA_DIST = asciidoc.conf callouts.xsl $(doc_ASCIIDOC)

MANPAGES = instantiator-mpi.1 ltsmin-mpi.1 ltsmin-convert.1 ltsmin.5
HTML = $(doc_ASCIIDOC:%.txt=%.html)

dist_man_MANS  = $(MANPAGES)
dist_html_DATA = $(HTML)

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
  $(ASCIIDOC) $(OUTPUT_OPTION) -b docbook -d manpage $(ASCIIDOC_FLAGS) $^
endef

define ASCIIDOC.html
  $(ASCIIDOC) $(OUTPUT_OPTION) -d manpage $(ASCIIDOC_FLAGS) $^
endef

define ASCIIDOC.man
  $(XMLTO) -m callouts.xsl man $^
endef

.DELETE_ON_ERROR:
