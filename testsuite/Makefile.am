AUTOMAKE_OPTIONS = dejagnu
RUNTESTDEFAULTFLAGS = -v -v


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
DEJATOOL = ltsmin
