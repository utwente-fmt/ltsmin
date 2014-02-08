#ifndef ZOBRIST_H
#define ZOBRIST_H

/**
 * Implementation of zobrist incremental hashing on fixed-length state vectors.
 * Successor states in model checking often exhibit similarity to their
 * predecessors: only a few variables are updated (the state space explosion
 * is caused by combinatorial values of the variables in the states).
 * This zobrist implementation exploits this, by hashing in and out random
 * values for the variables that a state differs from its predecessor. The set
 * of random values is small so it can be precalculated and stored. It is
 * indexed by the variable's value.
 *
 * @incollection {springerlink:10.1007/978-3-642-20398-5_40,
     author = {Laarman, Alfons and van de Pol, Jaco and Weber, Michael},
     affiliation = {Formal Methods and Tools, University of Twente, The Netherlands},
     title = {Multi-Core LTS&lt;span style="font-variant:small-caps"&gt;&lt;small&gt;min&lt;/small&gt;&lt;/span&gt;: Marrying Modularity and Scalability},
     booktitle = {NASA Formal Methods},
     series = {Lecture Notes in Computer Science},
     editor = {Bobaru, Mihaela and Havelund, Klaus and Holzmann, Gerard and Joshi, Rajeev},
     publisher = {Springer Berlin / Heidelberg},
     isbn = {978-3-642-20397-8},
     keyword = {Computer Science},
     pages = {506-511},
     volume = {6617},
     url = {http://dx.doi.org/10.1007/978-3-642-20398-5_40},
     note = {10.1007/978-3-642-20398-5_40},
     year = {2011}
   }
 *
 */


#include <stdint.h>

#include <dm/dm.h>
#include <util-lib/fast_hash.h>

typedef struct zobrist_s *zobrist_t;

extern hash64_t     zobrist_hash
    (const zobrist_t z, int *v, int *prev, hash64_t h);

extern hash64_t     zobrist_hash_dm
    (const zobrist_t z, int *v, int *prev, hash64_t h, int g);

extern hash64_t     zobrist_rehash (zobrist_t z, hash64_t seed);

extern zobrist_t    zobrist_create (size_t length, size_t z, matrix_t * m);

extern void         zobrist_free (zobrist_t z);

#endif
