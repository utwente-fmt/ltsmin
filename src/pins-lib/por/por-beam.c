#include <hre/config.h>

#include <limits.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>

#include <dm/dm.h>
#include <hre/stringindex.h>
#include <hre/unix.h>
#include <hre/user.h>
#include <ltsmin-lib/ltsmin-standard.h>
#include <pins-lib/pins.h>
#include <pins-lib/pins-util.h>
#include <pins-lib/por/por-beam.h>
#include <pins-lib/por/por-internal.h>
#include <util-lib/bitmultiset.h>
#include <util-lib/util.h>


static int NO_HEUR = 0;
static int MAX_BEAM = -1;
static int NO_HEUR_BEAM = 0;
static int RANDOM = 0;
static int USE_MC = 0;

struct poptOption beam_options[]={
    { "no-heur" , 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN , &NO_HEUR , 1 ,"without heuristic" , NULL },
    { "beam" , 0, POPT_ARG_INT | POPT_ARGFLAG_DOC_HIDDEN , &MAX_BEAM , 1 , "maximum number of beam searches" , NULL },
    { "no-heur-beam" , 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN , &NO_HEUR_BEAM , 1 , "without heuristic / beam search" , NULL },
    { "por-random" , 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN , &RANDOM , 1 , "randomize enabled and NES selection" , NULL },
    { "use-mc" , 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN , &USE_MC , 1 , "Use MC to reduce search path" , NULL },
    POPT_TABLEEND
};

const char* keyNames[] = {"None", "Any", "Visible", "Invisible"};
typedef enum {
    KEY_NONE,
    KEY_ANY,
    KEY_VISIBLE,
    KEY_INVISIBLE
} key_type_t;

typedef enum {
    ES_QUEUED       = 0x01,
    ES_VISITED      = 0x02,
    ES_EMITTED      = 0x04,
} emit_status_t;

/**
 * The analysis has multiple search contexts (search_context).
 * Each search context is a search in a different state started form a different
 * initial transition. The beam search will always start working on the
 * search context with the lowest score.
 */
typedef struct search_context {
    int             idx;            // index of this search context
    int             group;          // initial enabled group of search
    int             initialized;    // initialized arrays?
    char           *emit_status;    // status of each transition group
    int            *nes_score;      // nes score
    ci_list        *work_enabled;   // search queue of enabled transitions
    ci_list        *work_disabled;  // search queue of disabled transitions
    ci_list        *enabled;        // enabled transitions selected
    ci_list        *emitted;        // emitted
    int             score_disabled; // search weight
    int             score_visible;  // selected number of visible transitions
    int             score_vis_en;   // selected number of visible and enabled transitions
    key_type_t      has_key;        // key transitions type
    int             visible_nes;    // set of visible NESs included
    int             visible_nds;    // set of visible NDSs included
} search_context_t;

typedef struct beam_s {
    int              beam_used;     // number of search contexts in use
    search_context_t**search;       // search contexts
} beam_t;

static inline void
incr_ns_update (por_context *ctx, int group, int new_group_score)
{
    HREassert (new_group_score > 0);

    int oldgroup_score = ctx->group_score[group];
    if (oldgroup_score == new_group_score) return;

    ctx->group_score[group] = new_group_score;
    for (int i = 0; i < ctx->group2ns[group]->count; i++) {
        int ns = ctx->group2ns[group]->data[i];
        ctx->nes_score[ns] += new_group_score - oldgroup_score;
    }
}

