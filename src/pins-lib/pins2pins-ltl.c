#include <hre/config.h>

#include <limits.h>
#include <stdlib.h>

// Should be first to avoid collision with LTSmin print macros
#include <ltl2ba.h>
#undef Debug

#include <dm/dm.h>
#include <hre/unix.h>
#include <hre/user.h>
#include <ltsmin-lib/ltl2ba-lex.h>

#ifdef HAVE_SPOT
#include <ltsmin-lib/ltl2spot.h>
#endif

#include <ltsmin-lib/ltsmin-standard.h>
#include <mc-lib/atomics.h>
#include <pins-lib/pins.h>
#include <pins-lib/pins-util.h>
#include <pins-lib/pins2pins-ltl.h>
#undef min
#include <pins-lib/por/pins2pins-por.h>
#include <pins-lib/property-semantics.h>
#include <util-lib/util.h>


uint32_t                HOA_ACCEPTING_SET = 0;
static char            *ltl_file = NULL;
static const char      *ltl_semantics_name = "none";
static const char      *buchi_type = "ba";
pins_ltl_type_t         PINS_LTL = PINS_LTL_NONE;
pins_buchi_type_t       PINS_BUCHI_TYPE = PINS_BUCHI_TYPE_BA;

static si_map_entry db_ltl_semantics[]={
    {"none",    PINS_LTL_NONE},
    {"spin",    PINS_LTL_SPIN},
    {"textbook",PINS_LTL_TEXTBOOK},
    {"ltsmin",  PINS_LTL_LTSMIN},
    {NULL, 0}
};

static si_map_entry db_buchi_type[]={
    {"ba",      PINS_BUCHI_TYPE_BA},
    {"tgba",    PINS_BUCHI_TYPE_TGBA},
    {"spotba",  PINS_BUCHI_TYPE_SPOTBA},
    {NULL, 0}
};

uint32_t
GBgetAcceptingSet ()
{
    return HOA_ACCEPTING_SET;
}

static void
ltl_popt (poptContext con, enum poptCallbackReason reason,
          const struct poptOption *opt, const char *arg, void *data)
{
    (void)con; (void)opt; (void)arg; (void)data;
    switch (reason) {
    case POPT_CALLBACK_REASON_PRE:
        break;
    case POPT_CALLBACK_REASON_POST:
        {
            int l = linear_search (db_ltl_semantics, ltl_semantics_name);
            if (l < 0) {
                Warning (error, "unknown ltl semantic %s", ltl_semantics_name);
                HREprintUsage();
                HREexit(LTSMIN_EXIT_FAILURE);
            }
            Print (infoLong, "LTL semantics: %s", ltl_semantics_name);
            PINS_LTL = l;

            int b = linear_search (db_buchi_type, buchi_type);
            if (b < 0) {
                Warning (error, "unknown buchi type %s", buchi_type);
                HREprintUsage();
                HREexit(LTSMIN_EXIT_FAILURE);
            }
            Print (infoLong, "Buchi type: %s", buchi_type);
            PINS_BUCHI_TYPE = b;
        }
        return;
    case POPT_CALLBACK_REASON_OPTION:
        break;
    }
    Abort("unexpected call to ltl_popt");
}

struct poptOption ltl_options[] = {
    {NULL, 0, POPT_ARG_CALLBACK | POPT_CBFLAG_POST | POPT_CBFLAG_SKIPOPTION, (void *)ltl_popt, 0, NULL, NULL},
    {"ltl", 0, POPT_ARG_STRING, &ltl_file, 0, "LTL formula or file with LTL formula",
     "<ltl-file>.ltl|<ltl formula>"},
    {"ltl-semantics", 0, POPT_ARG_STRING, &ltl_semantics_name, 0,
     "LTL semantics", "<spin|textbook|ltsmin> (default: \"spin\")"},
    {"buchi-type", 0, POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT, &buchi_type, 0,
     "Buchi automaton type", "<ba|tgba|spotba>"},
    POPT_TABLEEND
};


typedef struct ltl_context {
    model_t         parent;
    int             ltl_idx;
    int             sl_idx_accept;
    int             sl_idx_nonaccept;
    int             el_idx_accept_set;
    int             len;
    int             old_len;
    int             groups;
    int             old_groups;
    int             edge_labels;
    int             edge_labels_old;
    ltsmin_buchi_t *ba;
    bool            is_weak;
    int            *labels;
} ltl_context_t;

typedef struct cb_context {
    model_t         model;
    TransitionCB    cb;
    void           *user_context;
    int            *src;
    int             ntbtrans; /* number of textbook transitions */
    ltl_context_t  *ctx;
    int             predicate_evals; // assume < 32 predicates..
} cb_context;

static inline int
is_accepting (ltl_context_t *ctx, int *state)
{
    // state[0] must be the buchi automaton, because it's necessarily dependent
    int val = state[ctx->ltl_idx] == -1 ? 0 : state[ctx->ltl_idx];
    HREassert(val < ctx->ba->state_count);
    return ctx->ba->states[val]->accept;
}

static int
ltl_sl_short(model_t model, int label, int *state)
{
    ltl_context_t *ctx = GBgetContext(model);
    if (label == ctx->sl_idx_accept) {
        return is_accepting (ctx, state);
    } else if (label == ctx->sl_idx_nonaccept) {
        return !is_accepting (ctx, state);
    } else {
        return GBgetStateLabelShort(ctx->parent, label, state + 1);
    }
}

static int
ltl_sl_long(model_t model, int label, int *state)
{
    ltl_context_t *ctx = GBgetContext(model);
    if (label == ctx->sl_idx_accept) {
        return is_accepting (ctx, state);
    } else if (label == ctx->sl_idx_nonaccept) {
        return !is_accepting (ctx, state);
    } else {
        return GBgetStateLabelLong(ctx->parent, label, state + 1);
    }
}

static void
ltl_sl_all(model_t model, int *state, int *labels)
{
    ltl_context_t *ctx = GBgetContext(model);
    GBgetStateLabelsAll(ctx->parent, state + 1, labels);
    labels[ctx->sl_idx_accept] = is_accepting (ctx, state);
    if (ctx->sl_idx_nonaccept != -1) {
        labels[ctx->sl_idx_nonaccept] = !is_accepting (ctx, state);
    }
}

static inline int
eval (cb_context *infoctx, int *state)
{
    ltl_context_t *ctx = infoctx->ctx;
    int pred_evals = 0; // assume < 32 predicates..
    for(int i=0; i < ctx->ba->predicate_count; i++) {
        if (eval_predicate(GBgetParent(infoctx->model), ctx->ba->predicates[i], state, ctx->ba->env))
            pred_evals |= (1 << i);
    }
    return pred_evals;
}

/*
 * TYPE LTSMIN
 */
void ltl_ltsmin_cb (void *context, transition_info_t *ti, int *dst, int *cpy) {
    cb_context *infoctx = (cb_context*)context;
    ltl_context_t *ctx = infoctx->ctx;
    // copy dst, append ltl never claim in lockstep
    int dst_buchi[ctx->len];
    memcpy (dst_buchi + 1, dst, (ctx->len - 1) * sizeof(int) );

    // evaluate predicates
    int pred_evals = infoctx->predicate_evals;
    if (pred_evals == -1) // long calls cannot do before-hand evaluation
        eval (infoctx, infoctx->src + 1); /* ltsmin: src instead of dst */
    int i = infoctx->src[ctx->ltl_idx];
    HREassert (i < ctx->ba->state_count);
    if (PINS_BUCHI_TYPE == PINS_BUCHI_TYPE_TGBA) {
        HREassert (ctx->edge_labels_old == ctx->el_idx_accept_set);
        memcpy (ctx->labels, ti->labels, sizeof(int[ctx->edge_labels_old]));
        ti->labels = ctx->labels; // inline because por_proviso is passed up
    }
    for(int j=0; j < ctx->ba->states[i]->transition_count; j++) {
        // check predicates
        if ((pred_evals & ctx->ba->states[i]->transitions[j].pos[0]) == ctx->ba->states[i]->transitions[j].pos[0] &&
            (pred_evals & ctx->ba->states[i]->transitions[j].neg[0]) == 0) {
            // perform transition
            dst_buchi[ctx->ltl_idx] = ctx->ba->states[i]->transitions[j].to_state;

            // allocate the edge labels and write the TGBA acceptance set
            if (PINS_BUCHI_TYPE == PINS_BUCHI_TYPE_TGBA) {
                ti->labels[ctx->el_idx_accept_set] =
                                ctx->ba->states[i]->transitions[j].acc_set;
            }

            // callback, emit new state, move allowed
            infoctx->cb(infoctx->user_context, ti, dst_buchi,cpy);
            ++infoctx->ntbtrans;
        }
    }
}

static int
ltl_ltsmin_long (model_t self, int group, int *src, TransitionCB cb,
           void *user_context)
{
    ltl_context_t *ctx = GBgetContext(self);
    cb_context new_ctx = {self, cb, user_context, src, 0, ctx, -1};
    GBgetTransitionsLong(ctx->parent, group, src + 1, ltl_ltsmin_cb, &new_ctx);
    return new_ctx.ntbtrans;
}

static int
ltl_ltsmin_short (model_t self, int group, int *src, TransitionCB cb,
           void *user_context)
{
    (void)self; (void)group; (void)src; (void)cb; (void)user_context;
    Abort("Using LTL layer --cached?  Still on todo list ;)");
}


