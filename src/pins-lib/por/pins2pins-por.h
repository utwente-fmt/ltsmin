#ifndef PINS2PINS_POR
#define PINS2PINS_POR

#include <dm/dm.h>
#include <hre/user.h>
#include <ltsmin-lib/ltsmin-standard.h>
#include <pins-lib/pins.h>
#include <util-lib/util.h>
#include <util-lib/bitmultiset.h>


/*
 * The partial order reduction algorithm used here is an
 * extension of the stubborn set algorithm (Valmari) as
 * described in the Ph.d. thesis of Godefroid, using
 * necessary disabling sets and a heuristic function
 * to select a good necessary enabling set.
 *
 * Details can be found in:
 *
 * @mastersthesis{por4pins:thesis:2011,
 *      AUTHOR = {Elwin Pater},
 *      TITLE = {Partial Order Reduction for PINS},
 *      YEAR = {2011},
 *      NOTE = {fmt.cs.utwente.nl/files/sprojects/17.pdf},
 *      SCHOOL = {University of Twente},
 *      ADDRESS = {the Netherlands}
 * }
 *
 * Updated by Alfons Laarman and Elwin Pater to correspond more closely
 * to the strong stubborn set theory of Valmari, as described in:
 *
 * @inproceedings {spin2013,
        year = {2013},
        isbn = {978-3-642-39175-0},
        booktitle = {SPIN},
        volume = {7976},
        series = {Lecture Notes in Computer Science},
        editor = {Bartocci, Ezio and Ramakrishnan, C.R.},
        doi = {10.1007/978-3-642-39176-7_15},
        title = {Guard-Based Partial-Order Reduction},
        url = {http://dx.doi.org/10.1007/978-3-642-39176-7_15},
        publisher = {Springer Berlin Heidelberg},
        author = {Laarman, Alfons and Pater, Elwin and Pol, Jaco and Weber, Michael},
        pages = {227-245}
    }
 *
 * @Article{BBR10,
        author={Laarman, Alfons W. and Pater, Elwin and van de Pol, Jaco C. and Weber, Michael},
        title =        "{Guard-based Partial-Order Reduction (Extended Version)}",
        journal =      "International Journal on Software Tools for Technology Transfer (STTT)",
        year =         "2014",
        optnote =      "Special Section on SPIN 13"
    }
 *
 * Improved safety / liveness provisos added by Alfons Laarman according to
 * Valmari's "State Explosion Problem", Chapter 7:
 *
    @incollection {state-explosion,
        year = {1998},
        isbn = {978-3-540-65306-6},
        booktitle = {Lectures on Petri Nets I: Basic Models},
        volume = {1491},
        series = {Lecture Notes in Computer Science},
        editor = {Reisig, Wolfgang and Rozenberg, Grzegorz},
        doi = {10.1007/3-540-65306-6_21},
        title = {The state explosion problem},
        url = {http://dx.doi.org/10.1007/3-540-65306-6_21},
        publisher = {Springer Berlin Heidelberg},
        author = {Valmari, Antti},
        pages = {429-528}
    }
 *
 * The S proviso here is implemented using the visible transitions. Meaning
 * that if weak ignoring is detected, as communicated via the PINS
 * transition_info_t struct by the lower algorithmic layer which performs the
 * state space search, then all visible transitions are selected (similar to
 * what happens in the L2 liveness proviso). The POR search ensures thereafter,
 * that all ignored transitions that may enable visible ones are selected.
 * Thus visible transitions / labels must be set for these safety properties,
 * the visibility proviso is however only used for liveness properties
 * (see ltl field in por_context_t struct below).
 * This implements A5' in Valmari's "Stubborn Set Methods for Process Algebras".
 *
 * TODO:
 * - detect bottom SCCs for weak cycle proviso (and communicate ignored
 * transitions to this layer?)
 *
 */

extern int NO_L12;

typedef struct por_ctx por_context;

extern void por_exclude (por_context *ctx, ci_list *groups);

extern bool por_is_stubborn (por_context *ctx, int group);

extern void por_init_transitions (model_t model, por_context *ctx, int *src);

#endif // PINS2PINS_POR
