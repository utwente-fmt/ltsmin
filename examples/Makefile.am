EXTRA_DIST  =	gear.1.etf \
		elevator.3.dve \
		peterson3.pml \
		p117.pml \
		MutexSimple.mch \
		TimingWithoutTimeout9OnTheFlynessShallow.promela \
		ieee-11073.mcrl2 \
		zune.ltl \
		p312.pml \
		onebit.mcrl2 \
		anderson.1.prop4.dve \
		hef_wrong.tbf \
		gear.1.dve \
		elevator.3.ltl \
		iprotocol.2.dve \
		iprotocol.2.prop4.dve \
		brp.mcrl \
		i4 \
		pbes_formula \
		hef_wrong.mcrl \
		fischer4.xml \
		iprotocol.2.ltl \
		nodeadlock.mu \
		Philosophers-5.pnml \
		brp.mcrl2 \
		fgs.promela \
		zune.pml

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

