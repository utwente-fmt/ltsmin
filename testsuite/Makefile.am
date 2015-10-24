AUTOMAKE_OPTIONS = dejagnu
#RUNTESTDEFAULTFLAGS = -v -v

EXTRA_DIST = \
			 ltsmin.exp \
			 lib/*.tcl \
			 */*.exp

CLEANFILES = out/* *.log *.sum

clean-local:
	rm -rf out/*
	rm -rf out

DISTCLEANFILES = site.exp

export DEJAGNU

DEJATOOL  =

DEJATOOL +=	alg_backends_dve \
			alg_backends_etf \
			ltl_model_checking \
			safety_model_checking \
			partial_order_reduction \
			dfs_fifo \
			scc

if HAVE_SPINS_COMPILER
DEJATOOL +=	alg_backends_prom
endif

if HAVE_LIBMCRL2
#if HAVE_CADP
DEJATOOL +=	lts_generation
#endif
endif

if HAVE_OPAAL_COMPILER
DEJATOOL +=	alg_backends_opaal
endif

if HAVE_LIBMCRL2
DEJATOOL +=	alg_backends_lps
DEJATOOL +=	alg_backends_pbes
endif

if HAVE_LIBMCRL
# Test disabled due to bug in mcrl path parsing
DEJATOOL +=	alg_backends_lpo
endif

if HAVE_PNML
DEJATOOL += alg_backends_pnml
endif

if HAVE_PROB
DEJATOOL += alg_backends_prob
endif

if HAVE_SCOOP
DEJATOOL += alg_backends_mapa
endif

