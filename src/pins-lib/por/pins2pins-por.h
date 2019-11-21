#ifndef PINS2PINS_POR
#define PINS2PINS_POR

#include <pins-lib/pins.h>
#include <util-lib/bitmultiset.h>
#include <util-lib/util.h>


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
 * @Article{LaarmanSTTT2014,
        author="Laarman, Alfons and Pater, Elwin and Pol, Jaco and Hansen, Henri",
        title="Guard-based partial-order reduction",
        journal="International Journal on Software Tools for Technology Transfer",
        year="2014",
        pages="1--22",
        issn="1433-2787",
        doi="10.1007/s10009-014-0363-9",
        url="http://dx.doi.org/10.1007/s10009-014-0363-9"
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

/**
\brief The POR mode:

no POR, POR, or POR with correctness check (invisible)
*/

typedef enum {
    PINS_POR_NONE,
    PINS_POR_ON,
    PINS_POR_CHECK,
} pins_por_t;

/**
 * \brief boolean indicating whether PINS uses POR
 */
extern pins_por_t PINS_POR;

typedef enum {
    POR_NONE,
    POR_AMPLE,
    POR_AMPLE1,
    POR_BEAM,
    POR_DEL,
    POR_LIPTON,
    POR_TR,
    POR_UNINITIALIZED = -1,
} por_alg_t;

extern por_alg_t    PINS_POR_ALG;

typedef struct por_ctx por_context;

/**
\brief Add POR layer before LTL layer
*/
extern model_t GBaddPOR(model_t model);

extern struct poptOption por_options[];


/**
 * POR internals:
 */

extern int NO_L12;

typedef int (*state_find_f)(int *state, transition_info_t *ti, void *ctx);

extern void por_set_find_state (state_find_f f, void *tmp);

extern void por_stats (model_t model);

#endif // PINS2PINS_POR
