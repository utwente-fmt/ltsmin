
bin_SCRIPTS = spinjal

if HAVE_CADP
CADPCC = $(CADP)/src/com/cadp_cc
CADPINCLUDES = -I$(CADP)/incl
bin_SCRIPTS += pins-open
endif

# pins_open
pins-open: pins-open.in Makefile subst_pins_open.mk
	$(subst_pins_open) < $< > $@
	chmod +x $@

# spinjal
spinjal: spinjal.in Makefile subst_pins_open.mk
	$(subst_pins_open) < $< > $@
	chmod +x $@

-include subst_pins_open.mk

subst_pins_open.mk: spinjal.in Makefile config.munge.pl
	perl $(srcdir)/config.munge.pl subst_pins_open < $< > $@

CLEANFILES 		= pins_open spinjal
DISTCLEANFILES  = subst_pins_open.mk

EXTRA_DIST  = pins-open.in spinjal.in
EXTRA_DIST += config.munge.pl subst_pins_open.mk
	