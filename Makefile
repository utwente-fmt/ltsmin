# CFLAGS=-m32 -std=c99 -Wall -O4 -D_FILE_OFFSET_BITS=64 -g -pthread -pg 
# LDFLAGS=-m32 -pthread -pg 
CC=gcc
LD=gcc
AR=ar
RANLIB=ranlib
MPICC=mpicc -DUSE_MPI
MPILD=mpicc
OPT=-O4 -g

ifeq (Linux,$(shell uname -s))
ARCH_LIBS=-lrt
else
ARCH_LIBS=
endif

mcrl:=$(shell which mcrl 2>/dev/null)
ifeq ($(mcrl),)
$(warning mCRL not found)
mcrlall=
else
p1=$(shell dirname $(mcrl))
MCRL=$(shell dirname $(p1))/mCRL
$(warning mCRL found in $(MCRL))
mcrlall=instantiator-mpi
endif

MACHINE := $(subst -, ,$(shell $(CC) -dumpmachine))
ARCH    := $(firstword $(MACHINE))

ifneq (, $(findstring $(ARCH), i386 i486 i586))
ifeq ($(CADP),)
$(warning set CADP variable to enable BCG support)
BCG_FLAGS=
BCG_LIBS=
bcgall=
else
$(warning BCG support enabled)
CADP_ARCH := $(shell $(CADP)/com/arch)
BCG_FLAGS=-DUSE_BCG -I$(CADP)/incl
BCG_LIBS=-L$(CADP)/bin.$(CADP_ARCH) -lBCG_IO -lBCG -lm
bcgall=bcg2gsf ar2bcg
endif
else
$(warning Assuming that BCG does not work in 64 bit)
endif

ifneq (, $(findstring $(ARCH), i386 i486 i586))
LDFLAGS=-m32 -pthread
CFLAGS=-m32 -std=c99 -Wall $(OPT) -D_FILE_OFFSET_BITS=64 -pthread $(BCG_FLAGS)
LIBS=$(BCG_LIBS) -lz $(ARCH_LIBS)
else
$(warning assuming 64 bit environment)
CFLAGS=-m64 -std=c99 -Wall $(OPT) -pthread
LIBS=-lz $(ARCH_LIBS)
LDFLAGS=-m64 -pthread
endif

BINARIES= $(bcgall) $(mcrlall) ltsmin-mpi ltscopy \
	gsf2ar ar2gsf mkar sdd par_wr par_rd seq_wr seq_rd mpi_rw_test

all: .depend $(BINARIES)

distclean: clean
	$(RM) -f $(BINARIES)

docs:
	doxygen docs.cfg
	(cd doc/latex ; make clean refman.pdf)

.depend: *.c
	touch .depend
	makedepend -f .depend -- $(CFLAGS) -- $^ 2>/dev/null

-include .depend

clean:: .depend
	$(RM) -rf *.o *~

.SUFFIXES: .o .c .h

instantiator-mpi.o: instantiator-mpi.c
	$(MPICC) $(CFLAGS) -I$(MCRL)/include -c $<

mpi%.o: mpi%.c
	$(MPICC) $(CFLAGS) -c $<

%-mpi.o: %-mpi.c
	$(MPICC) $(CFLAGS) -c $<

.c.o:
	$(CC) $(CFLAGS) -c $<

clean::
	$(RM) -f libutil.a

libutil.a: unix.o runtime.o stream.o misc.o archive.o archive_dir.o archive_gsf.o ltsmeta.o \
		stream_buffer.o fast_hash.o generichash4.o generichash8.o treedbs.o \
		archive_format.o raf.o stream_mem.o ghf.o archive_gcf.o time.o \
		gzstream.o stream_diff32.o options.o
	$(AR) -r $@ $?
	$(RANLIB) $@

clean::
	$(RM) -f libmpi.a

libmpi.a: mpi_io_stream.o mpi_core.o mpi_raf.o mpi_ram_raf.o
	$(AR) -r $@ $?
	$(RANLIB) $@


ltsmin-mpi:  ltsmin-mpi.o set.o lts.o dlts.o libmpi.a libutil.a
	$(MPILD) $(LDFLAGS) -o $@ $^ $(LIBS)

instantiator-mpi: instantiator-mpi.o libmpi.a libutil.a
	$(MPILD) $(LDFLAGS) -o $@ $^ $(LIBS) -L$(MCRL)/lib -ldl -lATerm -lmcrl -lstep -lmcrlunix -lz

mpi%: mpi%.o libmpi.a libutil.a
	$(MPILD) $(LDFLAGS) -o $@ $^ $(LIBS)

%-mpi: %-mpi.o libmpi.a libutil.a
	$(MPILD) $(LDFLAGS) -o $@ $^ $(LIBS)

lts%: lts%.o libutil.a
	$(LD) $(LDFLAGS) -o $@ $^ $(LIBS)

%: %.o libutil.a
	$(LD) $(LDFLAGS) -o $@ $^ $(LIBS)

