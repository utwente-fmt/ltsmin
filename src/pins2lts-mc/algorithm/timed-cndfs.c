#include <hre/config.h>

#include <pins2lts-mc/algorithm/timed-cndfs.h>

typedef struct cta_counter_s {
    ta_counter_t        timed;
    size_t              sub_cyan;       // subsumed by cyan
    size_t              sub_red;        // subsumed by red
} cta_counter_t;

typedef struct ta_alg_local_s {
    cndfs_alg_local_t   cndfs;
    lm_loc_t            lloc;       // Lattice location (serialized by state-info)
    fset_t             *cyan;       // Cyan states
    //fset_t             *pink;       // Pink states
    fset_t             *cyan2;      // Cyan states
    lm_loc_t            added_at;   // successor is added at location
    lm_loc_t            last;       // last tombstone location
    state_info_t       *successor;  // current successor state
    int                 subsumes;   // successor subsumes a state in LMAP
    int                 done;
    cta_counter_t       counters;   // statistics counters
} ta_alg_local_t;

typedef struct ta_reduced_s {
    cndfs_reduced_t     cndfs;
    cta_counter_t       counters;
    float               waittime;
} ta_reduced_t;

typedef struct ta_alg_shared_s {
    alg_shared_t        cndfs;
    lm_t               *lmap;
} ta_alg_shared_t;

//static const lm_status_t LM_WHITE = 0;
static const lm_status_t LM_RED  = 1;
static const lm_status_t LM_BLUE = 2;
static const lm_status_t LM_BOTH = 1 | 2;

lm_cb_t
ta_cndfs_covered (void *arg, lattice_t l, lm_status_t status, lm_loc_t lm)
{
    wctx_t             *ctx = (wctx_t *) arg;
    ta_alg_local_t     *ta_loc = (ta_alg_local_t *) ctx->local;
    lm_status_t         color = (lm_status_t)ta_loc->subsumes;
    if ( (status & color) && (ta_loc->successor->lattice == l) ) {
        ta_loc->done = 1;
        return LM_CB_STOP;
    }
    int *succ_l = (int *) &ta_loc->successor->lattice;
    if (UPDATE != 0 && (color & status & LM_RED)) {
        if ( GBisCoveredByShort(ctx->model, succ_l, (int*)&l) ) {
            ta_loc->done = 1;
            ta_loc->counters.sub_red++; // count (strictly) subsumed by reds
            return LM_CB_STOP;
        }
    }

    return LM_CB_NEXT;
    (void) lm;
}

int
ta_cndfs_subsumed (wctx_t *ctx, state_info_t *state, lm_status_t color)
{
    ta_alg_local_t     *ta_loc = (ta_alg_local_t *) ctx->local;
    ta_alg_shared_t    *shared = (ta_alg_shared_t*) ctx->run->shared;
    ta_loc->subsumes = color;
    ta_loc->done = 0;
    ta_loc->successor = state;
    if (NONBLOCKING) {
        lm_iterate (shared->lmap, state->ref, ta_cndfs_covered, ctx);
    } else {
        lm_lock (shared->lmap, state->ref);
        lm_iterate (shared->lmap, state->ref, ta_cndfs_covered, ctx);
        lm_unlock (shared->lmap, state->ref);
    }
    return ta_loc->done;
}

lm_cb_t
ta_cndfs_spray (void *arg, lattice_t l, lm_status_t status, lm_loc_t lm)
{
    wctx_t             *ctx = (wctx_t *) arg;
    ta_alg_local_t     *ta_loc = (ta_alg_local_t *) ctx->local;
    ta_alg_shared_t    *shared = (ta_alg_shared_t*) ctx->run->shared;
    ta_counter_t       *cnt = (ta_counter_t *) &ta_loc->counters;
    lm_status_t         color = (lm_status_t)ta_loc->subsumes;

    if (UPDATE != 0) {
        int *succ_l = (int *) &ta_loc->successor->lattice;
        if ( ((status & color) && ta_loc->successor->lattice == l) ||
             ((status & LM_RED) &&
                    GBisCoveredByShort(ctx->model, succ_l, (int*)&l)) ) {
            ta_loc->done = 1;
            if (color & LM_BLUE) // only red marking should continue to remove blue states
                return LM_CB_STOP;
        } else if ((color & LM_RED)) { // remove subsumed blue and red states
            if ( GBisCoveredByShort(ctx->model, (int*)&l, succ_l) ) {
                lm_delete (shared->lmap, lm);
                ta_loc->last = (LM_NULL_LOC == ta_loc->last ? lm : ta_loc->last);
                cnt->deletes++;
            }
        }
    } else {
        if ( ta_loc->successor->lattice == l ) {
            if ((status & color) == 0)
                lm_set_status (shared->lmap, lm, status | color);
            ta_loc->done = 1;
            return LM_CB_STOP;
        }
    }

    return LM_CB_NEXT;
}