static void
por_transition_costs (por_context *ctx)
{
    if (NO_HEUR) return;

    // set score for enable transitions
    if (PINS_LTL || SAFETY) {
        int visible = ctx->visible_enabled * ctx->ngroups
                        + bms_count (ctx->visible, VISIBLE)
                        - ctx->visible_enabled;
        int visibleNds = ctx->visible_nds_enabled * ctx->ngroups
                        + bms_count (ctx->visible, VISIBLE_NDS)
                        - ctx->visible_nds_enabled;
        int visibleNes = ctx->visible_nes_enabled * ctx->ngroups
                        + bms_count (ctx->visible, VISIBLE_NES)
                        - ctx->visible_nes_enabled;
        if (NO_DYN_VIS) {
            visibleNes = visibleNds = visible;
        }
        if (NO_V) {
            visible = visibleNds = visibleNes = ctx->enabled_list->count * ctx->ngroups;
        }
        for (int i = 0; i < ctx->ngroups; i++) {
            int             new_score;
            if (ctx->group_status[i] == GS_DISABLED) {
                new_score = 1;
            } else {
                int vis = ctx->visible->set[i];
                bool nes = (vis & (1 << VISIBLE_NES)) != 0;
                bool nds = (vis & (1 << VISIBLE_NDS)) != 0;
                if (vis && nes == nds) { // visible & group | visible & nes & nds
                    new_score = visible;
                } else if (nes) {
                    new_score = visibleNds;
                } else if (nds) {
                    new_score = visibleNes;
                } else {
                    new_score = ctx->ngroups;
                }
            }
            incr_ns_update (ctx, i, new_score);
        }
    } else {
        for (int i = 0; i < ctx->ngroups; i++) {
            int enabled = ctx->group_status[i] == GS_ENABLED;
            incr_ns_update (ctx, i, enabled ? ctx->ngroups : 1);
        }
    }

    if (ctx->exclude == NULL) return;
    // try avoid excluded groups
    int max_score = ctx->enabled_list->count * ctx->ngroups;
    for (int g = 0; g < ctx->exclude->count; g++) {
        int group = ctx->exclude->data[g];
        incr_ns_update (ctx, group, max_score);
    }
}

/**
 * The function update_score is called whenever a new group is added to the stubborn set
 * It takes care of updating the heuristic function for the nes based on the new group selection
 */
static inline void
update_ns_scores (por_context* ctx, search_context_t *s, int group)
{
    if (NO_HEUR) return;
    // change the heuristic function according to selected group
    for(int k=0 ; k< ctx->group2ns[group]->count; k++) {
        int ns = ctx->group2ns[group]->data[k];
        // note: this selects some nesses that aren't used, but take the extra work
        // instead of accessing the guards to verify the work
        s->nes_score[ns] -= ctx->group_score[group];
        HRE_ASSERT (s->nes_score[ns] >= 0, "FAILURE! NS: %s%d, group: %d", ns < ctx->nguards ? "+" : "-", ns, group);
    }
}

/**
 * Mark a group selected, update counters and NS scores.
 */
static inline bool
select_group (por_context* ctx, search_context_t *s, int group)
{
    if (s->emit_status[group] & (ES_QUEUED | ES_VISITED)) { // already selected
        Debugf ("(%d), ", group);
        return false;
    }
    s->emit_status[group] |= ES_QUEUED;

    update_ns_scores (ctx, s, group);

    // and add to work array and update counts
    int visible = is_visible (ctx, group);
    if (ctx->group_status[group] & GS_DISABLED) {
        s->work_disabled->data[s->work_disabled->count++] = group;
        s->score_disabled += 1;
    } else {
        s->work_enabled->data[s->work_enabled->count++] = group;
        s->score_vis_en += visible;
        s->enabled->data[s->enabled->count++] = group;
    }
    s->score_visible += visible;
    Debugf ("%d, ", group);
    return true;
}

static inline int
emit_all (por_context *ctx, ci_list *list, prov_t *provctx, int *src)
{
    int c = 0;
    for (int z = 0; z < list->count; z++) {
        int i = list->data[z];
        c += GBgetTransitionsLong (ctx->parent, i, src, hook_cb, provctx);
    }
    return c;
}

static inline int
emit_new (por_context *ctx, ci_list *list, prov_t *provctx, int *src)
{
    beam_t             *beam = (beam_t *) ctx->alg;
    search_context_t   *s = beam->search[0];
    int c = 0;
    for (int z = 0; z < list->count; z++) {
        int i = list->data[z];
        if (s->emit_status[i] & ES_EMITTED) continue;
        s->emit_status[i] |= ES_EMITTED;
        s->emitted->data[s->emitted->count++] = i;
        c += GBgetTransitionsLong (ctx->parent, i, src, hook_cb, provctx);
    }
    return c;
}

/**
 * Based on the heuristic and find the cheapest NS (NES/NDS) for a disabled
 * group.
 */
