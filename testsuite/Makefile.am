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

if TEST_DVE
DEJATOOL +=	alg_backends_dve
endif

if TEST_ETF
DEJATOOL +=	alg_backends_etf
endif

if TEST_LTL
DEJATOOL +=	ltl_model_checking
endif

if TEST_SAFETY
DEJATOOL +=	safety_model_checking
endif

if TEST_POR
DEJATOOL +=	partial_order_reduction
endif

if TEST_DFS_FIFO
DEJATOOL +=	dfs_fifo
endif

if TEST_SCC	
DEJATOOL +=	scc
endif

if HAVE_SPINS_COMPILER 
if TEST_PROMELA
DEJATOOL +=	alg_backends_prom
endif
endif

if HAVE_LIBMCRL2 
if TEST_LTS_GENERATION
#if HAVE_CADP
DEJATOOL +=	lts_generation
#endif
endif
endif

if HAVE_OPAAL_COMPILER
if TEST_OPPAAL
DEJATOOL +=	alg_backends_opaal
endif
endif

if HAVE_LIBMCRL2
if TEST_LPS
DEJATOOL +=	alg_backends_lps
endif
if TEST_PBES
DEJATOOL +=	alg_backends_pbes
endif
endif

if HAVE_LIBMCRL
# Test disabled due to bug in mcrl path parsing
DEJATOOL +=	alg_backends_lpo
endif

if HAVE_PNML
if  TEST_PNML
DEJATOOL +=	alg_backends_pnml
endif
endif

if HAVE_PROB 
if TEST_PROB
DEJATOOL +=	alg_backends_prob
endif
endif

if HAVE_SCOOP
if TEST_MAPA
DEJATOOL +=	alg_backends_mapa
endif
endif