lm_cb_t
ta_cndfs_spray_nb (void *arg, lattice_t l, lm_status_t status, lm_loc_t lm)
{
    wctx_t             *ctx = (wctx_t *) arg;
    ta_alg_local_t     *ta_loc = (ta_alg_local_t *) ctx->local;
    ta_counter_t       *cnt = (ta_counter_t *) &ta_loc->counters;
    ta_alg_shared_t    *shared = (ta_alg_shared_t*) ctx->run->shared;
    lm_status_t         color = (lm_status_t)ta_loc->subsumes;
    lattice_t           lattice = ta_loc->successor->lattice;

    if (UPDATE != 0) {
        int *succ_l = (int *)&lattice;
        if ( ((status & color) && ta_loc->successor->lattice == l) ||
             ((status & LM_RED) &&
                    GBisCoveredByShort(ctx->model, succ_l, (int*)&l)) ) {
            ta_loc->done = 1;
            if (color & LM_BLUE) // only red marking should continue to remove blue states
                return LM_CB_STOP;
        } else if (color & LM_RED) { // remove subsumed blue and red states
            if ( GBisCoveredByShort(ctx->model, (int*)&l, succ_l) ) {
                if (!ta_loc->done) {
                    if (!lm_cas_update (shared->lmap, lm, l, status, lattice, color)) {
                        l = lm_get (shared->lmap, lm);
                        if (l == LM_NULL_LATTICE) // deleted
                            return LM_CB_NEXT;
                        status = lm_get_status (shared->lmap, lm);
                        return ta_cndfs_spray_nb (arg, l, status, lm); // retry
                    } else {
                        ta_loc->done = 1;
                    }
                } else {                            // delete second etc
                    lm_cas_delete (shared->lmap, lm, l, status);
                    cnt->deletes++;
                }
            }
        }
    } else {
        if ( ta_loc->successor->lattice == l ) {
            if (!lm_cas_update (shared->lmap, lm, l, status, lattice, status|color)) {
                l = lm_get (shared->lmap, lm);
                if (l == LM_NULL_LATTICE) // deleted
                    return LM_CB_NEXT;
                status = lm_get_status (shared->lmap, lm);
                return ta_cndfs_spray_nb (arg, l, status, lm); // retry
            } else {
                ta_loc->done = 1;
                return LM_CB_STOP;
            }
        }
    }

    return LM_CB_NEXT;
}

static inline int
ta_cndfs_mark (wctx_t *ctx, state_info_t *state, lm_status_t color)
{
    ta_alg_local_t     *ta_loc = (ta_alg_local_t *) ctx->local;
    ta_counter_t       *cnt = (ta_counter_t *) &ta_loc->counters;
    ta_alg_shared_t    *shared = (ta_alg_shared_t*) ctx->run->shared;
    lm_loc_t            last;
    ta_loc->successor = state;
    ta_loc->subsumes = color;
    ta_loc->done = 0;
    ta_loc->last = LM_NULL_LOC;

    if (NONBLOCKING) {
        last = lm_iterate (shared->lmap, state->ref, ta_cndfs_spray_nb, ctx);
        if (!ta_loc->done) {
            lm_insert_from_cas (shared->lmap, state->ref, state->lattice, color, &last);
            cnt->inserts++;
        }
    } else {
        lm_lock (shared->lmap, state->ref);
        last = lm_iterate (shared->lmap, state->ref, ta_cndfs_spray, ctx);
        if (!ta_loc->done) {
            last = (LM_NULL_LOC == ta_loc->last ? last : ta_loc->last);
            lm_insert_from (shared->lmap, state->ref, state->lattice, color, &last);
            cnt->inserts++;
        }
        lm_unlock (shared->lmap, state->ref);
    }
    return ta_loc->done;
}

/* maintain on-stack states */

static inline int
ta_cndfs_has_state (fset_t *table, state_info_t *s, bool add_if_absent)
{
    struct val_s        state = { s->ref, s->lattice };
    int res = fset_find (table, NULL, &state, NULL, add_if_absent);
    HREassert (res != FSET_FULL, "Cyan table full");
    return res;
}

static inline void
ta_cndfs_remove_state (fset_t *table, state_info_t *s)
{
    struct val_s        state = { s->ref, s->lattice };
    int success = fset_delete (table, NULL, &state);
    HREassert (success, "Could not remove lattice state (%zu,%zu) from set", s->ref, s->lattice);
}

/* maintain a linked list of cyan states with same concrete part on the stack */

static inline void
ta_cndfs_next (wctx_t *ctx, raw_data_t stack_loc, state_info_t *s)
{
    ta_alg_local_t     *loc = (ta_alg_local_t *) ctx->local;
    void               *data;
    hash32_t            hash = ref_hash (s->ref);
    int res = fset_find (loc->cyan2, &hash, &s->ref, &data, true);
    HREassert (res != FSET_FULL, "Cyan2 table full");
    if (res) {
        loc->lloc = *(lm_loc_t*)data;
    } else {
        loc->lloc = LM_NULL_LOC;
    }
    state_info_serialize (s, stack_loc); // write previous pointer to stack
    *(raw_data_t *)data = stack_loc;     // write current pointer to hash map
}

static inline void
ta_cndfs_previous (wctx_t *ctx, state_info_t *s)
{
    ta_alg_local_t     *loc = (ta_alg_local_t *) ctx->local;
    hash32_t            hash = ref_hash (s->ref);
    if (loc->lloc == LM_NULL_LOC) {
        int res = fset_delete (loc->cyan2, &hash, &s->ref);
        HREassert (res, "state %zu not in Cyan2 table", s->ref);
        return;
    }
    void              *data;
    int res = fset_find (loc->cyan2, &hash, &s->ref, &data, false);
    HREassert (res != FSET_FULL, "Cyan2 table full");
    HREassert (res, "state %zu not in Cyan2 table", s->ref);
    *(lm_loc_t *)data = loc->lloc;      // write new current pointer to hash map
}

static inline bool
ta_cndfs_subsumes_cyan (wctx_t *ctx, state_info_t *s)
{
    alg_local_t        *loc = ctx->local;
    ta_alg_local_t     *ta_loc = (ta_alg_local_t *) ctx->local;
    hash32_t            hash = ref_hash (s->ref);
    void               *data;
    int res = fset_find (ta_loc->cyan2, &hash, &s->ref, &data, false);
    HREassert (res != FSET_FULL, "Cyan2 table full");
    ta_loc->lloc = *(lm_loc_t *)data;
    if (!res)
        return false;
    size_t              iteration = 0;
    while (ta_loc->lloc != LM_NULL_LOC) {
        state_info_deserialize (loc->seed, (raw_data_t)ta_loc->lloc); // writes to ta_loc->lloc!
        if (loc->seed->lattice == s->lattice) {
            return true;
        }
        if (GBisCoveredByShort(ctx->model, (int*)loc->seed->lattice, (int*)s->lattice)) {
            ta_loc->counters.sub_cyan++;
            return true;
        }
        iteration++;
    }
    return false;
}

static void
ta_cndfs_handle_nonseed_accepting (wctx_t *ctx)
{
    alg_local_t        *loc = ctx->local;
    cndfs_alg_local_t  *cndfs_loc = (cndfs_alg_local_t *) ctx->local;
    size_t              nonred, accs;
    nonred = accs = dfs_stack_size (cndfs_loc->out_stack);
    if (nonred) {
        loc->red.waits++;
        cndfs_loc->counters.rec += accs;
        RTstartTimer (cndfs_loc->timer);
        while ( nonred && !run_is_stopped(ctx->run) ) {
            nonred = 0;
            for (size_t i = 0; i < accs; i++) {
                raw_data_t state_data = dfs_stack_peek (cndfs_loc->out_stack, i);
                state_info_deserialize (ctx->state, state_data);
                if (!ta_cndfs_subsumed(ctx, ctx->state, LM_RED)) {
                    nonred++;
                }
            }
        }
        RTstopTimer (cndfs_loc->timer);
    }
    for (size_t i = 0; i < accs; i++)
        dfs_stack_pop (cndfs_loc->out_stack);
    size_t pre = dfs_stack_size (cndfs_loc->in_stack);
    while ( dfs_stack_size(cndfs_loc->in_stack) ) {
        raw_data_t state_data = dfs_stack_pop (cndfs_loc->in_stack);
        state_info_deserialize (ctx->state, state_data);
        ta_cndfs_mark (ctx, ctx->state, LM_RED);
        //remove_state (loc->pink, ctx->state);
    }
    if (pre)
        fset_clear (cndfs_loc->pink);
    //HREassert (fset_count(loc->pink) == 0, "Pink set not empty: %zu", fset_count(loc->pink));
    if (fset_count(cndfs_loc->pink) != 0)
        Warning (info, "Pink set not empty: %zu", fset_count(cndfs_loc->pink));
}

static inline bool
ta_cndfs_is_cyan (wctx_t *ctx, state_info_t *s, raw_data_t d, bool add_if_absent)
{
    ta_alg_local_t     *ta_loc = (ta_alg_local_t *) ctx->local;
    if (UPDATE == 1) {
        if (add_if_absent) { // BOTH stacks:
            bool result = ta_cndfs_subsumes_cyan (ctx, s);
            result = ta_cndfs_has_state(ta_loc->cyan, s, add_if_absent);
            if (!result && add_if_absent)
                ta_cndfs_next (ctx, d, ctx->state);
            return result;
        } else {
            return ta_cndfs_subsumes_cyan (ctx, s);
        }
    } else if (UPDATE == 2) {
        bool result = ta_cndfs_subsumes_cyan (ctx, s);
        if (!result && add_if_absent)
            ta_cndfs_next (ctx, d, ctx->state);
        return result;
    } else {
        return ta_cndfs_has_state(ta_loc->cyan, s, add_if_absent);
    }
}

static void
ta_cndfs_handle_red (void *arg, state_info_t *successor, transition_info_t *ti, int seen)
{
    wctx_t             *ctx = (wctx_t *) arg;
    alg_local_t        *loc = ctx->local;
    cndfs_alg_local_t  *cndfs_loc = (cndfs_alg_local_t *) ctx->local;
    /* Find cycle back to the seed */
    if ( ta_cndfs_is_cyan(ctx, successor, NULL, false) )
        ndfs_report_cycle (ctx, ctx->model, loc->stack, successor);
    if ( !ta_cndfs_has_state(cndfs_loc->pink, successor, false) //&&
         /*!ta_cndfs_subsumed(ctx, successor, LM_RED)*/ ) {
        raw_data_t stack_loc = dfs_stack_push (loc->stack, NULL);
        state_info_serialize (successor, stack_loc);
    }
    (void) ti; (void) seen;
}

static inline int
is_acc (wctx_t* ctx, state_info_t *si)
{
    return pins_state_is_accepting (ctx->model, state_info_state(si));
}

static void
ta_cndfs_handle_blue (void *arg, state_info_t *successor, transition_info_t *ti, int seen)
{
    wctx_t             *ctx = (wctx_t *) arg;
    alg_local_t        *loc = ctx->local;
    int cyan = ta_cndfs_is_cyan (ctx, successor, NULL, false);
    if (ecd && cyan && (is_acc(ctx, successor) || is_acc(ctx, ctx->state)) ) {
        /* Found cycle in blue search */
        ndfs_report_cycle (ctx, ctx->model, loc->stack, successor);
    }
    if ( all_red || !cyan ) {
        raw_data_t stack_loc = dfs_stack_push (loc->stack, NULL);
        state_info_serialize (successor, stack_loc);
        if (!seen) {
            loc->counters.accepting += is_acc(ctx, successor);
        }
    }
    (void) ti; (void) seen;
}

static inline void
ta_cndfs_explore_state_red (wctx_t *ctx)
{
    alg_local_t        *loc = ctx->local;
    work_counter_t     *cnt = &loc->red_work;
    dfs_stack_enter (loc->stack);
    increase_level (cnt);
    cnt->trans += permute_trans (ctx->permute, ctx->state, ta_cndfs_handle_red, ctx);
    check_counter_example (ctx, loc->stack, true);
    cnt->explored++;
    run_maybe_report (ctx->run, cnt, "[Red ] ");
}

static inline void
ta_cndfs_explore_state_blue (wctx_t *ctx)
{
    alg_local_t        *loc = ctx->local;
    work_counter_t     *cnt = ctx->counters;
    dfs_stack_enter (loc->stack);
    increase_level (cnt);
    cnt->trans += permute_trans (ctx->permute, ctx->state, ta_cndfs_handle_blue, ctx);
    check_counter_example (ctx, loc->stack, true);
    cnt->explored++;
    run_maybe_report1 (ctx->run, cnt, "[Blue] ");
}

