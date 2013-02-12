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

DEJATOOL = 	alg_backends_etf \
			alg_backends_rest \
			ltl_model_checking \
			lts_generation \
			safety_model_checking

if HAVE_LIBSTEP_INFO
# Test disabled due to bug in mcrl path parsing
# DEJATOOL +=	alg_backends_lpo#
endif