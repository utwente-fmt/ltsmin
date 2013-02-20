AUTOMAKE_OPTIONS = dejagnu
#RUNTESTDEFAULTFLAGS = -v -v

EXTRA_DIST = \
			 ltsmin.exp \
			 lib/unix.exp \
			 lib/default.exp

CLEANFILES = *.o *.so *.spins *.gcf *.aut *.bcg *.fsm *.etf *.cpp *.c *.log \
             *.sum *.c *.bin \
             */*.o */*.so */*.spins */*.gcf */*.aut */*.bcg */*.fsm */*.etf \
             */*.cpp */*.c */*.log */*.sum */*.c */*.bin

clean: clean_dirs

clean_dirs:
	rm -rf */*.dir
	rm -rf ../examples/*.spins

export DEJAGNU

DEJATOOL = 	alg_backends_dve \
			alg_backends_etf \
			ltl_model_checking \
			safety_model_checking

if HAVE_SPINS_COMPILER
DEJATOOL +=	alg_backends_prom
endif

if HAVE_LIBMCRL2
if HAVE_CADP
DEJATOOL +=	lts_generation
endif
endif

if HAVE_POSIX
# the following tools all require --procs=
if HAVE_LIBMCRL2
DEJATOOL +=	alg_backends_lps
DEJATOOL +=	alg_backends_pbes
endif

if HAVE_OPAAL_COMPILER
DEJATOOL +=	alg_backends_opaal
endif

if HAVE_LIBMCRL
# Test disabled due to bug in mcrl path parsing
DEJATOOL +=	alg_backends_lpo
endif
endif