/* ENDFS dfs_red */
static void
ta_cndfs_red (wctx_t *ctx, ref_t seed, lattice_t l_seed)
{
    alg_local_t        *loc = ctx->local;
    cndfs_alg_local_t  *cndfs_loc = (cndfs_alg_local_t *) ctx->local;
    size_t              seed_level = dfs_stack_nframes (loc->stack);
    while ( !run_is_stopped(ctx->run) ) {
        raw_data_t          state_data = dfs_stack_top (loc->stack);
        if (NULL != state_data) {
            state_info_deserialize (ctx->state, state_data);
            if ( !ta_cndfs_subsumed(ctx, ctx->state, LM_RED) &&
                 !ta_cndfs_has_state(cndfs_loc->pink, ctx->state, true) ) {
                dfs_stack_push (cndfs_loc->in_stack, state_data);
                if ( ctx->state->ref != seed && ctx->state->lattice != l_seed &&
                     pins_state_is_accepting(ctx->model, state_info_state(ctx->state)) )
                    dfs_stack_push (cndfs_loc->out_stack, state_data);
                ta_cndfs_explore_state_red (ctx);
            } else {
                if (seed_level == dfs_stack_nframes (loc->stack))
                    break;
                dfs_stack_pop (loc->stack);
            }
        } else { // backtrack
            dfs_stack_leave (loc->stack);
            HREassert (loc->red_work.level_cur != 0);
            loc->red_work.level_cur--;

            /* exit search if backtrack hits seed, leave stack the way it was */
            if (seed_level == dfs_stack_nframes(loc->stack))
                break;
            dfs_stack_pop (loc->stack);
        }
    }
}

/* ENDFS dfs_blue */
static void
ta_cndfs_blue (run_t *run, wctx_t *ctx)
{
    HREassert (run == ctx->run, "wrong worker context");
    alg_local_t            *loc = ctx->local;
    transition_info_t       ti = GB_NO_TRANSITION;
    ta_cndfs_handle_blue (ctx, ctx->initial, &ti, 0);
    ctx->counters->trans = 0; //reset trans count

    ta_alg_local_t          *ta_loc = (ta_alg_local_t *) ctx->local;
    lm_status_t BLUE_CHECK = (UPDATE != 0 ? LM_BOTH : LM_BLUE);
    while ( !run_is_stopped(ctx->run) ) {
        raw_data_t          state_data = dfs_stack_top (loc->stack);
        if (NULL != state_data) {
            state_info_deserialize (ctx->state, state_data);
            if ( !ta_cndfs_subsumed(ctx, ctx->state, BLUE_CHECK) &&
                 !ta_cndfs_is_cyan(ctx, ctx->state, state_data, true) ) {
                if (all_red)
                    bitvector_set (&loc->stackbits, ctx->counters->level_cur);
                ta_cndfs_explore_state_blue (ctx);
            } else {
                if ( all_red && ctx->counters->level_cur != 0 &&
                     !ta_cndfs_subsumed(ctx, ctx->state, LM_RED) )
                    bitvector_unset (&loc->stackbits, ctx->counters->level_cur - 1);
                dfs_stack_pop (loc->stack);
            }
        } else { // backtrack
            if (0 == dfs_stack_nframes(loc->stack)) {
                if (fset_count(ta_loc->cyan) != 0)
                    Warning (info, "Cyan set not empty: %zu", fset_count(ta_loc->cyan));
                break;
            }
            dfs_stack_leave (loc->stack);
            ctx->counters->level_cur--;
            /* call red DFS for accepting states */
            state_data = dfs_stack_top (loc->stack);
            state_info_deserialize (ctx->state, state_data);
            if (UPDATE == 1) {
                ta_cndfs_previous (ctx, ctx->state);
                ta_cndfs_remove_state (ta_loc->cyan, ctx->state);
            } else if (UPDATE == 2) {
                ta_cndfs_previous (ctx, ctx->state);
            } else {
                ta_cndfs_remove_state (ta_loc->cyan, ctx->state);
            }
            /* Mark state BLUE on backtrack */
            ta_cndfs_mark (ctx, ctx->state, LM_BLUE);
            if ( all_red && bitvector_is_set(&loc->stackbits, ctx->counters->level_cur) ) {
                /* all successors are red */
                //permute_trans (loc->permute, ctx->state, check, ctx);
                int red = ta_cndfs_mark (ctx, ctx->state, LM_RED);
                loc->counters.allred += red;
                loc->red.allred += 1 - red;
            } else if ( pins_state_is_accepting(ctx->model, state_info_state (ctx->state)) ) {
                ta_cndfs_red (ctx, ctx->state->ref, ctx->state->lattice);
                ta_cndfs_handle_nonseed_accepting (ctx);
            } else if (all_red && ctx->counters->level_cur > 0 &&
                       !ta_cndfs_subsumed(ctx, ctx->state, LM_RED) ) {
                /* unset the all-red flag (only for non-initial nodes) */
                bitvector_unset (&loc->stackbits, ctx->counters->level_cur - 1);
            }
            dfs_stack_pop (loc->stack);
        }
    }
}

