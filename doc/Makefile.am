ASCIIDOC = @ASCIIDOC@
XMLTO    = @XMLTO@

MANPAGE_XSL = $(srcdir)/manpage-normal.xsl
XMLTO_FLAGS =
ASCIIDOC_FLAGS  = -a asciidoc7compatible
ASCIIDOC_FLAGS += -a no-inline-literal
ASCIIDOC_FLAGS += -a ltsmin-asciidoc-no-roff
ASCIIDOC_FLAGS += -a version=$(VERSION)

MANPAGES  = ltsmin.7
MANPAGES += ltsmin-ctl.5
MANPAGES += ltsmin-ltl.5
MANPAGES += ltsmin-mpi.1
MANPAGES += ltsmin-mu.5
MANPAGES += ltsmin-tracepp.1
MANPAGES += ltsmin-convert.1
MANPAGES += etf-convert.1
MANPAGES += etf.5
MANPAGES += gcf.1
MANPAGES += ce-mpi.1
MANPAGES += pins_open.1

MANPAGES += dve-reach.1
MANPAGES += dve2lts-grey.1
MANPAGES += dve2lts-gsea.1
MANPAGES += dve2lts-mc.1
MANPAGES += dve2lts-mpi.1
MANPAGES += dve2torx.1

MANPAGES += etf-reach.1
MANPAGES += etf2lts-grey.1
MANPAGES += etf2lts-gsea.1
MANPAGES += etf2lts-mc.1
MANPAGES += etf2lts-mpi.1
MANPAGES += etf2torx.1

MANPAGES += lpo-reach.1
MANPAGES += lpo2lts-grey.1
MANPAGES += lpo2lts-gsea.1
MANPAGES += lpo2lts-mpi.1
MANPAGES += lpo2torx.1

MANPAGES += lps-reach.1
MANPAGES += lps2lts-grey.1
MANPAGES += lps2lts-gsea.1
MANPAGES += lps2lts-mpi.1
MANPAGES += lps2torx.1

MANPAGES += nips-reach.1
MANPAGES += nips2lts-grey.1
MANPAGES += nips2lts-mc.1
MANPAGES += nips2lts-mpi.1
MANPAGES += nips2torx.1

MANPAGES += spinja-reach.1
MANPAGES += spinja2lts-grey.1
MANPAGES += spinja2lts-gsea.1
MANPAGES += spinja2lts-mc.1
MANPAGES += spinja2lts-mpi.1
MANPAGES += spinja2torx.1

MANPAGES += dve2-reach.1
MANPAGES += dve22lts-grey.1
MANPAGES += dve22lts-gsea.1
MANPAGES += dve22lts-mc.1
MANPAGES += dve22lts-mpi.1
MANPAGES += dve22torx.1


doc_ASCIIDOC = $(addsuffix .txt, $(basename $(MANPAGES)))

extra_ASCIIDOC  = inc/buchi.txt
extra_ASCIIDOC += inc/buddy-options.txt
extra_ASCIIDOC += inc/compression.txt
extra_ASCIIDOC += inc/devel-options.txt
extra_ASCIIDOC += inc/dve2.txt
extra_ASCIIDOC += inc/file-formats.txt
extra_ASCIIDOC += inc/general-options.txt
extra_ASCIIDOC += inc/io-options.txt
extra_ASCIIDOC += inc/ltsmin-tracepp-options.txt
extra_ASCIIDOC += inc/mcrl-options.txt
extra_ASCIIDOC += inc/mcrl2-options.txt
extra_ASCIIDOC += inc/mpi-blurb.txt
extra_ASCIIDOC += inc/pins-options.txt
extra_ASCIIDOC += inc/reachability-options.txt
extra_ASCIIDOC += inc/spec-reach-options.txt
extra_ASCIIDOC += inc/spec-reach.txt
extra_ASCIIDOC += inc/spec-reach.txt
extra_ASCIIDOC += inc/spec2lts-grey-options.txt
extra_ASCIIDOC += inc/spec2lts-grey.txt
extra_ASCIIDOC += inc/spec2lts-grey.txt
extra_ASCIIDOC += inc/spec2lts-gsea-options.txt
extra_ASCIIDOC += inc/spec2lts-gsea.txt
extra_ASCIIDOC += inc/spec2lts-mc-options.txt
extra_ASCIIDOC += inc/spec2lts-mc.txt
extra_ASCIIDOC += inc/spec2lts-mpi-options.txt
extra_ASCIIDOC += inc/spec2lts-mpi.txt
extra_ASCIIDOC += inc/spec2lts-mpi.txt
extra_ASCIIDOC += inc/spec2torx.txt
extra_ASCIIDOC += inc/spinja.txt
extra_ASCIIDOC += inc/state-storage-options.txt
extra_ASCIIDOC += inc/support.txt
extra_ASCIIDOC += inc/vset-options.txt

EXTRA_DIST  = asciidoc.conf
EXTRA_DIST += manpage-base.xsl
EXTRA_DIST += manpage-normal.xsl
EXTRA_DIST += manpage-1.72.xsl
EXTRA_DIST += $(doc_ASCIIDOC)
EXTRA_DIST += $(extra_ASCIIDOC)

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

%.1 %.2 %.3 %.4 %.5 %.6 %.7 %.8: %.xml $(MANPAGE_XSL)
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

./$(DEPDIR)/asciidoc.d: $(doc_ASCIIDOC) $(extra_ASCIIDOC) Makefile
	@$(MKDIR_P) ./$(DEPDIR)
	@grep 'include::' $(filter %.txt,$^) |\
		sed 's/include::/ /;s/\[]//;s%\(.*inc/\).*: *%&\1%' > $@
endif

clean-local:
	-$(RM) -r ./$(DEPDIR)

define ASCIIDOC.xml
  $(ASCIIDOC) $(OUTPUT_OPTION) -b docbook -d manpage $(ASCIIDOC_FLAGS) $<
endef

define ASCIIDOC.html
  $(ASCIIDOC) $(OUTPUT_OPTION) -d manpage $(ASCIIDOC_FLAGS) $<
endef

define ASCIIDOC.man
  $(XMLTO) -m $(MANPAGE_XSL) $(XMLTO_FLAGS) man $<
endef

.DELETE_ON_ERROR:
