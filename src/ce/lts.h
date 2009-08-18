#ifndef LTS_H
#define LTS_H

#include <sys/types.h>
#include "config.h"

#ifdef USE_SVC
extern void* lts_stack_bottom;
extern int lts_aterm_init_done;
#endif

typedef enum {LTS_LIST,LTS_BLOCK,LTS_BLOCK_INV} LTS_TYPE;

typedef struct lts {
	LTS_TYPE type;
	u_int32_t root;
	u_int32_t transitions;
	u_int32_t states;
	u_int32_t *begin;
	u_int32_t *src;
	u_int32_t *label;
	u_int32_t *dest;
	int tau;
	char **label_string;
	u_int32_t label_count;
} *lts_t;

extern lts_t lts_create();

extern void lts_free(lts_t lts);

extern void lts_set_type(lts_t lts,LTS_TYPE type);

extern void lts_set_size(lts_t lts,u_int32_t states,u_int32_t transitions);

extern void lts_uniq(lts_t lts);

extern void lts_sort(lts_t lts);

extern void lts_sort_alt(lts_t lts);

extern void lts_bfs_reorder(lts_t lts);

extern void lts_tau_cycle_elim(lts_t lts);

extern void lts_tau_indir_elim(lts_t lts);

#define LTS_AUTO 0
#define LTS_AUT 1
#ifdef USE_BCG
#define LTS_BCG 2
#endif
#define LTS_DIR 3
#ifdef USE_SVC
#define LTS_SVC 4
#endif

extern int lts_write_segments;

extern void lts_read(int format,char *name,lts_t lts);

extern void lts_write(int format,char *name,lts_t lts);

#endif
