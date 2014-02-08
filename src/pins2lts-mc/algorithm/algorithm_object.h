
/** @file algorithm_object.h
 * @brief C encoding of the algorithm object.
 */

#ifndef ALGORITHM_OBJECT_H
#define ALGORITHM_OBJECT_H

#include <pins2lts-mc/algorithm/algorithm.h>

typedef struct alg_obj_s {
    char                   *type_name;
    alg_global_init_f       alg_global_init;
    alg_local_init_f        alg_local_init;
    alg_run_f               alg_run;
    alg_reduce_f            alg_reduce;
    alg_print_stats_f       alg_print_stats;
    alg_global_deinit_f     alg_global_deinit;
    alg_local_deinit_f      alg_local_deinit;
    alg_global_bits_f       alg_global_bits;
    alg_state_seen_f        alg_state_seen;
} alg_obj_t;

#endif // ALGORITHM_OBJECT_H
