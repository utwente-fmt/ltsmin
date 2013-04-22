#ifndef PINS2PINS_POR
#define PINS2PINS_POR

#include <dm/dm.h>
#include <hre/user.h>
#include <ltsmin-lib/ltsmin-standard.h>
#include <pins-lib/pins.h>


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
 */

/**
 * Beam search algorithm for persistent sets
 * Using the NES and NDS information
 */
typedef enum {
    GS_ENABLED      = 0x00, // should be zero (memset)
    GS_DISABLED     = 0x01,
} group_status_t;

typedef enum {
    ES_SELECTED     = 0x01,
    ES_READY        = 0x02,
    ES_EMITTED      = 0x04,
} emit_status_t;

/**
 * ci = count, integer
 * frequently used to setup mappings
 */
typedef struct ci_list
{
    int count;
    int data[];
} ci_list;

/**
 * The analysis has global context (por_context)
 * and multiple search contexts (search_context).
 * Each search context is a search in a different state
 * started form a different initial transition
 * The beam search will always start working on the
 * search context with the lowest score.
 *
 * The score of each search context is a sum:
 * - disabled transition group 0
 * - enabled  transition group 1
 * - visible  transition group N
 *
 */
typedef struct search_context
{
    emit_status_t   *emit_status;    // status of each transition group
    int             *work;           // list of size n+1, enabled transitions start at 0 upwards,
                                     // disabled transitions start at n downwards
                                     // [enabled0, enabled1, .. | free | workn-1, workn]
    int             work_enabled;    // number of enabled groups in work
    int             work_disabled;   // number of disabled groups in work
    int             idx;             // index of this search context

    int             score;           // search weight
    int            *nes_score;       // nes score
    int             visibles_selected; // selected number of visible transitions
    int             ve_selected;       // selected number of visible and enabled transitions
    int             enabled_selected;  // selected number of enabled transitions
} search_context;


/**
 * Additional context with por_model returned by this layer
 * Contains dependency relation, co-enabled information
 * of guards, necessary enabling/disabling sets (nes/nds) etc
 */
typedef struct por_context {
    model_t         parent;         // parent PINS model
    int             ltl;
    int             nguards;        // number of guards
    int             nlabels;        // number of labels (including guards
    int             ngroups;        // number of groups
    matrix_t        gnes_matrix;    //
    matrix_t        gnds_matrix;    //
    matrix_t        not_accords_with;   //
    ci_list       **not_accords_tg_tg;  // mapping from transition group to groups that it accords with
    ci_list       **guard2group;    // mapping from guard to transition group
    ci_list       **group2guard;    // mapping from group to guards
    ci_list       **guard_nes;      // transition groups that form a nes for a guard (guard -> [t1, t2, t..])
    ci_list       **guard_nds;      // transition groups that form a nds for a guard
    ci_list       **guard_dep;      // transition groups that depend on a guard
    ci_list       **guard_nce;      // mapping from guards to transition groups that may not be co-enabled

    /**
     * The global data used for the search
     * This data is setup one time for each state that is processed
     */
    int             *label_status;  // status of the guards in current state
    group_status_t  *group_status;  // status of the transition groups in the current state
    int             *group_score;   // score assigned to each group by heuristic function
    int              beam_width;    // maximum width of the beam search
    int              beam_used;     // number of search contexts in use
    int              visible_enabled;// number of enabled visible transitions
    ci_list         *enabled_list;  // enabled groups
    ci_list         *visible_list;  // enabled groups

    // global nes/nds
    int             *nes_score;     // Template for the nes_score (TODO: check)
    ci_list        **ns;            // nes/nds combined
    ci_list        **group2ns;      // mapping group to each nes/nds in which it is used
    ci_list        **group_has;     // mapping group to each nes/nds for it

    int              emit_limit;    // maximum number of transition groups that can be emitted
    int              emit_score;    // directly emit when search finishes with this score
    int              emitted;       // number of already emitted transitions

    // location in search array (extra indirection for quick switching between contexts)
    int             *search_order;
    search_context  *search;        // context for each search

    int             *group_visibility; // visible groups (static)
    int             *label_visibility; // visible labels (for dynamic visibility)

    ci_list         *label_list;    // visible labels
    ci_list         *marked_list;   // labels dynamically marked as visible
    int             *dynamic_visibility; // idem

    void            *scc_ctx;
} por_context;

#endif // PINS2PINS_POR
