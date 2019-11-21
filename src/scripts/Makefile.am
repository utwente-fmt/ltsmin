
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

CLEANFILES 		= pins-open
DISTCLEANFILES  = subst_pins_open.mk

EXTRA_DIST  = pins-open.in spins

