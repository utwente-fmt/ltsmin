bin_PROGRAMS = pinsim

LTSMIN_LIBS = ../util-lib/libutil.la ../ltsmin-lib/libltsmin.la

# PINS_LIBS  =
# PINS_LIBS += ../lts-io/liblts-io.la
# PINS_LIBS += ../hre-io/libhre-io.la
# PINS_LIBS += $(HRE_LIB)
# PINS_LIBS += ../pins-lib/libpins.la
# PINS_LIBS += ../ltsmin-lib/libltsmin.la
# PINS_LIBS += ../util-lib/libutil.la
# PINS_LIBS += ../dm/libdm.la
# PINS_LIBS += ../spg-lib/libspg.la

# PINSim
pinsim_SOURCES   = pinsim.c
pinsim_SOURCES  += ../pins-lib/pins.c ../pins-lib/pins.h
pinsim_LDADD     = ../lts-lib/liblts-lib.la ../lts-io/liblts-io.la
pinsim_LDADD    += ../hre/libhre.la ../hre-io/libhre-io.la 
pinsim_LDADD    += ../pins-lib/libpins.la
pinsim_LDADD    += ../dm/libdm.la
pinsim_LDADD    += $(LTSMIN_LIBS)
pinsim_CPPFLAGS  = $(AM_CPPFLAGS)
pinsim_LDFLAGS   = $(AM_LDFLAGS)