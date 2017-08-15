AUTOMAKE_OPTIONS = dejagnu
#RUNTESTDEFAULTFLAGS = -v -v

EXTRA_DIST =	ltsmin.exp \
		lib/backends.tcl \
		lib/include.tcl \
		lib/default.exp \
		lib/unix.exp \
		check-DVE/test1.exp \
		check-ETF/test1.exp \
		check-LPO/test1.exp \
		check-LPS/test1.exp \
		check-MAPA/test1.exp \
		check-OPAAL/test1.exp \
		check-PBES/test1.exp \
		check-PNML/test1.exp \
		check-ProB/test1.exp \
		check-Promela/test1.exp \
		check-DFS-FIFO/test1.exp \
		check-LTL/test1.exp \
		check-LTS/test1.exp \
		check-POR/test1.exp \
		check-safety/test1.exp \
		check-SCC/test1.exp \
		check-MU/test1.exp

CLEANFILES = out/* *.log *.sum

clean-local:
	rm -rf out/*
	rm -rf out

DISTCLEANFILES = site.exp

export DEJAGNU

.NOTPARALLEL:
.PHONY: $(EXTRA_CHECKS) check-long
$(EXTRA_CHECKS):
	$(MAKE) $(AM_MAKEFLAGS) check DEJATOOL="$@"

check-long: $(EXTRA_CHECKS)

