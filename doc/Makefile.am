ASCIIDOC = @ASCIIDOC@
XMLTO    = @XMLTO@

MANPAGE_XSL = $(srcdir)/manpage-normal.xsl
XMLTO_FLAGS =
ASCIIDOC_FLAGS  = -a asciidoc7compatible
ASCIIDOC_FLAGS += -a no-inline-literal
ASCIIDOC_FLAGS += -a ltsmin-asciidoc-no-roff
ASCIIDOC_FLAGS += -a version=$(VERSION)

MANPAGES =
MANPAGES += ce-mpi.1
MANPAGES += etf.5
MANPAGES += etf-convert.1
MANPAGES += gcf.1
MANPAGES += ltsmin.7
MANPAGES += ltsmin-ctl.5
MANPAGES += ltsmin-ltl.5
MANPAGES += ltsmin-pred.5
MANPAGES += ltsmin-type-system.5
MANPAGES += ltsmin-mu.5
MANPAGES += ltsmin-mucalc.5
MANPAGES += ltsmin-printtrace.1
MANPAGES += ltsmin-convert.1
MANPAGES += ltsmin-compare.1
MANPAGES += pins-open.1
MANPAGES += ltsmin-reduce-dist.1
MANPAGES += ltsmin-reduce.1
MANPAGES += spins.1

MANPAGES += dve2lts-sym.1
MANPAGES += dve2lts-seq.1
MANPAGES += dve2lts-dist.1
MANPAGES += dve2lts-mc.1
MANPAGES += dve2torx.1

MANPAGES += etf2lts-sym.1
MANPAGES += etf2lts-seq.1
MANPAGES += etf2lts-dist.1
MANPAGES += etf2lts-mc.1
MANPAGES += etf2torx.1

MANPAGES += lpo2lts-sym.1
MANPAGES += lpo2lts-seq.1
MANPAGES += lpo2lts-dist.1
MANPAGES += lpo2lts-mc.1
MANPAGES += lpo2torx.1

MANPAGES += lps2lts-sym.1
MANPAGES += lps2lts-seq.1
MANPAGES += lps2lts-dist.1
MANPAGES += lps2lts-mc.1
MANPAGES += lps2torx.1

MANPAGES += mapa2lts-sym.1
MANPAGES += mapa2lts-dist.1

MANPAGES += pins2lts-sym.1
MANPAGES += pins2lts-seq.1
MANPAGES += pins2lts-dist.1
MANPAGES += pins2lts-mc.1
MANPAGES += pins2torx.1

MANPAGES += prom2lts-sym.1
MANPAGES += prom2lts-seq.1
MANPAGES += prom2lts-dist.1
MANPAGES += prom2lts-mc.1
MANPAGES += prom2torx.1

MANPAGES += opaal2lts-mc.1

MANPAGES += pbes2lts-sym.1
MANPAGES += pbes2lts-seq.1
MANPAGES += pbes2lts-dist.1
MANPAGES += pbes2lts-mc.1

MANPAGES += pnml2lts-sym.1
MANPAGES += pnml2lts-seq.1
MANPAGES += pnml2lts-dist.1
MANPAGES += pnml2lts-mc.1

MANPAGES += prob2lts-sym.1
MANPAGES += prob2lts-seq.1
MANPAGES += prob2lts-dist.1
MANPAGES += prob2lts-mc.1

MANPAGES += spgsolver.1

doc_ASCIIDOC = $(addsuffix .txt, $(basename $(MANPAGES)))

extra_ASCIIDOC  = inc/buchi.txt
extra_ASCIIDOC += inc/buddy-options.txt
extra_ASCIIDOC += inc/compression.txt
extra_ASCIIDOC += inc/devel-options.txt
extra_ASCIIDOC += inc/dve.txt
extra_ASCIIDOC += inc/env-vars.txt
extra_ASCIIDOC += inc/file-formats.txt
extra_ASCIIDOC += inc/general-options.txt
extra_ASCIIDOC += inc/hre-options.txt
extra_ASCIIDOC += inc/hre-startup.txt
extra_ASCIIDOC += inc/lace-options.txt
extra_ASCIIDOC += inc/lddmc-options.txt
extra_ASCIIDOC += inc/listdd-options.txt
extra_ASCIIDOC += inc/listdd64-options.txt
extra_ASCIIDOC += inc/lts-io-options.txt
extra_ASCIIDOC += inc/ltsmin-printtrace-options.txt
extra_ASCIIDOC += inc/mcrl-options.txt
extra_ASCIIDOC += inc/mcrl2-options.txt
extra_ASCIIDOC += inc/mpi-blurb.txt
extra_ASCIIDOC += inc/mpi-example.txt
extra_ASCIIDOC += inc/pbes-options.txt
extra_ASCIIDOC += inc/pins-options.txt
extra_ASCIIDOC += inc/pins2lts-dist-options.txt
extra_ASCIIDOC += inc/pins2lts-dist.txt
extra_ASCIIDOC += inc/pins2lts-mc-options.txt
extra_ASCIIDOC += inc/pins2lts-mc-ta-options.txt
extra_ASCIIDOC += inc/pins2lts-mc.txt
extra_ASCIIDOC += inc/pins2lts-seq-options.txt
extra_ASCIIDOC += inc/pins2lts-seq.txt
extra_ASCIIDOC += inc/pins2lts-sym-options.txt
extra_ASCIIDOC += inc/pins2lts-sym.txt
extra_ASCIIDOC += inc/pins2torx.txt
extra_ASCIIDOC += inc/plugin-options.txt
extra_ASCIIDOC += inc/pnml.txt
extra_ASCIIDOC += inc/pnml-options.txt
extra_ASCIIDOC += inc/prob.txt
extra_ASCIIDOC += inc/prob-options.txt
extra_ASCIIDOC += inc/procs-option.txt
extra_ASCIIDOC += inc/prom.txt
extra_ASCIIDOC += inc/reachability-options.txt
extra_ASCIIDOC += inc/scoop-options.txt
extra_ASCIIDOC += inc/spg-options.txt
extra_ASCIIDOC += inc/spg-solve-options.txt
extra_ASCIIDOC += inc/support.txt
extra_ASCIIDOC += inc/sylvan-options.txt
extra_ASCIIDOC += inc/threads-option.txt
extra_ASCIIDOC += inc/vset-options.txt
extra_ASCIIDOC += inc/regroup-options.txt

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
