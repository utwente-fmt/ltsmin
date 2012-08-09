
bin_SCRIPTS = spinjal

if HAVE_CADP
# pins_open
CADPCC = $(CADP)/src/com/cadp_cc
CADPINCLUDES = -I$(CADP)/incl
bin_SCRIPTS += pins_open
endif

CLEANFILES 		= pins_open spinjal
DISTCLEANFILES  = subst_pins_open.mk

EXTRA_DIST  = pins_open.in spinjal.in
EXTRA_DIST += config.munge.pl subst_pins_open.mk

-include subst_pins_open.mk

pins_open: pins_open.in Makefile subst_pins_open.mk
	$(subst_pins_open) < $< > $@
	chmod +x $@

subst_pins_open.mk: spinjal.in Makefile config.munge.pl
	perl $(srcdir)/config.munge.pl subst_pins_open < $< > $@

spinjal: spinjal.in Makefile subst_pins_open.mk
	$(subst_pins_open) < $< > $@
	chmod +x $@
