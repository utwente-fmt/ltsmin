#ifndef LTS_TYPE_H
#define LTS_TYPE_H

#include <stream.h>

/**
\file lts-type.h
\brief This data type stores signatures of labeled transition systems.

- How long is the state vector?
- Which elements of the state vector are visible and what are their types?
- How many labels of which type are there on every edge?
- How many defined state labels are there?
- What are the types used in the LTS?
*/

typedef struct lts_type_s *lts_type_t;

/// Create a new lts type.
extern lts_type_t lts_type_create();

/// Destroy an lts type.
extern void lts_type_destroy(lts_type_t *t);

/// Set state length.
extern void lts_type_set_state_length(lts_type_t  t,int length);

/// Get state length.
extern int lts_type_get_state_length(lts_type_t  t);

/// Set the name of a state slot.
extern void lts_type_set_state_name(lts_type_t  t,int idx,const char* name);

/// Get the name of a state slot.
extern char* lts_type_get_state_name(lts_type_t  t,int idx);

/// Set the type of a state slot by name.
extern void lts_type_set_state_type(lts_type_t  t,int idx,const char* name);

/// Get the type of a state slot.
extern char* lts_type_get_state_type(lts_type_t  t,int idx);

/// Set the type of a state slot by type number.
extern void lts_type_set_state_typeno(lts_type_t  t,int idx,int typeno);

/// Get the type number of a state slot.
extern int lts_type_get_state_typeno(lts_type_t  t,int idx);

extern void lts_type_set_state_label_count(lts_type_t  t,int count);
extern int lts_type_get_state_label_count(lts_type_t  t);
extern void lts_type_set_state_label_name(lts_type_t  t,int label,const char*name);
extern void lts_type_set_state_label_type(lts_type_t  t,int label,const char*name);
extern void lts_type_set_state_label_typeno(lts_type_t  t,int label,int typeno);
extern char* lts_type_get_state_label_name(lts_type_t  t,int label);
extern char* lts_type_get_state_label_type(lts_type_t  t,int label);
extern int lts_type_get_state_label_typeno(lts_type_t  t,int label);

extern void lts_type_set_edge_label_count(lts_type_t  t,int count);
extern int lts_type_get_edge_label_count(lts_type_t  t);
extern void lts_type_set_edge_label_name(lts_type_t  t,int label,const char*name);
extern void lts_type_set_edge_label_type(lts_type_t  t,int label,const char*name);
extern void lts_type_set_edge_label_typeno(lts_type_t  t,int label,int typeno);
extern char* lts_type_get_edge_label_name(lts_type_t  t,int label);
extern char* lts_type_get_edge_label_type(lts_type_t  t,int label);
extern int lts_type_get_edge_label_typeno(lts_type_t  t,int label);

extern int lts_type_get_type_count(lts_type_t  t);
extern int lts_type_add_type(lts_type_t  t,const char *name,int* is_new);
extern char* lts_type_get_type(lts_type_t  t,int typeno);

extern void lts_type_serialize(lts_type_t t,stream_t s);
extern lts_type_t lts_type_deserialize(stream_t s);

#endif