static inline int
beam_cheapest_ns (por_context* ctx, search_context_t *s, int group, int *cost)
{
    *cost = INT32_MAX;
    int             n_guards = ctx->nguards;
    int             count = ctx->group_has[group]->count;
    int             selected_ns = -1;
    int             c = RANDOM ? clock() : 0;
    for (int i = 0; i < count; i++) {
        int index = i;
        if (RANDOM) index = (i + c) % count;
        int ns = ctx->group_has[group]->data[index];
        if (!NO_HEUR && s->nes_score[ns] >= *cost) continue;

        // check guard status for ns (nes for disabled and nds for enabled):
        if ((ns < n_guards && (ctx->label_status[ns] == 0))  ||
            (ns >= n_guards && (ctx->label_status[ns-n_guards] != 0)) ) {

            selected_ns = ns;
            *cost = s->nes_score[ns];
            if (NO_HEUR || *cost == 0) return selected_ns; // can't improve
        }
    }
    return selected_ns;
}

static inline bool
select_all (por_context* ctx, search_context_t *s, ci_list *list)
{
    bool added_new = false;
    for (int i = 0; i < list->count; i++) {
        int group = list->data[i];
        added_new |= select_group (ctx, s, group);
    }
    return added_new;
}

static inline int
beam_is_key (por_context *ctx, search_context_t *s, int group, int max_score)
{
    int                 score = 0;
    for (int t = 0; t < ctx->dna_diff[group]->count && score < max_score; t++) {
        int tt = ctx->dna_diff[group]->data[t];
        int fresh = (s->emit_status[tt] & ES_QUEUED) == 0;
        score += fresh ? ctx->group_score[tt] : 0;
    }
    return score;
}

static void
beam_add_all_for_enabled (por_context *ctx, search_context_t *s, int group)
{

    if (POR_WEAK == WEAK_NONE && USE_MC) {
        // include all never coenabled transitions without searching from them!

        for (int i = 0; i < ctx->group_nce[group]->count; i++) {
            int nce = ctx->group_nce[group]->data[i];
            HREassert (ctx->group_status[nce] & GS_DISABLED, "Error in maybe-coenabled relation: %d - %d are coenabled.", group, nce);

            if (s->emit_status[nce] & ES_VISITED) continue;
            if (s->emit_status[nce] & ES_QUEUED) {
                s->emit_status[nce] |= ES_VISITED; // skip in search
                continue;
            }

            update_ns_scores (ctx, s, nce);
            s->score_disabled += 1;
            s->score_visible += is_visible (ctx, nce);
            s->emit_status[nce] |= ES_VISITED | ES_QUEUED; // skip in search
        }
    }

    // V proviso only for LTL
    int             vis;
    if ((PINS_LTL || SAFETY) && (vis = ctx->visible->set[group])) {
        if (NO_V) { // Use Peled's stronger visibility proviso:
            s->enabled->count = ctx->enabled_list->count; // selects all enabled
            return;
        }
        Debugf ("visible=[ ");

        bool            nes = (vis & (1 << VISIBLE_NES)) != 0;
        bool            nds = (vis & (1 << VISIBLE_NDS)) != 0;
        if (NO_DYN_VIS || nes == nds) { // visible & group | visible & nes & nds
            select_all (ctx, s, ctx->visible->lists[VISIBLE]);
        } else {
            int newNDS = ctx->visible_nes->set[group] ^ s->visible_nds; // g in NES ==> NDS C Ts
            int newNES = ctx->visible_nds->set[group] ^ s->visible_nes; // g in NDS ==> NES C Ts
            if (newNES || newNDS) {
                for (size_t i = 0; i < ctx->visible_nds->types; i++) {
                    if ((1 << i) & newNDS) {
                        select_all (ctx, s, ctx->visible_nds->lists[i]);
                    }
                    if ((1 << i) & newNES) {
                        select_all (ctx, s, ctx->visible_nes->lists[i]);
                    }
                }
                s->visible_nds |= newNDS;
                s->visible_nes |= newNES;
            }
        }
        Debugf ("] ");
    }

    ci_list **accords = POR_WEAK ? ctx->not_left_accords : ctx->not_accords;
    for (int j = 0; j < accords[group]->count; j++) {
        int dependent_group = accords[group]->data[j];
        select_group (ctx, s, dependent_group);
    }

    if (POR_WEAK == WEAK_VALMARI) {
        Debugf ("weak=[ ");
        int         cost_ndss = beam_is_key (ctx, s, group, INT32_MAX);
        int         cost_nes  = INT32_MAX;
        int         selected_ns = -1;
        if (cost_ndss != 0)
            selected_ns = beam_cheapest_ns (ctx, s, group, &cost_nes); //maybe -1
        if (cost_ndss <= cost_nes) {
            if (s->has_key == KEY_VISIBLE || s->has_key == KEY_ANY)
                s->has_key = !is_visible(ctx, group) ? KEY_INVISIBLE : KEY_VISIBLE;
            if (cost_ndss > 0) {
                select_all (ctx, s, ctx->dna_diff[group]);
            }
        } else if (cost_nes > 0){
            select_all (ctx, s, ctx->ns[selected_ns]);
        }
        Debugf ("] ");
    }
}

