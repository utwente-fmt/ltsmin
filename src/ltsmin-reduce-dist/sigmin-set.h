// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#ifndef SIGMIN_SET_H
#define SIGMIN_SET_H

#include <popt.h>

#include <hre-mpi/user.h>
#include <hre-mpi/mpi_event_loop.h>
#include <ltsmin-reduce-dist/seg-lts.h>
#include <ltsmin-reduce-dist/sigmin-types.h>

extern struct poptOption sigmin_set_options[];

extern sig_id_t *sigmin_set_strong(event_queue_t queue,seg_lts_t lts);

/// tau cycle free optimized strong bisimulation
extern sig_id_t *sigmin_set_strong_tcf(event_queue_t queue,seg_lts_t lts,uint32_t tau);

extern sig_id_t *sigmin_set_branching(event_queue_t queue,seg_lts_t lts,uint32_t tau);

/// tau cycle free optimized branching bisimulation
extern sig_id_t *sigmin_set_branching_tcf(event_queue_t queue,seg_lts_t lts,uint32_t tau);

#endif
