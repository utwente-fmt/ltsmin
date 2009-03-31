ASCIIDOC = @ASCIIDOC@
XMLTO    = @XMLTO@
ASCIIDOC_FLAGS = -a version=$(VERSION)

MANPAGES = ltsmin-mpi.1 ltsmin-convert.1 gcf.1 lpo-reach.1 lps-reach.1	\
        nips-reach.1 lpo2lts-grey.1 lps2lts-grey.1 nips2lts-grey.1	\
        lpo2lts-mpi.1 lps2lts-mpi.1 nips2lts-mpi.1 etf.5	\
        etf2lts-grey.1 etf2lts-mpi.1 etf-reach.1 ltsmin.7 etf-convert.1

doc_ASCIIDOC = $(addsuffix .txt, $(basename $(MANPAGES)))

extra_ASCIIDOC = inc/spec2lts-mpi.txt inc/spec2lts-grey.txt		\
	inc/spec-reach.txt inc/buddy-options.txt inc/devel-options.txt	\
	inc/file-formats.txt inc/general-options.txt			\
	inc/io-options.txt inc/mcrl-options.txt inc/mpi-blurb.txt	\
	inc/pins-options.txt inc/spec-reach-options.txt			\
	inc/spec-reach.txt inc/spec2lts-grey-options.txt		\
	inc/spec2lts-grey.txt inc/spec2lts-mpi-options.txt		\
	inc/spec2lts-mpi.txt inc/support.txt inc/vset-options.txt       \
        inc/compression.txt inc/mcrl2-options.txt

EXTRA_DIST = asciidoc.conf callouts.xsl $(doc_ASCIIDOC) $(extra_ASCIIDOC)

HTML = $(doc_ASCIIDOC:.txt=.html)

dist_html_DATA = $(HTML)
dist_man_MANS  = $(MANPAGES)

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

.txt.html:
if HAVE_ASCIIDOC
	$(ASCIIDOC.html)
else
	@:
endif

.txt.xml:
if HAVE_ASCIIDOC
	$(ASCIIDOC.xml)
else
	@:
endif

%.txt:
	test -f $@ && touch $@

%.1 %.2 %.3 %.4 %.5 %.6 %.7 %.8: %.xml
if HAVE_ASCIIDOC
if HAVE_XMLTO
	$(ASCIIDOC.man)
else
	@:
endif
else
	@:
endif

if HAVE_ASCIIDOC
-include ./$(DEPDIR)/asciidoc.d

./$(DEPDIR)/asciidoc.d: $(doc_ASCIIDOC) $(extra_ASCIIDOC)
	@$(MKDIR_P) ./$(DEPDIR)
	@grep 'include::' $^ |\
		sed 's/include::/ /;s/\[]//;s%\(.*inc/\).*: *%&\1%' > $@
endif

distclean-local:
	-$(RM) -r ./$(DEPDIR)

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