static inline int
beam_cmp (search_context_t *s1, search_context_t *s2)
{
    //if (s1->score != s2->score)
        return s1->enabled->count - s2->enabled->count;
    //int a = s1->disabled_score + s1->score- s1->work_disabled;
    //int b = s2->disabled_score + s2->score - s2->work_disabled;
    //return (a << 15) / s1->work_disabled - (b << 15) / s2->work_disabled;
}

bool check_sort (por_context *ctx) {
    beam_t             *beam = (beam_t *) ctx->alg;
    for (int i = 0; i < beam->beam_used; i++)
        if (i > 0 && beam_cmp(beam->search[i-1], beam->search[i]) > 0) return false;
    return true;
}

/**
 * Sorts BEAM search contexts.
 */
static inline bool
beam_sort (por_context *ctx)
{
    beam_t             *beam = (beam_t *) ctx->alg;
    search_context_t   *s = beam->search[0];

    int bubble = 1;
    while (bubble < beam->beam_used && beam_cmp(s, beam->search[bubble]) > 0) {
        beam->search[bubble - 1] = beam->search[bubble];
        bubble++;
    }
    beam->search[bubble - 1] = s;

    // didn't move and no more work
    if (beam->search[0] == s && s->work_disabled->count == 0 && s->work_enabled->count == 0) {
        Debugf ("bailing out, no more work (selected: %d, en-count: %d)\n", s->idx, s->enabled->count);
        HRE_ASSERT (check_sort(ctx), "unsorted!");
        return false;
    }
    return true;
}

/**
 * Analyze NS is the function called to find the smallest persistent set
 * It builds stubborn sets in multiple search contexts, by using a beam
 * search it switches search context each time the score of the current context
 * (based on heuristic function h(x) (nes_score)) isn't the lowest score anymore
 *
 * Assumes sorted beam contexts array
 */
static inline void
beam_search (por_context *ctx)
{
    if (ctx->enabled_list->count <= 1) return;
    beam_t             *beam = (beam_t *) ctx->alg;

    Debugf ("BEAM search (re)started (enabled: %d)\n", ctx->enabled_list->count);
    do {
        search_context_t   *s = beam->search[0];
        // quit the current search when all transitions are included
        if (s->enabled->count >= ctx->enabled_list->count) {
            s->work_enabled->count = s->work_disabled->count = 0;
            Debugf ("BEAM-%d abandoning search |ss|=|en|=%d\n", s->idx, ctx->enabled_list->count);
            break;
        }

        if (!s->initialized) { // init search (expensive)
            memset(s->emit_status, 0, sizeof(char[ctx->ngroups]));
            if (!NO_HEUR) {
                memcpy(s->nes_score, ctx->nes_score, sizeof(int[NS_SIZE(ctx)]));
            }
            select_group (ctx, s, s->group);
            s->initialized = 1;
            Debugf ("BEAM-%d Initializing scores for group %d\n", s->idx, s->group);
        }

        // while there are no enabled, but some disabled transitions:
        int             group;
        while (s->work_enabled->count == 0 && s->work_disabled->count > 0) {
            group = s->work_disabled->data[--s->work_disabled->count];
            HREassert (s->emit_status[group] & ES_QUEUED);
            if (s->emit_status[group] & ES_VISITED) continue;
            s->emit_status[group] |= ES_VISITED;

            Debugf ("BEAM-%d investigating group %d (disabled) --> ", s->idx, group);
            int         cost;
            int         selected_ns = beam_cheapest_ns (ctx, s, group, &cost);
            HREassert (selected_ns != -1, "No NES found for group %d", group);
            if (cost != 0) { // only if cheapest NES has cost
                select_all (ctx, s, ctx->ns[selected_ns]);
            }
            Debugf (" (ns %d (%s))\n", selected_ns % ctx->nguards,
                    selected_ns < ctx->nguards ? "disabled" : "enabled");
        }

        // if the current search context has enabled transitions, handle all of them
        while (s->work_enabled->count > 0 && beam_cmp(s, beam->search[1]) <= 0) {
            group = s->work_enabled->data[--s->work_enabled->count];
            HREassert (s->emit_status[group] & ES_QUEUED);
            if (s->emit_status[group] & ES_VISITED) continue;
            s->emit_status[group] |= ES_VISITED;

            Debugf ("BEAM-%d investigating group %d (enabled) --> ", s->idx, group);
            beam_add_all_for_enabled (ctx, s, group); // add DNA
            Debugf ("\n");
        }
    } while (beam_sort(ctx));
}