static int
ltl_ltsmin_all (model_t self, int *src, TransitionCB cb,
         void *user_context)
{
    ltl_context_t *ctx = GBgetContext(self);
    cb_context new_ctx = {self, cb, user_context, src, 0, ctx, 0};
    // evaluate predicates (on source, so before hand!)
    new_ctx.predicate_evals = eval (&new_ctx, src + 1); /* No EVARS! */
    GBgetTransitionsAll(ctx->parent, src + 1, ltl_ltsmin_cb, &new_ctx);
    return new_ctx.ntbtrans;
}

/*
 * TYPE SPIN
 */
void ltl_spin_cb (void *context, transition_info_t *ti, int *dst, int *cpy) {
    cb_context *infoctx = (cb_context*)context;
    ltl_context_t *ctx = infoctx->ctx;
    // copy dst, append ltl never claim in lockstep
    int dst_buchi[ctx->len];
    memcpy (dst_buchi + 1, dst, ctx->old_len * sizeof(int) );
    int pred_evals = infoctx->predicate_evals;
    int i = infoctx->src[ctx->ltl_idx];
    HREassert (i < ctx->ba->state_count);
    if (PINS_BUCHI_TYPE == PINS_BUCHI_TYPE_TGBA) {
        HREassert (ctx->edge_labels_old == ctx->el_idx_accept_set);
        memcpy (ctx->labels, ti->labels, sizeof(int[ctx->edge_labels_old]));
        ti->labels = ctx->labels; // inline because por_proviso is passed up
    }
    for(int j=0; j < ctx->ba->states[i]->transition_count; j++) {
        // check predicates
        if ((pred_evals & ctx->ba->states[i]->transitions[j].pos[0]) == ctx->ba->states[i]->transitions[j].pos[0] &&
            (pred_evals & ctx->ba->states[i]->transitions[j].neg[0]) == 0) {
            // perform transition
            dst_buchi[ctx->ltl_idx] = ctx->ba->states[i]->transitions[j].to_state;

            // allocate the edge labels and write the TGBA acceptance set
            if (PINS_BUCHI_TYPE == PINS_BUCHI_TYPE_TGBA) {
                ti->labels[ctx->el_idx_accept_set] =
                                ctx->ba->states[i]->transitions[j].acc_set;
            }

            // callback, emit new state, move allowed
            infoctx->cb (infoctx->user_context, ti, dst_buchi,cpy);
            ++infoctx->ntbtrans;
        }
    }
}

static int
ltl_spin_long (model_t self, int group, int *src, TransitionCB cb,
           void *user_context)
{
    (void)self; (void)group; (void)src; (void)cb; (void)user_context;
    Abort("Using LTL layer --grey? -reach? ? Still on todo list ;)");
    // Could be implemented by evaluating all guards for to detect a deadlock
}

static int
ltl_spin_short (model_t self, int group, int *src, TransitionCB cb,
           void *user_context)
{
    (void)self; (void)group; (void)src; (void)cb; (void)user_context;
    Abort("Using LTL layer --cached?  Still on todo list ;)");
}


static int
ltl_spin_all (model_t self, int *src, TransitionCB cb,
              void *user_context)
{
    ltl_context_t *ctx = GBgetContext(self);

    cb_context new_ctx = {self, cb, user_context, src, 0, ctx, 0};
    // evaluate predicates (on source, so before hand!)
    new_ctx.predicate_evals = eval (&new_ctx, src + 1); /* No EVARS! */
    GBgetTransitionsAll(ctx->parent, src + 1, ltl_spin_cb, &new_ctx);
    if (0 == new_ctx.ntbtrans) { // deadlock, let buchi continue
        int dst_buchi[ctx->len];
        memcpy (dst_buchi + 1, src + 1, ctx->old_len * sizeof(int) );
        int i = src[ctx->ltl_idx];
        HREassert (i < ctx->ba->state_count);
        memset (ctx->labels, 0, sizeof(int) * ctx->edge_labels);
        for(int j=0; j < ctx->ba->states[i]->transition_count; j++) {
            if ((new_ctx.predicate_evals & ctx->ba->states[i]->transitions[j].pos[0]) == ctx->ba->states[i]->transitions[j].pos[0] &&
                (new_ctx.predicate_evals & ctx->ba->states[i]->transitions[j].neg[0]) == 0) {
                // perform transition
                dst_buchi[ctx->ltl_idx] = ctx->ba->states[i]->transitions[j].to_state;
                // callback, emit new state, move allowed
                int group = ctx->old_groups + ctx->ba->states[i]->transitions[j].index;
                HREassert (group < ctx->groups, "Group %d larger than expected nr of groups %d, buchi idx: %d",
                           group, ctx->groups, ctx->ba->states[i]->transitions[j].index);

                // set the edge label for the TGBA acceptance set
                if (PINS_BUCHI_TYPE == PINS_BUCHI_TYPE_TGBA) {
                    ctx->labels[ctx->el_idx_accept_set] =
                                    ctx->ba->states[i]->transitions[j].acc_set;
                }

                transition_info_t ti = GB_TI(ctx->labels, group);
                ti.por_proviso = 1; // state fully explored

                cb(user_context, &ti, dst_buchi,NULL);
                ++new_ctx.ntbtrans;
            }
        }
    }
    return new_ctx.ntbtrans;
}

