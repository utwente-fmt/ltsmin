// -*- tab-width:4 ; indent-tabs-mode:nil -*-

#ifndef LTS_H
#define LTS_H

#include <sys/types.h>

#include <lts-io/user.h>
#include <lts-type.h>
#include <treedbs.h>

typedef enum {LTS_LIST,LTS_BLOCK,LTS_BLOCK_INV} LTS_TYPE;

typedef struct lts {
    lts_type_t ltstype; //@< contains the signature of the LTS.
    value_table_t *values; //@< value tables for all types.
    LTS_TYPE type;	
    u_int32_t root_count; //@< number of initial states
    u_int32_t *root_list; //@< array of initial states
    u_int32_t transitions;
    u_int32_t states;
    treedbs_t state_db; // used if state vector length is positive.
    u_int32_t *properties; // contains state labels
    treedbs_t prop_idx; // used if there is more than one state label
    u_int32_t *begin;
    u_int32_t *src;
    u_int32_t *label; // contains edge labels
    treedbs_t edge_idx; // used if there is more than one edge label
    u_int32_t *dest;
    int32_t tau;
} *lts_t;


extern void lts_set_sig(lts_t lts,lts_type_t type);

extern lts_t lts_create();

extern void lts_free(lts_t lts);

extern void lts_set_type(lts_t lts,LTS_TYPE type);

extern void lts_set_size(lts_t lts,u_int32_t roots,u_int32_t states,u_int32_t transitions);

extern void lts_uniq_sort(lts_t lts);

extern void lts_uniq(lts_t lts);

extern void lts_sort(lts_t lts);

extern void lts_sort_alt(lts_t lts);

extern void lts_sort_dest(lts_t lts);

extern void lts_bfs_reorder(lts_t lts);

extern void lts_randomize(lts_t lts);

extern void lts_tau_cycle_elim(lts_t lts);

extern void lts_tau_cycle_elim_old(lts_t lts);

extern void lts_tau_indir_elim(lts_t lts);

extern int lts_diameter(lts_t lts);

extern void lts_stats(lts_t lts);

extern void lts_read(char *name,lts_t lts);

extern void lts_write(char *name,lts_t lts,int segments);

extern void lts_read_tra(const char*tra,lts_t lts);

extern void lts_write_tra(const char*tra,lts_t lts);

extern void lts_merge(lts_t lts1,lts_t lts2);

extern lts_t lts_encode_edge(lts_t lts);


#ifdef HAVE_BCG_USER_H

extern void lts_read_bcg(char *name,lts_t lts);

extern void lts_write_bcg(char *name,lts_t lts);

#endif

/**
\brief Create a write interface for an LTS.
*/
lts_file_t lts_writer(lts_t lts,int segments,lts_file_t settings);

/**
\brief Create a read interface for an LTS.
*/
lts_file_t lts_reader(lts_t lts,int segments,lts_file_t settings);


#endif