static inline int
beam_min_key_group (por_context* ctx, search_context_t *s, bool invisible)
{
    size_t min_score = INT32_MAX;
    int min_group = -1;
    for (int i = 0; i < s->enabled->count; i++) {
        int group = s->enabled->data[i];
        if (invisible && is_visible(ctx, group)) continue;

        size_t score = beam_is_key (ctx, s, group, min_score);
        if (score == 0) {
            Debugf ("BEAM %d has key: %d (all NDSs already included)\n", s->idx, group);
            return -1;
        }
        if (score < min_score) {
            min_score = score;
            min_group = group;
        }
    }
    return min_group;
}

static inline int
beam_min_invisible_group (por_context* ctx, search_context_t *s)
{
    size_t min_score = INT32_MAX;
    int min_group = -1;
    for (int i = 0; i < ctx->enabled_list->count; i++) {
        int group = ctx->enabled_list->data[i];
        if ((s->emit_status[group] & ES_QUEUED) || is_visible(ctx, group)) continue;

        size_t              score = 0;
        ci_list **accords = POR_WEAK ? ctx->not_left_accords : ctx->not_accords;
        for (int j = 0; j < accords[group]->count; j++) {
            int dependent = accords[group]->data[j];
            int fresh = (s->emit_status[dependent] & ES_QUEUED) == 0;
            score += fresh ? ctx->group_score[dependent] : 0;
        }
        if (score == 0) return group;
        if (score < min_score) {
            min_score = score;
            min_group = group;
        }
    }
    HREassert (min_group != -1, "No min invisible group found. %d candidates", ctx->enabled_list->count);
    return min_group;
}

static inline void
beam_ensure_invisible_and_key (por_context* ctx)
{
    if (!POR_WEAK && !(SAFETY || PINS_LTL)) return;

    Debugf ("ADDING KEY <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<\n");
    beam_t             *beam = (beam_t *) ctx->alg;
    while (true) {
        search_context_t   *s = beam->search[0];
        if (s->enabled->count == ctx->enabled_list->count || ctx->enabled_list->count <= 1) {
            Debugf ("BEAM %d needs no (invisible) key\n", s->idx);
            break;
        }

        bool need_invisible = (SAFETY || PINS_LTL) &&
                                 ctx->visible_enabled != ctx->enabled_list->count;
        if (need_invisible && s->has_key == KEY_INVISIBLE) {
            Debugf ("BEAM %d has invisible key\n", s->idx);
            break;
        }
        if (!need_invisible && s->has_key != KEY_NONE) {
            Debugf ("BEAM %d has key (%s)\n", s->idx, keyNames[s->has_key]);
            break;
        }

        bool has_invisible = s->score_vis_en != s->enabled->count;
        if (need_invisible && !has_invisible) {
            int i_group = beam_min_invisible_group (ctx, s);
            Debugf ("BEAM %d adding invisible key: %d", s->idx, i_group);
            select_group (ctx, s, i_group);
            if (POR_WEAK) { // make it also key
                Debugf (" +strong[ ");
                select_all (ctx, s, ctx->dna_diff[i_group]);
                Debugf ("]\n");
            }
            s->has_key = KEY_INVISIBLE;
        } else if (POR_WEAK) {
            int key_group = -1;
            if (need_invisible && has_invisible) {
                key_group = beam_min_key_group (ctx, s, true);
                s->has_key = KEY_INVISIBLE;
            } else { // only visible available:
                key_group = beam_min_key_group (ctx, s, false);
                s->has_key = KEY_VISIBLE;
            }
            if (key_group != -1) {
                Debugf ("BEAM %d making %d key: ", s->idx, key_group);
                select_all (ctx, s, ctx->dna_diff[key_group]);
                Debugf (".\n");
            }
        } else {
            s->has_key = KEY_ANY;
            Debugf ("BEAM %d key is available and I proviso met\n", s->idx);
            break;
        }

        beam_sort (ctx);
        beam_search (ctx); // may switch context
    }
    Debugf (">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n");
}

