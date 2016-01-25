
EXTRA_DIST  =
EXTRA_DIST += *.dve
EXTRA_DIST += *.mcrl
EXTRA_DIST += *.tbf
EXTRA_DIST += *.ltl
EXTRA_DIST += *.xml
EXTRA_DIST += *.etf
EXTRA_DIST += i4
EXTRA_DIST += *.promela
EXTRA_DIST += *.mcrl2
EXTRA_DIST += *.mu
EXTRA_DIST += *.pml
EXTRA_DIST += pbes_formula
EXTRA_DIST += *.pnml
EXTRA_DIST += *.mch

if HAVE_LIBMCRL2
EXTRA_DIST += brp.lps
EXTRA_DIST += brp.pbes
EXTRA_DIST += ieee-11073.lps

BUILT_SOURCES =
BUILT_SOURCES += brp.lps
BUILT_SOURCES += brp.pbes
BUILT_SOURCES += ieee-11073.lps
endif

CLEANFILES =
CLEANFILES += brp.lps
CLEANFILES += brp.pbes
CLEANFILES += ieee-11073.lps

%.lps: %.mcrl2
	$(MCRL2) -nDf $< $@

%.pbes: %.lps
	$(PBES) -f $(srcdir)/nodeadlock.mu $< $@

