/**
 * Thread-safe set structure with iteration and removal 
 * This is a simplification of the union-find structure, used for checking
 * Generalized Rabin Pairs.
 *
 * NB: the structure doesn't allow states with value 0 (not checked expl.)
 *
 */

#ifndef ITERSET_H
#define ITERSET_H

#include <pins2lts-mc/algorithm/algorithm.h>

typedef struct iterset_s        iterset_t;
typedef struct iterset_state_s  iterset_state_t;

typedef enum is_pick_result {
    IS_PICK_DEAD         = 1,
    IS_PICK_SUCCESS      = 2,
    IS_PICK_MARK_DEAD    = 3,
} is_pick_e;

extern iterset_t  *iterset_create ();

extern void        iterset_clear (iterset_t *is);

/* ******************************* operations ****************************** */

extern bool        iterset_is_in_set (const iterset_t *is, ref_t state);

extern is_pick_e   iterset_pick_state_from (iterset_t *is, ref_t state, ref_t *ret);

extern is_pick_e   iterset_pick_state (iterset_t *is, ref_t *ret);

extern bool        iterset_add_state_at (iterset_t *is, ref_t state, ref_t pos);

extern bool        iterset_add_state (iterset_t *is, ref_t state);

extern bool        iterset_remove_state (iterset_t *is, ref_t state);

extern bool        iterset_is_empty (iterset_t *is);

/* ******************************** testing ******************************** */

extern ref_t       iterset_debug (const iterset_t *is);

extern int         iterset_size (const iterset_t *is);

#endif /* ITERSET_H */
