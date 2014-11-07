/**
 *
 * DFS_FIFO algorithm for progress cycle detection with POR, as described in:
 *
   @incollection{pdfsfifo,
    year={2013},
    isbn={978-3-642-38087-7},
    booktitle={NFM 2013},
    volume={7871},
    series={LNCS},
    editor={Brat, Guillaume and Rungta, Neha and Venet, Arnaud},
    doi={10.1007/978-3-642-38088-4_3},
    title={Improved on-the-Fly Livelock Detection},
    publisher={Springer},
    author={Laarman, Alfons W. and Farag\'o, David},
    pages={32-47}
   }
 *
 */

#ifndef DFS_FIFO_H
#define DFS_FIFO_H

#include <pins2lts-mc/algorithm/ltl.h>
#include <pins2lts-mc/algorithm/reach.h>

extern struct poptOption dfs_fifo_options[];

#endif // DFS_FIFO_H
