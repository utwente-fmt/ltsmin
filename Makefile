# CFLAGS=-m32 -std=c99 -Wall -O4 -D_FILE_OFFSET_BITS=64 -g -pthread -pg 
# LDFLAGS=-m32 -pthread -pg 
CC=gcc
LD=gcc
MPICC=mpicc
MPILD=mpicc

ifeq ($(shell arch),i686)
LDFLAGS=-m32 -pthread
ifeq ($(CADP),)
$(warning "set CADP variable to enable BCG support")
CFLAGS=-m32 -std=c99 -Wall -O4 -D_FILE_OFFSET_BITS=64 -pthread
LIBS=
bcgall=
else
$(warning "BCG support enabled")
CFLAGS=-m32 -std=c99 -Wall -O4 -D_FILE_OFFSET_BITS=64 -pthread -DUSE_BCG -I$(CADP)/incl
LIBS=-L$(CADP)/bin.iX86 -lBCG_IO -lBCG -lm
bcgall=bcg2gsf dir2bcg
endif
else
$(warning "assuming 64 bit environment")
CFLAGS=-m64 -std=c99 -Wall -O4 -pthread
LIBS=
bcgall=
LDFLAGS=-m64 -pthread
endif

all: .depend dir2gsf gsf2dir $(bcgall)

docs:
	doxygen docs.cfg
	(cd doc/latex ; make)

.depend: *.c
	touch .depend
	makedepend -f .depend -- $(CFLAGS) -- $^ 2>/dev/null

-include .depend

clean:: .depend
	/bin/rm -rf *.o *~

.SUFFIXES: .o .c .h

mpi%.o: mpi%.c
	$(MPICC) $(CFLAGS) -c $<
	
.c.o:
	$(CC) $(CFLAGS) -c $<

clean::
	/bin/rm -f libutil.a

libutil.a: runtime.o stream.o data_io.o misc.o archive.o archive_dir.o archive_gsf.o ltsmeta.o \
		stream_buffer.o

	ar -r $@ $?

clean::
	/bin/rm -f libmpi.a

libmpi.a: mpi_kernel.o
	ar -r $@ $?

test-%: test-%.o libutil.a
	$(LD) $(LDFLAGS) -o $@ $^ $(LIBS)

dir2%: dir2%.o libutil.a
	$(LD) $(LDFLAGS) -o $@ $^ $(LIBS)

gsf2%: gsf2%.o libutil.a
	$(LD) $(LDFLAGS) -o $@ $^ $(LIBS)

%2dir: %2dir.o libutil.a
	$(LD) $(LDFLAGS) -o $@ $^ $(LIBS)

%2gsf: %2gsf.o libutil.a
	$(LD) $(LDFLAGS) -o $@ $^ $(LIBS)

mpi-%: mpi-%.o libmpi.a libutil.a
	$(MPILD) $(LDFLAGS) -o $@ $^ $(LIBS)

