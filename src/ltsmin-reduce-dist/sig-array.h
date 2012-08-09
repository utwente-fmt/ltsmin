// -*- tab-width:4 ; indent-tabs-mode:nil -*-
/**
\file sig-array.h
\brief Object for manipulation arrays of signatures.
*/

#ifndef SIG_ARRAY_H
#define SIG_ARRAY_H

#include <ltsmin-reduce-dist/seg-lts.h>
#include <ltsmin-reduce-dist/sigmin-types.h>

/// Opaque type for a signature array.
typedef struct sig_array_s* sig_array_t;

/// Create a signature array for a segment.
extern sig_array_t SigArrayCreate(seg_lts_t lts,int seg,const char *sig_type);

/// Destroy a signature array.
extern void SigArrayDestroy(sig_array_t sa,sig_id_t **save_equivalence);

/// Set the current partition ID for a state.
extern void SigArraySetID(sig_array_t sa,s_idx_t state,sig_id_t id);

/// Get the current partition ID of a state.
extern sig_id_t SigArrayGetID(sig_array_t sa,s_idx_t state);

/// Get the current signature of a state.
extern chunk SigArrayGetSig(sig_array_t sa,s_idx_t state);

/// Set the destination partition ID of a state.
extern void SigArraySetDestID(sig_array_t sa,int dst_seg,e_idx_t edge,sig_id_t id);

/**
\brief Enumeration of possible events during signature computation.

SIG_READY means that the new signature of a state has been computed.
The manager should fetch this signature and use SigArraySetID
to assign a partition ID.

ID_READY means that the partition ID should be forwarded.
The manager should fetch the partition ID and enter it for
every edge using SigArraySetDestID.

Note that in case of classical strong bisimulation,
every state will be ID_READY upon starting a new iteration.
In case of a DAG, the deadlocks will start as ID_READY
and the other states will become ID_READY only after
the ID's of their successors have been provided,
their signatures computed fetched and their ID's set.

COMPLETED means that there are no events pending.
The manager can conclude that the round has completed.

*/
typedef enum {SIG_READY,ID_READY,COMPLETED} sig_event_type;
typedef struct{
    sig_event_type what;
    s_idx_t where;
} sig_event_t;

/**
\brief Retrieve the next event to be handled.

This function may wait a long time until returning if it has to wait
for additional information to become available. (E.g. all successor ID's
of a state have to be known before starting the computation of
its signature.)
*/
extern sig_event_t SigArrayNext(sig_array_t sa);

/// Start a new round of signature computation.
extern void SigArrayStartRound(sig_array_t sa);

#endif

