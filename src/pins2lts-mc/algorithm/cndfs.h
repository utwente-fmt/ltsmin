/**
 * o Parallel NDFS algorithm by Evangelista/Pettruci/Youcef (ENDFS)
 * o Improved (Combination) NDFS algorithm (CNDFS).
     <Submitted to ATVA 2012>
 * o Combination of ENDFS and LNDFS (NMCNDFS)
     @inproceedings{pdmc11,
       month = {July},
       official_url = {http://dx.doi.org/10.4204/EPTCS.72.2},
       issn = {2075-2180},
       author = {A. W. {Laarman} and J. C. {van de Pol}},
       series = {Electronic Proceedings in Theoretical Computer Science},
       editor = {J. {Barnat} and K. {Heljanko}},
       title = {{Variations on Multi-Core Nested Depth-First Search}},
       address = {USA},
       publisher = {EPTCS},
       id_number = {10.4204/EPTCS.72.2},
       url = {http://eprints.eemcs.utwente.nl/20618/},
       volume = {72},
       location = {Snowbird, Utah},
       booktitle = {Proceedings of the 10th International Workshop on Parallel and Distributed Methods in verifiCation, PDMC 2011, Snowbird, Utah},
       year = {2011},
       pages = {13--28}
      }
 */

#ifndef CNDFS_H
#define CNDFS_H

#include <pins2lts-mc/algorithm/lndfs.h>

typedef struct cndfs_counter_s {
    size_t              rec;
} cndfs_counter_t;

typedef struct cndfs_alg_local_s {
    alg_local_t         ndfs;

    cndfs_counter_t     counters;
    dfs_stack_t         out_stack;
    rt_timer_t          timer;
    alg_local_t        *rec;
} cndfs_alg_local_t;

typedef struct cndfs_reduced_s {
    alg_reduced_t       ndfs;
    size_t              rec;
    float               waittime;
} cndfs_reduced_t;

extern void cndfs_print_stats   (run_t *run, wctx_t *ctx);

extern void cndfs_reduce        (run_t *run, wctx_t *ctx);

#endif // CNDFS_H
