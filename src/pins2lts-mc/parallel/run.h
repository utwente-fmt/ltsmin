/**
 *
 */

#ifndef RUN_H
#define RUN_H

typedef struct alg_s        alg_t;
typedef struct alg_shared_s alg_shared_t;
typedef struct alg_reduced_s alg_reduced_t;

typedef struct run_s {
    alg_t              *alg;
    alg_shared_t       *shared;
    alg_reduced_t      *reduced;
} run_t;

#endif // RUN_H
