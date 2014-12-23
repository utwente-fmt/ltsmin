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

extern int POR_WEAK;
extern int NO_L12;

/**
 * Beam search algorithm for persistent sets
 * Using the NES and NDS information
 */
typedef enum {
    GS_ENABLED      = 0x00, // should be zero (memset)
    GS_DISABLED     = 0x01,
} group_status_t;

/**
 * Additional context with por_model returned by this layer
 * Contains dependency relation, co-enabled information
 * of guards, necessary enabling/disabling sets (nes/nds) etc
 */
typedef struct por_context {
    model_t         parent;         // parent PINS model

    int             nguards;        // number of guards
    int             nlabels;        // number of labels (including guards)
    int             ngroups;        // number of groups
    int             nslots;         // state variable slots

    matrix_t        label_nes_matrix;
    matrix_t        label_nds_matrix;
    matrix_t        not_accords_with;
    matrix_t        nce;            // not-coenabled
    matrix_t        gnce_matrix;    // guard not-coenabled
    ci_list       **not_accords;    // mapping from transition group to groups that it accords with
    ci_list       **guard2group;    // mapping from guard to transition group
    ci_list       **group2guard;    // mapping from group to guards
    ci_list       **label_nes;      // transition groups that form a nes for a guard (guard -> [t1, t2, t..])
    ci_list       **label_nds;      // transition groups that form a nds for a guard
    ci_list       **guard_nce;      // mapping from guards to transition groups that may not be co-enabled
    ci_list       **group_nce;      // mapping from guards to transition groups that may not be co-enabled
    ci_list       **ns;             // nes/nds combined
    ci_list       **group2ns;       // mapping group to each nes/nds in which it is used
    ci_list       **group_has;      // mapping group to each nes/nds for it
    ci_list       **group_hasn;     // mapping group to each nes/nds for it
    ci_list       **not_left_accords;
    ci_list       **not_left_accordsn;
    ci_list       **dna_diff;       // group in dna but not in dns (for finding keys)

    int             *group_visibility; // visible groups
    int             *label_visibility; // visible labels

    void            *beam_ctx;      // BEAM search struct
    void            *scc_ctx;       // SCC search struct
    void            *del_ctx;       // Deletion struct
    void            *ample_ctx;     // Ample set context

    /**
     * The global data used for the search
     * This data is setup one time for each state that is processed
     */
    int             *label_status;  // status of the guards in current state
    char            *group_status;  // status of the transition groups in the current state
    ci_list         *enabled_list;  // enabled groups
    bms_t           *visible;
    bms_t           *visible_nes;
    bms_t           *visible_nds;
    int              visible_enabled;// number of enabled visible transitions
    int              visible_nes_enabled;// number of enabled visible transitions
    int              visible_nds_enabled;// number of enabled visible transitions

    int             *group_score;   // score assigned to each group by heuristic function
    int             *nes_score;     // Template for the nes_score
} por_context;

extern bool por_is_stubborn (por_context *ctx, int group);

#endif // PINS2PINS_POR