/*
 * TYPE TEXTBOOK
 */
void ltl_textbook_cb (void *c, transition_info_t *ti, int *dst, int *cpy) {
    cb_context *infoctx = (cb_context*)c;
    ltl_context_t *ctx = infoctx->ctx;
    // copy dst, append ltl never claim in lockstep
    int dst_buchi[ctx->len];
    memcpy (dst_buchi + 1, dst, ctx->old_len * sizeof(int) );
    int dst_pred = eval (infoctx, dst);
    int i = infoctx->src[ctx->ltl_idx];
    if (i == -1) { i=0; } /* textbook: extra initial state */
    HREassert (i < ctx->ba->state_count );
    if (PINS_BUCHI_TYPE == PINS_BUCHI_TYPE_TGBA) {
        HREassert (ctx->edge_labels_old == ctx->el_idx_accept_set);
        memcpy (ctx->labels, ti->labels, sizeof(int[ctx->edge_labels_old]));
        ti->labels = ctx->labels; // inline because por_proviso is passed up
    }
    for(int j=0; j < ctx->ba->states[i]->transition_count; j++) {
        // check predicates
        if ((dst_pred & ctx->ba->states[i]->transitions[j].pos[0]) == ctx->ba->states[i]->transitions[j].pos[0] &&
            (dst_pred & ctx->ba->states[i]->transitions[j].neg[0]) == 0) {
            // perform transition
            dst_buchi[ctx->ltl_idx] = ctx->ba->states[i]->transitions[j].to_state;

            // allocate the edge labels and write the TGBA acceptance set
            if (PINS_BUCHI_TYPE == PINS_BUCHI_TYPE_TGBA) {
                ti->labels[ctx->el_idx_accept_set] =
                                ctx->ba->states[i]->transitions[j].acc_set;
            }

            // callback, emit new state, move allowed
            infoctx->cb(infoctx->user_context, ti, dst_buchi,cpy);
            ++infoctx->ntbtrans;
        }
    }
}

static int
ltl_textbook_long (model_t self, int group, int *src, TransitionCB cb,
           void *user_context)
{
    (void)self; (void)group; (void)src; (void)cb; (void)user_context;
    Abort("Using LTL layer --grey? -reach? ? Still on todo list ;)");
}

static int
ltl_textbook_short (model_t self, int group, int *src, TransitionCB cb,
           void *user_context)
{
    (void)self; (void)group; (void)src; (void)cb; (void)user_context;
    Abort("Using LTL layer --cached?  Still on todo list ;)");
}


static int
ltl_textbook_all (model_t self, int *src, TransitionCB cb, void *user_context)
{
    ltl_context_t *ctx = GBgetContext(self);
    cb_context new_ctx = {self, cb, user_context, src, 0, ctx, 0};
    if (src[ctx->ltl_idx] == -1) {
        int labels[ctx->edge_labels];
        memset (labels, 0, sizeof(int[ctx->edge_labels]));
        transition_info_t ti = GB_TI(labels, ctx->groups);
        ltl_textbook_cb(&new_ctx, &ti, src + 1, NULL);
        return new_ctx.ntbtrans;
    } else {
        GBgetTransitionsAll(ctx->parent, src + 1, ltl_textbook_cb, &new_ctx);
        return new_ctx.ntbtrans;
    }
}

