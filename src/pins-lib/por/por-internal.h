#ifndef POR_INTERNAL
#define POR_INTERNAL

#include <pins-lib/pins.h>
#include <pins-lib/pins2pins-ltl.h>
#include <pins-lib/por/pins2pins-por.h>
#include <pins-lib/por/por-leap.h>
#include <util-lib/bitmultiset.h>
#include <util-lib/util.h>

extern int SAFETY;
extern int POR_WEAK;

extern int NO_DYN_VIS;
extern int NO_MCNDS;
extern int NO_V;
extern int PREFER_NDS;
extern int CHECK_SEEN;
extern int NO_MC;
extern int USE_DEL;

/**
 * Beam search algorithm for persistent sets
 * Using the NES and NDS information
 */
typedef enum {
    GS_ENABLED      = 0x00, // should be zero (memset)
    GS_DISABLED     = 0x01,
} group_status_t;

typedef enum {
    VISIBLE,            // group or label NES/NDS
    VISIBLE_GROUP,      // group
    VISIBLE_NES,        // label NES
    VISIBLE_NDS,        // label NDS
    VISIBLE_COUNT
} visible_t;

typedef enum {
    WEAK_NONE = 0,
    WEAK_HANSEN,
    WEAK_VALMARI
} por_weak_t;

/**
 * Additional context with por_model returned by this layer
 * Contains dependency relation, co-enabled information
 * of guards, necessary enabling/disabling sets (nes/nds) etc
 */
struct por_ctx {
    model_t         parent;         // parent PINS model
    void           *alg;            // Algorithm context

    int             nguards;        // number of guards
    int             nlabels;        // number of labels (including guards)
    int             ngroups;        // number of groups
    int             nslots;         // state variable slots

    matrix_t        label_nes_matrix;
    matrix_t        label_nds_matrix;
    matrix_t        not_accords_with;
    matrix_t       *nla;
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

    bms_t           *include;
    bms_t           *exclude;
    int              src_changed;

    leap_t          *leap;
};

/**
 * These functions emits the persistent set with cycle proviso
 * To ensure this proviso extra communication with the algorithm is required.
 * The algorithm annotates each transition with the por_proviso flag.
 * For ltl, all selected transition groups in the persistent set must
 * have this por_proviso flag set to true, otherwise enabled(s) will be returned.
 * For safety, the proviso needs to hold for at least on emitted state.
 * The client may (should) set the proviso always to true for deadlocks.
 */

typedef struct proviso_s {
    TransitionCB    cb;
    void           *user_context;
    int             por_proviso_true_cnt;
    int             por_proviso_false_cnt;
    int             force_proviso_true;     // feedback to algorithm that proviso already holds
} prov_t;

extern void hook_cb (void *context, transition_info_t *ti, int *dst, int *cpy);

extern model_t PORwrapper (model_t model);

extern bool por_is_stubborn (por_context *ctx, int group);

extern void por_init_transitions (model_t model, por_context *ctx, int *src);

extern void por_seen_groups (por_context *ctx, int *src, int src_changed);

// number of necessary sets (halves if MC is absent, because no NDSs then)
static inline int
NS_SIZE (por_context* ctx)
{
    return NO_MCNDS ? ctx->nguards : ctx->nguards << 1;
}

static inline int
is_visible (por_context* ctx, int group)
{
    return bms_has(ctx->visible, VISIBLE, group);
}

static inline int
nr_excludes (por_context *ctx)
{
    return bms_count (ctx->exclude, 0);
}

static inline int
is_excluded (por_context *ctx, int group)
{
    return bms_has (ctx->exclude, 0, group);
}

static inline int
not_included (por_context *ctx, int group)
{
    return bms_count (ctx->include, 0) > 0 && bms_has (ctx->include, 0, group);
}

static inline bool
guard_of (por_context *ctx, int i, matrix_t *m, int j)
{
    for (int g = 0; g < ctx->group2guard[i]->count; g++) {
        int guard = ctx->group2guard[i]->data[g];
        if (dm_is_set (m, guard, j)) {
            return true;
        }
    }
    return false;
}

static inline bool
all_guards (por_context *ctx, int i, matrix_t *m, int j)
{
    for (int g = 0; g < ctx->group2guard[i]->count; g++) {
        int guard = ctx->group2guard[i]->data[g];
        if (!dm_is_set (m, guard, j)) {
            return false;
        }
    }
    return true;
}

#endif // POR_INTERNAL