void
ta_cndfs_local_init   (run_t *run, wctx_t *ctx)
{
    ta_alg_local_t         *loc = RTmallocZero (sizeof(ta_alg_local_t));
    ctx->local = (alg_local_t *) loc;
    loc->cyan = fset_create (sizeof(struct val_s), 0, 10, 28);
    loc->cyan2= fset_create (sizeof(ref_t), sizeof(void *), 10, 20);

    // Extend state info with a lattice location
    state_info_add_simple (ctx->state, sizeof(lm_loc_t), &loc->lloc);
    // Extend state info with a lattice location
    state_info_add_simple (ctx->initial, sizeof(lm_loc_t), &loc->lloc);
    state_info_t       *si_perm = permute_state_info(ctx->permute);
    state_info_add_simple (si_perm, sizeof(lm_loc_t), &loc->lloc);

    cndfs_local_setup (run, ctx);
    state_info_add_simple (((alg_local_t*)loc)->seed, sizeof(lm_loc_t), &loc->lloc);
}

void
ta_cndfs_global_init   (run_t *run, wctx_t *ctx)
{
    (void) run; (void) ctx;
}

void
ta_cndfs_destroy   (run_t *run, wctx_t *ctx)
{
    (void) run; (void) ctx;
}

void
ta_cndfs_destroy_local   (run_t *run, wctx_t *ctx)
{
    cndfs_local_deinit (run, ctx);
}

void
ta_cndfs_print_stats   (run_t *run, wctx_t *ctx)
{
    cndfs_print_stats (run, ctx);

    ta_alg_shared_t    *shared = (ta_alg_shared_t*) run->shared;
    ta_print_stats   (shared->lmap, ctx);

    ta_alg_local_t         *ta_loc = (ta_alg_local_t *) ctx->local;
    ta_reduced_t           *reduced = (ta_reduced_t *) run->reduced;
    Warning (infoLong, "Red subsumed: %zu", ta_loc->counters.sub_red);
    Warning (infoLong, "Cyan subsumed: %zu", ta_loc->counters.sub_cyan);
    Warning (infoLong, "Wait time: %.1f sec", reduced->waittime);
}

static void
add_results (cta_counter_t *res, cta_counter_t *cnt)
{
    ta_add_results ((ta_counter_t *)res, (ta_counter_t *)cnt);
    res->sub_cyan += cnt->sub_cyan;
    res->sub_red += cnt->sub_red;
}

void
ta_cndfs_reduce   (run_t *run, wctx_t *ctx)
{
    if (run->reduced == NULL) {
        run->reduced = RTmallocZero (sizeof (ta_reduced_t));
        ta_reduced_t           *reduced = (ta_reduced_t *) run->reduced;
        reduced->waittime = 0;
    }
    ta_reduced_t           *reduced = (ta_reduced_t *) run->reduced;
    ta_alg_local_t         *ta_loc = (ta_alg_local_t *) ctx->local;

    add_results (&reduced->counters, &ta_loc->counters);

    cndfs_reduce (run, ctx);

    if (W >= 4 || !log_active(infoLong)) return;

    if (Strat_TA & strategy[0]) {
        fset_print_statistics (ta_loc->cyan, "Cyan stack set:");
    }
}

void
ta_cndfs_shared_init   (run_t *run)
{
    set_alg_local_init (run->alg, ta_cndfs_local_init);
    set_alg_global_init (run->alg, ta_cndfs_global_init);
    set_alg_global_deinit (run->alg, ta_cndfs_destroy);
    set_alg_local_deinit (run->alg, ta_cndfs_destroy_local);
    set_alg_print_stats (run->alg, ta_cndfs_print_stats);
    set_alg_run (run->alg, ta_cndfs_blue);
    set_alg_reduce (run->alg, ta_cndfs_reduce);

    run->shared = RTmallocZero (sizeof (ta_alg_shared_t));
    ta_alg_shared_t    *shared = (ta_alg_shared_t*) run->shared;
    shared->lmap = state_store_lmap (global->store);
}

