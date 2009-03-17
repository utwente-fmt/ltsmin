ASCIIDOC = @ASCIIDOC@
XMLTO    = @XMLTO@
ASCIIDOC_FLAGS = -a version=$(VERSION)

MANPAGES = ltsmin-mpi.1 ltsmin-convert.1 gcf.1 lpo-reach.1 lps-reach.1	\
        nips-reach.1 lpo2lts-grey.1 lps2lts-grey.1 nips2lts-grey.1	\
        lpo2lts-mpi.1 lps2lts-mpi.1 nips2lts-mpi.1 etf.5	\
        etf2lts-grey.1 etf2lts-mpi.1 etf-reach.1

doc_ASCIIDOC = $(addsuffix .txt, $(basename $(MANPAGES)))

extra_ASCIIDOC = inc/spec2lts-mpi.txt inc/spec2lts-grey.txt		\
	inc/spec-reach.txt inc/buddy-options.txt inc/devel-options.txt	\
	inc/file-formats.txt inc/general-options.txt			\
	inc/io-options.txt inc/mcrl-options.txt inc/mpi-blurb.txt	\
	inc/pins-options.txt inc/spec-reach-options.txt			\
	inc/spec-reach.txt inc/spec2lts-grey-options.txt		\
	inc/spec2lts-grey.txt inc/spec2lts-mpi-options.txt		\
	inc/spec2lts-mpi.txt inc/support.txt inc/vset-options.txt

EXTRA_DIST = asciidoc.conf callouts.xsl $(doc_ASCIIDOC) $(extra_ASCIIDOC)

HTML = $(doc_ASCIIDOC:.txt=.html)

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
.txt.html:
	$(ASCIIDOC.html)

.txt.xml:
	$(ASCIIDOC.xml)

%.txt:
	@test -f $@ && touch $@

if HAVE_XMLTO
%.1 %.2 %.3 %.4 %.5 %.6 %.7 %.8: %.xml
	$(ASCIIDOC.man)
endif

-include ./$(DEPDIR)/asciidoc.d

distclean-local:
	-$(RM) -r ./$(DEPDIR)

./$(DEPDIR)/asciidoc.d: $(doc_ASCIIDOC) $(extra_ASCIIDOC)
	@$(MKDIR_P) ./$(DEPDIR)
	@grep 'include::' $^ |\
		sed 's/include::/ /;s/\[]//;/^inc\//s%: %&inc/%' > $@
endif

define ASCIIDOC.xml
  $(ASCIIDOC) $(OUTPUT_OPTION) -b docbook -d manpage $(ASCIIDOC_FLAGS) $<
endef

define ASCIIDOC.html
  $(ASCIIDOC) $(OUTPUT_OPTION) -d manpage $(ASCIIDOC_FLAGS) $<
endef

define ASCIIDOC.man
  $(XMLTO) -m $(srcdir)/callouts.xsl man $<
  perl -pi -e 's/^\\&.ft/.ft/' $@ # kludge to work around asciidoc bug
endef

.DELETE_ON_ERROR:
