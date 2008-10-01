# CFLAGS=-m32 -std=c99 -Wall -O4 -D_FILE_OFFSET_BITS=64 -g -pthread -pg 
# LDFLAGS=-m32 -pthread -pg 
CC=gcc
LD=gcc
MPICC=mpicc -DUSE_MPI
MPILD=mpicc
OPT=-O4 -g

mcrl=$(shell which mcrl 2>/dev/null)
ifeq ($(mcrl),)
$(warning "mCRL not found")
mcrlall=
else
p1=$(shell dirname $(mcrl))
MCRL=$(shell dirname $(p1))/mCRL
$(warning "mCRL found in $(MCRL)")
mcrlall=mpi-inst
endif

ifeq ($(shell arch),i686)
ifeq ($(CADP),)
$(warning "set CADP variable to enable BCG support")
BCG_FLAGS=
BCG_LIBS=
bcgall=
else
$(warning "BCG support enabled")
BCG_FLAGS=-DUSE_BCG -I$(CADP)/incl
BCG_LIBS=-L$(CADP)/bin.iX86 -lBCG_IO -lBCG -lm
bcgall=bcg2gsf dir2bcg
endif
else
$(warning Assuming that BCG does not work in 64 bit)
endif

ifeq ($(shell arch),i686)
LDFLAGS=-m32 -pthread
CFLAGS=-m32 -std=c99 -Wall $(OPT) -D_FILE_OFFSET_BITS=64 -pthread $(BCG_FLAGS)
LIBS=$(BCG_LIBS) -lrt
else
$(warning "assuming 64 bit environment")
CFLAGS=-m64 -std=c99 -Wall $(OPT) -pthread
LIBS=-lrt
LDFLAGS=-m64 -pthread
endif

all: .depend dir2gsf gsf2dir observe $(bcgall) mpi_min $(mcrlall) dir2gcf gcf2dir\
	par_wr par_rd seq_wr seq_rd

docs:
	doxygen docs.cfg
	(cd doc/latex ; make clean refman.pdf)

.depend: *.c
	touch .depend
	makedepend -f .depend -- $(CFLAGS) -- $^ 2>/dev/null

-include .depend

clean:: .depend
	/bin/rm -rf *.o *~

.SUFFIXES: .o .c .h

mpi-inst.o: mpi-inst.c
	$(MPICC) $(CFLAGS) -I$(MCRL)/include -c $<

mpi%.o: mpi%.c
	$(MPICC) $(CFLAGS) -c $<
	
.c.o:
	$(CC) $(CFLAGS) -c $<

clean::
	/bin/rm -f libutil.a

libutil.a: runtime.o stream.o data_io.o misc.o archive.o archive_dir.o archive_gsf.o ltsmeta.o \
		stream_buffer.o fast_hash.o generichash4.o generichash8.o treedbs.o \
		archive_format.o raf.o stream_mem.o ghf.o archive_gcf.o time.o
	ar -r $@ $?

clean::
	/bin/rm -f libmpi.a

libmpi.a: mpi_io_stream.o mpi_core.o mpi_raf.o mpi_ram_raf.o
	ar -r $@ $?


mpi_min:  mpi_min.o set.o lts.o dlts.o libmpi.a libutil.a
	$(MPILD) $(LDFLAGS) -o $@ $^ $(LIBS)

mpi-inst: mpi-inst.o libmpi.a libutil.a
	$(MPILD) $(LDFLAGS) -o $@ $^ $(LIBS) -L$(MCRL)/lib -ldl -lATerm -lmcrl -lstep -lmcrlunix -lexplicit -lz

mpi%: mpi%.o libmpi.a libutil.a
	$(MPILD) $(LDFLAGS) -o $@ $^ $(LIBS)

%: %.o libutil.a
	$(LD) $(LDFLAGS) -o $@ $^ $(LIBS)



