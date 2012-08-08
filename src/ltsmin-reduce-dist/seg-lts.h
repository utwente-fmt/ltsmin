// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#ifndef SEG_LTS_H
#define SEG_LTS_H

#include <hre/user.h>
#include <lts-io/user.h>
#include <ltsmin-lib/lts-type.h>
#include <util-lib/tables.h>

/**
\brief Abstract data type for parallel manipulation of segmented LTSs.

This module is written on the assumption that it will be used in parallel
applications where every thread deals with precisely one segment.
*/

/// type of edge number
typedef uint32_t e_idx_t;

/// type of state number
typedef uint32_t s_idx_t;

/// invalid state
#define INVALID_STATE ((s_idx_t)-1)

/// Enumeration of possible SLTS layouts.
typedef enum {Out_List,In_List,IO_Edge_List,Succ_Pred} seg_lts_layout_t;

/// String representation of layout.
extern const char* SLTSlayoutString(seg_lts_layout_t layout);

/**
\brief Opaque type for one segment of a labeled transitions systems.
*/
typedef struct seg_lts_s *seg_lts_t;

/**
\brief Create an LTS by loading it from the given input.
*/
extern seg_lts_t SLTSload(const char* name,hre_task_queue_t task_queue);

/**
\brief Get the current layout of the SLTS.
*/
extern seg_lts_layout_t SLTSgetLayout(seg_lts_t slts);

/**
\brief Set the layout of the LTS.
*/
extern void SLTSsetLayout(seg_lts_t slts,seg_lts_layout_t layout);

/**
\brief Get the number of states in a segment.
*/
extern int SLTSstateCount(seg_lts_t slts);

/**
\brief Get the number of outgoing edges in a segment.
*/
extern int SLTSoutgoingCount(seg_lts_t slts);

/**
\brief Get the number of incoming edges in a segment.
*/
extern int SLTSincomingCount(seg_lts_t slts);


/**
\brief Get the incoming count of one state.
*/
extern int SLTSinCountState(seg_lts_t lts,s_idx_t state);

/**
\brief Get an incoming edge of a state.
*/
extern void SLTSgetInEdge(seg_lts_t lts,s_idx_t state,int edge_no,uint32_t *edge);

/**
\brief Get one field of an incoming edge.
*/
extern uint32_t SLTSgetInEdgeField(seg_lts_t lts,s_idx_t state,int edge_no,int field);

/**
\brief Map the begin array of incoming transitions.
*/
extern uint32_t* SLTSmapInBegin(seg_lts_t lts);

/**
\brief Map one of the fields of incoming transitions.
*/
extern  uint32_t* SLTSmapInField(seg_lts_t lts,int field);

/**
\brief Get the outgoing count of one state.
*/
extern int SLTSoutCountState(seg_lts_t lts,s_idx_t state);

/**
\brief Get an outgoing edge of a state.
*/
extern void SLTSgetOutEdge(seg_lts_t lts,s_idx_t state,int edge_no,uint32_t *edge);

/**
\brief Map the begin array of outgoing transitions.
*/
extern uint32_t* SLTSmapOutBegin(seg_lts_t lts);

/**
\brief Map one of the fields of outgoing transitions.
*/
extern  uint32_t* SLTSmapOutField(seg_lts_t lts,int field);

/**
\brief Get the ltstype.
*/
extern lts_type_t SLTSgetType(seg_lts_t lts);

/**
\brief Get the numebr of segmetns.
*/
extern int SLTSsegmentCount(seg_lts_t lts);

/**
\brief Get the number of the owned segment.
*/
extern int SLTSsegmentNumber(seg_lts_t lts);

/**
\brief Get the cooperation queue for the segment.
*/
extern hre_task_queue_t SLTSgetQueue(seg_lts_t lts);

/**
\brief Apply an state map to the LTS.

The state map is assumed to encode seg/ofs as ofs*workers+seg.
*/
extern void SLTSapplyMap(seg_lts_t lts,uint32_t *map,uint32_t tau);

/**
\brief Write the LTS.
*/
extern void SLTSstore(seg_lts_t lts,const char* name);

/**
\brief Get the value table for the LTS.
*/
extern value_table_t SLTStable(seg_lts_t lts,int type_no);

#endif