void
print_ltsmin_buchi(const ltsmin_buchi_t *ba, ltsmin_parse_env_t env)
{
    int is_hoa = ba->acceptance_set;
    if (is_hoa) { // HOA acceptance
        char buf[4096];
        memset(buf, 0, sizeof(buf));
        char* at = buf;
        *at = '\0';
        for (int p=0; p<32; p++) {
            if ( ba->acceptance_set & (1 << p) )
                at +=  sprintf(at, " %d",p);
        }
        Warning(info, "Acceptance set: {%s }", buf);
    }
    Warning(info, "buchi has %d states", ba->state_count);
    for(int i=0; i < ba->state_count; i++) {
        if (is_hoa) {
            if (!ba->states[i]->accept) {
                Warning(info, " state %d:", i);
            } else {
                char buf[4096];
                memset(buf, 0, sizeof(buf));
                char* at = buf;
                *at = '\0';
                for (int p=0; p<32; p++) {
                    if ( ba->states[i]->accept & (1 << p) )
                        at +=  sprintf(at, " %d",p);
                }
                Warning(info, " state %d: {%s }", i, buf);
            }
        }
        else {
            Warning(info, " state %d: %s", i, ba->states[i]->accept ? "accepting" : "non-accepting");
        }
        for(int j=0; j < ba->states[i]->transition_count; j++) {
            char buf[4096*32];
            memset(buf, 0, sizeof(buf));
            char* at = buf;
            *at = '\0';
            for(int k=0; k < ba->predicate_count; k++) {
                if (ba->states[i]->transitions[j].pos[k/32] & (1<<(k%32))) {
                    if (at != buf)
                        at += sprintf(at, " && ");
                    char *expr =  LTSminPrintExpr(ba->predicates[k], env);
                    at += sprintf(at, "%s",expr);
                }
                if (ba->states[i]->transitions[j].neg[k/32] & (1<<(k%32))) {
                    if (at != buf)
                        at += sprintf(at, " && ");
                    *at++ = '!';
                    char *expr = LTSminPrintExpr(ba->predicates[k], env);
                    at += sprintf(at, "%s",expr);
                }
            }
            if (at == buf) sprintf(at, "true");
            
            if (is_hoa && ba->states[i]->transitions[j].acc_set) { // HOA acceptance
                at += sprintf(at, " {");
                for (int p=0; p<32; p++) {
                    if ( ba->states[i]->transitions[j].acc_set & (1 << p) )
                        at +=  sprintf(at, " %d",p);
                }
                at +=  sprintf(at, " }");
            }
            Warning(info, "  -> %d, | %s", ba->states[i]->transitions[j].to_state, buf);
        }
    }
}

static ltsmin_buchi_t  *shared_ba = NULL;
static int              grab_ba = 0;

/* NOTE: this is hack around non-thread-safe ltl2ba
 * In multi-process environment, all processes create their own buchi,
 * which is what we want anyway, because ltsmin_ltl2ba uses strdup.
 */
ltsmin_buchi_t *
init_ltsmin_buchi(model_t model, const char *ltl_file)
{
    if (NULL == shared_ba && cas(&grab_ba, 0, 1)) {
        Warning(info, "LTL layer: formula: %s", ltl_file);
        ltsmin_parse_env_t env = LTSminParseEnvCreate();
        ltsmin_expr_t ltl = ltl_parse_file (ltl_file, env, GBgetLTStype(model));

        ltsmin_expr_t notltl = LTSminExpr(UNARY_OP, LTL_NOT, 0, ltl, NULL);

        ltsmin_buchi_t *ba;
#ifdef HAVE_SPOT
        if (PINS_BUCHI_TYPE == PINS_BUCHI_TYPE_TGBA ||
            PINS_BUCHI_TYPE == PINS_BUCHI_TYPE_SPOTBA) {
            ltsmin_ltl2spot(notltl, PINS_BUCHI_TYPE == PINS_BUCHI_TYPE_TGBA, env);
            ba = ltsmin_hoa_buchi();
        } else {
#endif
            HREassert(PINS_BUCHI_TYPE == PINS_BUCHI_TYPE_BA, 
                "Buchi type %s is not possible without Spot", buchi_type);
            ltsmin_ltl2ba(notltl);
            ba = ltsmin_buchi();
#ifdef HAVE_SPOT
        }
#endif

        if (NULL == ba) {
            Print(info, "Empty buchi automaton.");
            Print(info, "The property is TRUE.");
            HREexit(LTSMIN_EXIT_SUCCESS);
        }
        if (ba->predicate_count > 30) {
            Abort("more than 30 predicates in buchi automaton are currently not supported");
        }
        ba->env = env;
        atomic_write (&shared_ba, ba);
        print_ltsmin_buchi(ba, env);
    } else {
        while (NULL == atomic_read(&shared_ba)) {}
    }
    return atomic_read(&shared_ba);
}

typedef uint32_t visited_index_t;

typedef struct scc_ctx_s {
    ltsmin_buchi_t         *ba;
    uint64_t                index;
    uint64_t                scc_index;
    visited_index_t        *vset;
    ci_list                *stack;
} scc_ctx_t;

static bool
scc_check_r (scc_ctx_t *ctx, int v)
{
    bool            root = true;
    ctx->vset[v] = ctx->index++;
    for (int i = 0; i < ctx->ba->states[v]->transition_count; i++) {
        int                 w = ctx->ba->states[v]->transitions[i].to_state;
        if (ctx->vset[w] == 0)
            if (!scc_check_r (ctx, w)) return false;
        root &= ctx->vset[v] <= ctx->vset[w];
        ctx->vset[v] = min (ctx->vset[v], ctx->vset[w]);
    }
    if (root) {
        size_t              acc_count = ctx->ba->states[v]->accept != 0;
        size_t              scc_size = 1;
        while (ci_count(ctx->stack) != 0 &&
               ctx->vset[v] <= ctx->vset[ci_top(ctx->stack)]) {
            int                 w = ci_pop (ctx->stack);
            ctx->vset[w] = ctx->scc_index;
            scc_size++;
            acc_count += ctx->ba->states[w]->accept != 0;
        }
        ctx->index -= scc_size;
        if (scc_size != acc_count && acc_count != 0) return false;
        ctx->scc_index--;
    } else {
        ci_add (ctx->stack, v);
    }
    return true;
}

