#ifndef LTS_ENUM_H
#define LTS_ENUM_H

/**
\file lts_enum.h
\brief Interface for enumerating a LTS.

Writing to disk and loading into memory are two examples of tasks, which require
enumerating all states and transitions of a given LTS. Reading from disk and generating an LTS
from a specifcation are two examples of tasks that enumerate all states and transitions.

This library provides a common interface for producers and consumers of labels transition systems.
*/

#define LTS_ENUM_STATES 0x01
#define LTS_ENUM_EDGES  0x02

typedef void (*state_cb)(void* context,int seg,int* state,int* labels);

typedef void (*state_vec_cb)(void* context,int* state,int* labels);

typedef void (*state_seg_cb)(void* context,int seg,int ofs,int* labels);

typedef void (*edge_cb)(void* context,int src_seg,int* src,int dst_seg,int* dst,int*labels);

typedef void (*edge_vec_vec_cb)(void* context,int* src,int* dst,int*labels);

typedef void (*edge_seg_vec_cb)(void* context,int src_seg,int src_ofs,int* dst,int*labels);

typedef void (*edge_seg_seg_cb)(void* context,int src_seg,int src_ofs,int dst_seg,int dst_ofs,int*labels);

typedef void (*state_fold_t)(void* context,int *state,int*seg,int*ofs);

typedef void (*state_unfold_t)(void* context,int seg,int ofs,int* state);


typedef struct lts_enum_struct *lts_enum_cb_t;

/**
\brief Create an abstract enumeration output object.
*/
extern lts_enum_cb_t lts_enum_create(const char*mode);

/**
\brief override the current state_cb method
*/
extern void lts_enum_set_state_cb(lts_enum_cb_t ecb,state_cb cb);

/**
\brief override the current edge_seg_seg_cb method
*/
extern void lts_enum_set_edge_seg_seg_cb(lts_enum_cb_t ecb,edge_seg_seg_cb cb);

/**
\brief
 */
extern void lts_enum_set_context(lts_enum_cb_t ecb,void*context);

/**
\brief Fix inconsistencies in the object.
*/
extern void lts_enum_fix(lts_enum_cb_t ecb);



/**
\brief Create an output object for vector states and index/vector edges.
 */
extern lts_enum_cb_t lts_enum_viv(int len,void* context,state_vec_cb state_cb,edge_seg_vec_cb edge_cb);

/**
\brief Create an output object for indexed states and index/index edges.
 */
extern lts_enum_cb_t lts_enum_iii(int len,void* context,state_seg_cb state_cb,edge_seg_seg_cb edge_cb);

/**
\brief Create an output object for vector states and index/index edges.
 */
extern lts_enum_cb_t lts_enum_vii(int len,void* context,state_vec_cb state_cb,edge_seg_seg_cb edge_cb);

/**
\brief Stack a conversion on top an output.

\param idx_convert If 0 then source and destination index are the same. If 1 they are different,
meaning that every state has to be converted to a vector and (if necessary) back to the new index.
 */
extern lts_enum_cb_t lts_enum_convert(lts_enum_cb_t base,void*context,state_fold_t fold,state_unfold_t unfold,int idx_convert);

extern char* enum_get_mode(lts_enum_cb_t e);
extern void enum_set_mode(lts_enum_cb_t e,const char*mode);

extern void* enum_get_context(lts_enum_cb_t e);

extern void enum_state(lts_enum_cb_t sink,int seg,int* state,int* labels);
extern void enum_seg(lts_enum_cb_t sink,int seg,int ofs,int* labels);

extern void enum_edge(lts_enum_cb_t sink,int src_seg,int* src,int dst_seg,int* dst,int*labels);
extern void enum_vec_vec(lts_enum_cb_t sink,int* src,int* dst,int*labels);
extern void enum_seg_vec(lts_enum_cb_t sink,int src_seg,int src_ofs,int* dst,int*labels);
extern void enum_seg_seg(lts_enum_cb_t sink,int src_seg,int src_ofs,int dst_seg,int dst_ofs,int*labels);

#endif



