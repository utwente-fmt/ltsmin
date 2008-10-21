DOC_SOURCE = instantiator-mpi.txt ltsmin-mpi.txt ltsmin-convert.txt
MANPAGES   = instantiator-mpi.1 ltsmin-mpi.1 ltsmin-convert.1
HTML       = $(DOC_SOURCE:%.txt=%.html)

VERSION  = 1.0

ASCIIDOC = asciidoc
ASCIIDOC_FLAGS = -a version=$(VERSION)

XMLTO = xmlto

doc: man html

man: $(MANPAGES)

html: $(HTML)

clean:
	$(RM) $(MANPAGES)
	$(RM) $(DOC_SOURCE:%.txt=%.xml)
	$(RM) $(HTML)

.PHONY: doc man html clean

%.xml: %.txt
	$(ASCIIDOC.xml)

%.html: %.txt
	$(ASCIIDOC.html)

%.1 %.2 %.3 %.4 %.5 %.6 %.7 %.8: %.xml
	$(ASCIIDOC.man)

.DELETE_ON_ERROR:


define ASCIIDOC.xml
  $(ASCIIDOC) -b docbook -d manpage $(ASCIIDOC_FLAGS) $(OUTPUT_OPTION) $^
endef

define ASCIIDOC.html
  $(ASCIIDOC) -d manpage $(ASCIIDOC_FLAGS) $(OUTPUT_OPTION) $^
endef

define ASCIIDOC.man
  $(XMLTO) -m callouts.xsl man $^
endef
