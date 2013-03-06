
bin_SCRIPTS = spins

if HAVE_CADP
CADPCC = $(CADP)/src/com/cadp_cc
CADPINCLUDES = -I$(CADP)/incl
bin_SCRIPTS += pins-open
endif

# pins-open
pins-open: pins-open.in Makefile subst_pins_open.mk
	$(subst_pins_open) < $< > $@
	chmod +x $@

# spins
spins: spins.in Makefile subst_pins_open.mk
	$(subst_pins_open) < $< > $@
	chmod +x $@

-include subst_pins_open.mk

subst_pins_open.mk: spins.in Makefile config.munge.pl
	perl $(srcdir)/config.munge.pl subst_pins_open < $< > $@

CLEANFILES 		= pins-open spins
DISTCLEANFILES  = subst_pins_open.mk

EXTRA_DIST  = pins-open.in spins.in
EXTRA_DIST += config.munge.pl subst_pins_open.mk
	
