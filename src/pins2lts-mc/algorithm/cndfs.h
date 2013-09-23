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

#include <pins2lts-mc/parallel/run.h>
#include <pins2lts-mc/parallel/worker.h>

extern void cndfs_print_stats   (run_t *run, wctx_t *ctx);

#endif // CNDFS_H