static bool
is_weak (ltsmin_buchi_t *ba)
{
    visited_index_t    *seen = RTmallocZero(sizeof(uint32_t[ba->state_count]));
    scc_ctx_t           ctx;
    ctx.ba = ba;
    ctx.vset = seen;
    ctx.index = 1;
    ctx.scc_index = ba->state_count - 1;
    ctx.stack = ci_create (ba->state_count);
    bool            weak = scc_check_r (&ctx, 0);
    RTfree (seen);
    ci_free (ctx.stack);
    return weak;
}

/*
 * SHARED
 * por_model: if por layer is added por_model points to the model returned by the layer,
 *            otherwise this parameter should be NULL
 */
model_t
GBaddLTL (model_t model)
{
    if (ltl_file == NULL) return model;

    lts_type_t ltstype = GBgetLTStype(model);
    int old_idx = pins_get_accepting_state_label_index (model);
    if (old_idx != -1) {
        Print1 (info, "LTL layer: model already has a ``%s'' property, overriding",
               lts_type_get_state_label_name(ltstype, old_idx));
    }

    if (PINS_LTL == PINS_LTL_NONE) PINS_LTL = PINS_LTL_SPIN;

    ltsmin_buchi_t *ba = init_ltsmin_buchi(model, ltl_file);

    model_t         ltlmodel = GBcreateBase();
    ltl_context_t *ctx = RTmalloc(sizeof *ctx);
    ctx->parent = model;
    ctx->ba = ba;
    GBsetContext(ltlmodel, ctx);

    // copy and extend ltstype
    int             len = lts_type_get_state_length (ltstype);
    ctx->old_len = len;

    // We add a state variable (at the beginning) and some transitions for SPIN semantics
    int             new_len = len + 1; // one for buchi state

    // set in context for later use in function
    ctx->ltl_idx = 0;

    /* state variables and their types */
    lts_type_t ltstype_new = lts_type_clone (ltstype);
    // set new length
    lts_type_set_state_length (ltstype_new, new_len);
    // add type
    int type_count = lts_type_get_type_count (ltstype_new);
    int ltl_type = lts_type_add_type (ltstype_new, "buchi", NULL);
    HREassert (ltl_type == type_count, "LTL type error");
    lts_type_set_format (ltstype_new, ltl_type, LTStypeDirect);

    // add name (at position 0)
    lts_type_set_state_name(ltstype_new, ctx->ltl_idx, "ltl");
    lts_type_set_state_typeno(ltstype_new, ctx->ltl_idx, ltl_type);

    // copy-move old state names
    for (int i = 0; i < len; i++) {
        lts_type_set_state_name(ltstype_new, i+1, lts_type_get_state_name(ltstype, i));
        lts_type_set_state_type(ltstype_new, i+1, lts_type_get_state_type(ltstype, i));
    }

    /* State labels */
    int bool_is_new;
    int bool_type = lts_type_add_type (ltstype_new, LTSMIN_TYPE_BOOL, &bool_is_new);

    matrix_t       *p_sl = GBgetStateLabelInfo (model);
    int             sl_count = dm_nrows (p_sl);

    ctx->is_weak = is_weak(ba);
    int             new_sl_count;
    if (ctx->is_weak) {
        Print1 (info, "Weak Buchi automaton detected, adding non-accepting as progress label.");
        new_sl_count = sl_count + 2;
        lts_type_set_state_label_count (ltstype_new, new_sl_count);

        ctx->sl_idx_nonaccept = sl_count + 1;
        lts_type_set_state_label_name (ltstype_new, ctx->sl_idx_nonaccept, LTSMIN_STATE_LABEL_WEAK_LTL_PROGRESS);
        lts_type_set_state_label_typeno (ltstype_new, ctx->sl_idx_nonaccept, bool_type);
    } else {
        ctx->sl_idx_nonaccept = -1;
        new_sl_count = sl_count + 1;
        lts_type_set_state_label_count (ltstype_new, new_sl_count);
    }

    // add buchi label (at end)
    ctx->sl_idx_accept = sl_count;
    lts_type_set_state_label_name (ltstype_new, ctx->sl_idx_accept, LTSMIN_STATE_LABEL_ACCEPTING);
    lts_type_set_state_label_typeno (ltstype_new, ctx->sl_idx_accept, bool_type);


    // copy state labels
    for (int i = 0; i < sl_count; ++i) {
        lts_type_set_state_label_name (ltstype_new, i,
                                       lts_type_get_state_label_name(ltstype,i));
        lts_type_set_state_label_typeno (ltstype_new, i,
                                         lts_type_get_state_label_typeno(ltstype,i));
    }

    if (PINS_BUCHI_TYPE == PINS_BUCHI_TYPE_TGBA) {
        /* Edge labels */
        int acc_set_type = lts_type_add_type (ltstype_new, LTSMIN_EDGE_TYPE_ACCEPTING_SET, NULL);

        int edge_labels = lts_type_get_edge_label_count (ltstype);

        lts_type_set_edge_label_count (ltstype_new, edge_labels + 1);
        lts_type_set_edge_label_name (ltstype_new, edge_labels, LTSMIN_EDGE_LABEL_ACCEPTING_SET);
        lts_type_set_edge_label_type (ltstype_new, edge_labels, LTSMIN_EDGE_TYPE_ACCEPTING_SET);
        lts_type_set_edge_label_typeno (ltstype_new, edge_labels, acc_set_type);

        HOA_ACCEPTING_SET = ba->acceptance_set;

        ctx->el_idx_accept_set = edge_labels;
    }

    ctx->labels = RTmalloc (sizeof(int[lts_type_get_edge_label_count (ltstype_new)]));

    // This messes up the trace, the chunk maps now is one index short! Fixed below
    GBcopyChunkMaps(ltlmodel, model);

    // set new type
    GBsetLTStype(ltlmodel, ltstype_new);

    // extend the chunk maps
    GBgrowChunkMaps(ltlmodel, type_count);

    if (bool_is_new) {
        pins_chunk_put_at (ltlmodel, bool_type, chunk_str(LTSMIN_VALUE_BOOL_FALSE), 0);
        pins_chunk_put_at (ltlmodel, bool_type, chunk_str(LTSMIN_VALUE_BOOL_TRUE), 1);
    }

    /* Fix matrixes */
    matrix_t       *p_new_dm = (matrix_t*) RTmalloc(sizeof(matrix_t));
    matrix_t       *p_new_dm_r = (matrix_t*) RTmalloc(sizeof(matrix_t));
    matrix_t       *p_new_dm_w = (matrix_t*) RTmalloc(sizeof(matrix_t));
    matrix_t       *p_dm = GBgetDMInfo (model);
    matrix_t       *p_dm_r = GBgetDMInfoRead (model);
    matrix_t       *p_dm_w = GBgetDMInfoMayWrite (model);

    int             groups = dm_nrows( GBgetDMInfo(model) );
    int             new_ngroups;

    switch (PINS_LTL) {
    case PINS_LTL_LTSMIN:
        new_ngroups = groups;
        break;
    case PINS_LTL_SPIN:
        new_ngroups = groups + ba->trans_count;
        break;
    case PINS_LTL_TEXTBOOK:
        new_ngroups = groups + 1;
        break;
    default: Abort("Unknown LTL semantics.");
    }

    bitvector_t formula_state_dep;
    bitvector_create(&formula_state_dep, len);
    
    // mark the parts the buchi automaton uses for reading
    for (int k=0; k < ba->predicate_count; k++) {
        set_pins_semantics(model, ba->predicates[k], ba->env, &formula_state_dep);
    }

    // add one column to the matrix
    // copy matrix, add buchi automaton
    dm_create(p_new_dm  , new_ngroups, len+1);
    dm_create(p_new_dm_r, new_ngroups, len+1);
    dm_create(p_new_dm_w, new_ngroups, len+1);
    for(int i = 0; i < groups; i++) {
        // copy old matrix rows
        for(int j=0; j < len; j++) {
            if (dm_is_set(p_dm, i, j))
                dm_set(p_new_dm, i, j+1);
            if (dm_is_set(p_dm_r, i, j))
                dm_set(p_new_dm_r, i, j+1);
            if (dm_is_set(p_dm_w, i, j))
                dm_set(p_new_dm_w, i, j+1);
        }

        // add buchi as dependent
        dm_set(p_new_dm, i, ctx->ltl_idx);
        dm_set(p_new_dm_r, i, ctx->ltl_idx);
        dm_set(p_new_dm_w, i, ctx->ltl_idx);

        // add buchi variables as dependent (read)
        for(int j=0; j < len; j++) {
            if (bitvector_is_set(&formula_state_dep, j)) {
                dm_set(p_new_dm, i, j+1);
                dm_set(p_new_dm_r, i, j+1);
            }
        }
    }

    if (PINS_LTL == PINS_LTL_SPIN || PINS_LTL == PINS_LTL_TEXTBOOK) {
        for (int i = groups; i < new_ngroups; i++) {
            // add buchi as dependent
            dm_set(p_new_dm, i, ctx->ltl_idx);
            dm_set(p_new_dm_r, i, ctx->ltl_idx);
            dm_set(p_new_dm_w, i, ctx->ltl_idx);

            // add buchi variables as dependent (read)
            for (int j=0; j < len; j++) {
                if (bitvector_is_set(&formula_state_dep, j)) {
                    dm_set(p_new_dm, i, j+1);
                    dm_set(p_new_dm_r, i, j+1);
                }
            }
        }
    }

    // fill groups added by SPIN LTL deadlock behavior with guards dependencies
    if (PINS_LTL == PINS_LTL_SPIN) {
        // because these transitions depend on a deadlock, they depend on all others
        sl_group_t *guards = GBgetStateLabelGroupInfo(model, GB_SL_GUARDS);
        int deps[len]; // collects union of all dependencies
        memset (&deps, 0, sizeof(int[len]));
        if (NULL == guards) { // use DM read matrix as over-estimation for guard deps
            for(int i=0; i < groups; i++) {
                 for (int j=0; j < len; j++) {
                     deps[j] |= dm_is_set(p_dm_r, i, j);
                  }
             }
        } else {
            for (int i=0; i < guards->count; i++) {
                for (int j=0; j < len; j++) {
                    deps[j] |= dm_is_set(p_sl, guards->sl_idx[i], j);
                }
            }
        }
        for (int j=0; j < len; j++) {
            if (deps[j]) {
                for(int i = groups; i < new_ngroups; i++) {
                    dm_set(p_new_dm, i, j+1);
                    dm_set(p_new_dm_r, i, j+1);
                }
            }
        }
    }

    GBsetDMInfo(ltlmodel, p_new_dm);
    GBsetDMInfoRead(ltlmodel, p_new_dm_r);
    GBsetDMInfoMayWrite(ltlmodel, p_new_dm_w);

    // create new state label matrix
    matrix_t       *p_new_sl = RTmalloc (sizeof *p_new_sl);

    int             sl_len = dm_ncols (p_sl);
    int             new_sl_len = sl_len + 1;
    HREassert (new_sl_len == new_len);

    dm_create(p_new_sl, new_sl_count, new_sl_len);
    // copy old matrix
    for (int i=0; i < sl_count; ++i) {
        for (int j=0; j < sl_len; ++j) {
            if (dm_is_set(p_sl, i, j))
                dm_set(p_new_sl, i, j+1);
        }
    }

    // add buchi label dependencies
    dm_set(p_new_sl, ctx->sl_idx_accept, ctx->ltl_idx);
    for (int j=0; j < sl_len; ++j) {
        if (bitvector_is_set(&formula_state_dep, j))
            dm_set(p_new_sl, ctx->sl_idx_accept, j+1);
    }
    
    bitvector_clear(&formula_state_dep);

    GBsetStateLabelInfo(ltlmodel, p_new_sl);

    // Now overwrite the state label functions to catch the new state label
    GBsetStateLabelShort (ltlmodel, ltl_sl_short);
    GBsetStateLabelLong (ltlmodel, ltl_sl_long);
    GBsetStateLabelsAll (ltlmodel, ltl_sl_all);

    lts_type_validate(ltstype_new);

    switch (PINS_LTL) {
    case PINS_LTL_LTSMIN:
        GBsetNextStateLong  (ltlmodel, ltl_ltsmin_long);
        GBsetNextStateShort (ltlmodel, ltl_ltsmin_short);
        GBsetNextStateAll   (ltlmodel, ltl_ltsmin_all);
        break;
    case PINS_LTL_SPIN:
        GBsetNextStateLong  (ltlmodel, ltl_spin_long);
        GBsetNextStateShort (ltlmodel, ltl_spin_short);
        GBsetNextStateAll   (ltlmodel, ltl_spin_all);
        break;
    case PINS_LTL_TEXTBOOK:
        GBsetNextStateLong  (ltlmodel, ltl_textbook_long);
        GBsetNextStateShort (ltlmodel, ltl_textbook_short);
        GBsetNextStateAll   (ltlmodel, ltl_textbook_all);
        break;
    default: Abort("Unknown LTL semantics.");
    }

    int        *cur_visibility = GBgetPorGroupVisibility  (model);
    if (cur_visibility && new_ngroups != groups) {
        int        *group_visibility = RTmallocZero( new_ngroups * sizeof(int) );
        memcpy (group_visibility, cur_visibility, sizeof(int[groups]));
        GBsetPorGroupVisibility  (ltlmodel, group_visibility);
    }

    GBinitModelDefaults (&ltlmodel, model);

    int             s0[new_len];
    GBgetInitialState (model, s0+1);
    // set buchi initial state
    s0[ctx->ltl_idx] = (PINS_LTL == PINS_LTL_TEXTBOOK ? -1 : 0);

    GBsetInitialState (ltlmodel, s0);

    ctx->len = new_len;
    ctx->old_groups = groups;
    ctx->groups = new_ngroups;
    ctx->edge_labels_old = lts_type_get_edge_label_count(ltstype);
    ctx->edge_labels = lts_type_get_edge_label_count(ltstype_new);
    return ltlmodel;
}
