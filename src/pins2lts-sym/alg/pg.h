#ifndef ALG_PG_H
#define ALG_PG_H

#include <pins-lib/pins.h>
#include <spg-lib/spg-solve.h>
#include <vset-lib/vector_set.h>


extern void init_spg (model_t model);

extern parity_game *compute_symbolic_parity_game (vset_t visited, int *src);

extern void lts_to_pg_solve (vset_t visited, int* src);

#endif //ALG_PG_H
