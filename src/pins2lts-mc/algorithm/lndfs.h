/*
 * LNDFS by Laarman/Langerak/vdPol/Weber/Wijs (originally MCNDFS)
 *
 *  @incollection {springerlink:10.1007/978-3-642-24372-1_23,
       author = {Laarman, Alfons and Langerak, Rom and van de Pol, Jaco and Weber, Michael and Wijs, Anton},
       affiliation = {Formal Methods and Tools, University of Twente, The Netherlands},
       title = {{Multi-core Nested Depth-First Search}}},
       booktitle = {Automated Technology for Verification and Analysis},
       series = {Lecture Notes in Computer Science},
       editor = {Bultan, Tevfik and Hsiung, Pao-Ann},
       publisher = {Springer Berlin / Heidelberg},
       isbn = {978-3-642-24371-4},
       keyword = {Computer Science},
       pages = {321-335},
       volume = {6996},
       url = {http://eprints.eemcs.utwente.nl/20337/},
       note = {10.1007/978-3-642-24372-1_23},
       year = {2011}
    }
 */

#ifndef LNDFS_H
#define LNDFS_H

#include <pins2lts-mc/algorithm/ndfs.h>

extern void lndfs_blue (run_t *alg, wctx_t *ctx);

#endif // LNDFS_H
