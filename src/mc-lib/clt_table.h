
#ifndef CLT_TABLE_H
#define CLT_TABLE_H

/**
 * Parallel Cleary table implementation after Laarman/van der Vegt:
 *
 * @incollection {springerlink:10.1007/978-3-642-25929-6_18,
       author = {van der Vegt, Steven and Laarman, Alfons},
       affiliation = {Formal Methods and Tools, University of Twente, The Netherlands},
       title = {{A Parallel Compact Hash Table}},
       booktitle = {Mathematical and Engineering Methods in Computer Science},
       series = {Lecture Notes in Computer Science},
       editor = {Kotasek, Zdenek and Bouda, Jan and Cerna, Ivana and Sekanina, Lukas and Vojnar, Tomas and Antos, David},
       publisher = {Springer Berlin / Heidelberg},
       isbn = {978-3-642-25928-9},
       keyword = {Computer Science},
       pages = {191-204},
       volume = {7119},
       url = {http://eprints.eemcs.utwente.nl/20648/},
       note = {10.1007/978-3-642-25929-6_18},
       year = {2012}
    }
 *
 */

#include <stdbool.h>
#include <stdint.h>


typedef struct clt_dbs_s clt_dbs_t;

/**
 * Like find-or-put function from paper.
 * Assumes keys are randomized. This can be useful in case the client knows
 * certain properties of the key space that can be exploited for efficient
 * randomization.
 */
extern int          clt_find_or_put (const clt_dbs_t* dbs, uint64_t k, bool insert);

extern clt_dbs_t   *clt_create (uint32_t ksize, uint32_t log_size);

extern void         clt_free (clt_dbs_t* dbs);

#endif