/**
 * Visible transtions:      Tv
 * Invisible transtions:    Ti = T \ Tv
 * Stubborn set:            Ts
 * Keys in stubborn set:    Tk
 *
 * V  = Tv n Ts != {}     ==>  Tv C Ts
 * L1 = Ti n en(s) != {}  ==>  Tk n Ti != {}
 * L2 = s closes cycle    ==>  Tv C Ts
 *
 * Whether s closes a cycle is determined by the search algorithm, which may
 * employ DFS with the condition s in stack, or a more complicated search
 * algorithm such as the color proviso.
 *
 * Premature check whether L1 and L2 hold, i.e. before ignoring condition is
 * known (the premise of L2).
 * For safety (!PINS_LTL), we limit the proviso to L2. For details see
 * implementation notes in the header.
 */
static inline int
check_L2_proviso (por_context* ctx, search_context_t *s)
{ // all visible selected: satisfies (the conclusion of) L2
    return s->score_visible == bms_count(ctx->visible,VISIBLE);
}

static inline void
beam_enforce_L2 (por_context* ctx)
{
    beam_t             *beam = (beam_t *) ctx->alg;
    search_context_t   *s1 = beam->search[0];
    while (true) {
        search_context_t   *s = beam->search[0];
        if (check_L2_proviso(ctx, s)) {
            if (s == s1) {
                Debugf ("BEAM %d has all visibles and finished search (previously emitted BEAM %d)\n", s->idx, s1->idx);
                return;
            } else { // if context changed, it should include emitted transitions:
                Debugf ("BEAM %d adding previously emitted from BEAM %d: ", s->idx, s1->idx);
                bool new = select_all(ctx, s, s1->emitted);
                Debugf (".\n");
                if (!new) return;
            }
        } else {
            Debugf ("BEAM %d adding all visibles: ", s->idx);
            bool new = select_all (ctx, s, ctx->visible->lists[VISIBLE]);
            Debugf (".\n");
            HREassert (new);
        }
        beam_sort (ctx);
        beam_search (ctx); // may switch context
        beam_ensure_invisible_and_key (ctx);
    }
}

static inline int
beam_emit (por_context* ctx, int* src, TransitionCB cb, void* uctx)
{
    // selected in winning search context
    beam_t             *beam = (beam_t *) ctx->alg;
    // if no enabled transitions, return directly
    if (beam->beam_used == 0) return 0;
    search_context_t   *s = beam->search[0];
    int emitted = 0;
    // if the score is larger then the number of enabled transitions, emit all
    if (s->enabled->count >= ctx->enabled_list->count || ctx->enabled_list->count <= 1) {
        // return all enabled
        prov_t provctx = {cb, uctx, 0, 0, 1};
        emitted = emit_all (ctx, ctx->enabled_list, &provctx, src);
    } else if (!PINS_LTL && !SAFETY) { // deadlocks are easy:
        prov_t provctx = {cb, uctx, 0, 0, 1};
        emitted = emit_new (ctx, s->enabled, &provctx, src);
    } else { // otherwise enforce that all por_proviso flags are true
        prov_t provctx = {cb, uctx, 0, 0, 0};

        search_context_t   *s = beam->search[0];
        provctx.force_proviso_true = !NO_L12 && !NO_V && check_L2_proviso(ctx, s);
        emitted = emit_new (ctx, s->enabled, &provctx, src);

        // emit more if we need to fulfill a liveness / safety proviso
        if ( ( PINS_LTL && provctx.por_proviso_false_cnt != 0) ||
             (!PINS_LTL && provctx.por_proviso_true_cnt  == 0) ) {

            provctx.force_proviso_true = 1;
            Debugf ("BEAM %d may cause ignoring\n", s->idx);
            if (!NO_L12 && !NO_V && ctx->visible_enabled < ctx->enabled_list->count - 1) {
                // enforce L2 (include all visible transitions)
                beam_enforce_L2 (ctx);
                if (beam->search[0]->enabled->count == ctx->enabled_list->count) {
                    emitted += emit_new (ctx, ctx->enabled_list, &provctx, src);
                } else {
                    emitted += emit_new (ctx, s->enabled, &provctx, src);
                }
            } else {
                Debugf ("BEAM %d emitting all\n", s->idx);
                emitted += emit_new (ctx, ctx->enabled_list, &provctx, src);
            }
        }
    }
    return emitted;
}

