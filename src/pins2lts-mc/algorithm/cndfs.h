/**
 * o Parallel NDFS algorithm by Evangelista/Pettruci/Youcef (ENDFS)

 * o Improved (Combination) NDFS algorithm (CNDFS).
    @inproceedings{cndfs,
     Author = {Sami Evangelista and Alfons. W. Laarman and Laure Petrucci and van de Pol, Jaco C.},
     Booktitle = {ATVA 2012},
     Editor = {S. {Ramesh}},
     doi = {10.1007/978-3-642-33386-6_22},
     Isbn_13 = {978-3-642-33386-6},
     Pages = {269--283},
     Publisher = {Springer},
     Series = {LNCS},
     Title = {{Improved Multi-Core Nested Depth-First Search}},
     Volume = {7561},
     Year = {2012}
    }

 * o Partial-order reduction for CNDFS is described in:
    @incollection{cycleproviso,
     year={2014},
     isbn={978-3-319-13337-9},
     booktitle={HVC 2014},
     volume={8855},
     series={LNCS},
     editor={Yahav, Eran},
     doi={10.1007/978-3-319-13338-6_20},
     title={Partial-Order Reduction for Multi-core LTL Model Checking},
     url={http://dx.doi.org/10.1007/978-3-319-13338-6_20},
     publisher={Springer},
     author={Laarman, Alfons W. and Wijs, Anton J.},
     pages={267-283},
    }

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

typedef enum cndfs_successors_e {
    CYCLE   = 0, // a (volatile) successor closes a cycle (lies on the stack)
    NONEC   = 1, // no (volatile) successor closes a cyc;e
    SRCINV  = 2, // source state is involatile (this takes priority)
} cndfs_successors_t;

typedef struct cndfs_counter_s {
    size_t              rec;
} cndfs_counter_t;

typedef struct cndfs_alg_local_s {
    alg_local_t         ndfs;

    cndfs_counter_t     counters;
    dfs_stack_t         in_stack;
    dfs_stack_t         out_stack;
    rt_timer_t          timer;
    alg_local_t        *rec;
    fset_t             *pink;
    cndfs_successors_t  successors;
    size_t              accepting_depth;
} cndfs_alg_local_t;

typedef struct cndfs_reduced_s {
    alg_reduced_t       ndfs;
    size_t              rec;
    size_t              max_load;
    float               waittime;
} cndfs_reduced_t;

struct alg_shared_s {
    run_t              *rec;
    run_t              *previous;
    int                 color_bit_shift;
    run_t              *top_level;
    stop_f              run_stop;
    is_stopped_f        run_is_stopped;
};

extern void cndfs_print_stats   (run_t *run, wctx_t *ctx);

extern void cndfs_reduce        (run_t *run, wctx_t *ctx);

extern void cndfs_local_setup   (run_t *run, wctx_t *ctx);

extern void cndfs_local_deinit   (run_t *run, wctx_t *ctx);

#endif // CNDFS_H