/**
 * For each state, this function sets up the current guard values etc
 * This setup is then reused by the analysis function
 */
static void
beam_setup (model_t model, por_context* ctx, int* src)
{
    por_init_transitions (model, ctx, src);
    por_transition_costs (ctx);

    beam_t         *beam = ctx->alg;
    int             c = RANDOM ? clock() : 0;
    beam->beam_used = ctx->enabled_list->count;
    if (MAX_BEAM != -1 && beam->beam_used > MAX_BEAM)
        beam->beam_used = MAX_BEAM;
    Debugf ("Initializing searches: ");
    for (int i = 0; i < beam->beam_used; i++) {
        int enabled = i;
        if (RANDOM) enabled = (enabled + c) % beam->beam_used;
        int group = ctx->enabled_list->data[enabled];

        search_context_t *search = beam->search[i];
        search->has_key = KEY_NONE;
        search->visible_nds = 0;
        search->visible_nes = 0;
        search->work_disabled->count = 0;
        search->work_enabled->count = 0;
        search->enabled->count = 0;
        search->emitted->count = 0;
        search->score_disabled = 0;
        search->initialized = 0;
        search->score_visible = 0;
        search->score_vis_en = 0;
        search->idx = i;
        search->group = group;
        Debugf ("BEAM-%d=%d, ", i, group);
    }
    Debugf ("\n");
}

int
beam_search_all (model_t self, int *src, TransitionCB cb, void *user_context)
{
    por_context* ctx = ((por_context*)GBgetContext(self));
    beam_setup (self, ctx, src);
    beam_search (ctx);
    beam_ensure_invisible_and_key (ctx);
    return beam_emit (ctx, src, cb, user_context);
}

search_context_t *
create_search_context (por_context *ctx)
{
    search_context_t *search;
    search = RTmallocZero(sizeof(search_context_t));
    search->emit_status = RTmallocZero(sizeof(char[ctx->ngroups]));
    search->work_enabled = ci_create(ctx->ngroups);
    search->work_disabled = ci_create(ctx->ngroups);
    search->enabled = ci_create(ctx->ngroups);
    search->emitted = ci_create(ctx->ngroups);
    search->nes_score = RTmallocZero(sizeof(int[NS_SIZE(ctx)]));
    return search;
}

/**
 * Function used to setup the beam search
 */
beam_t *
beam_create_context (por_context *ctx)
{
    if (NO_HEUR_BEAM) NO_HEUR = MAX_BEAM = 1;
    if (!RANDOM && (NO_HEUR || MAX_BEAM != -1)) {
        if (__sync_bool_compare_and_swap(&RANDOM, 0, 1)) { // static variable
            Warning (info, "Using random selection instead of heuristics / multiple Beam searches.");
        }
    }

    beam_t         *beam = RTmallocZero(sizeof(beam_t));
    beam->search = RTmallocZero(sizeof(search_context_t *[ctx->ngroups]));
    for (int i = 0 ; i < ctx->ngroups; i++) {
        beam->search[i] = create_search_context (ctx);
    }
    return beam;
}

bool
beam_is_stubborn (por_context *ctx, int group)
{
    beam_t             *beam = (beam_t *) ctx->alg;
    search_context_t   *s = beam->search[0];
    return s->enabled->count >= ctx->enabled_list->count ||
          (s->emit_status[group] & ES_QUEUED);
}

