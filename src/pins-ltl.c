#include <config.h>
#include <stdlib.h>
#include <limits.h>
#include <assert.h>

#include <ltl2ba.h>
#undef Debug
#include <dm/dm.h>
#include <greybox.h>
#include <runtime.h>
#include <unix.h>
#include <ltsmin-syntax.h>
#include <ltsmin-tl.h>
#include <ltsmin-buchi.h>

// TODO fix include file
extern void ltsmin_ltl2ba(ltsmin_expr_t);
extern ltsmin_buchi_t *ltsmin_buchi();
// TODO

typedef struct cb_context {
    TransitionCB cb;
    void* user_context;
    int*  src;
    int   ntbtrans;               /* number of textbook transitions */
} cb_context;

typedef struct ltl_context {
    model_t         parent;
    int             ltl_idx;
    int             sl_idx_accept;
    int             len;
    const ltsmin_buchi_t *ba;
} ltl_context_t;

static ltl_context_t *ctx;

int eval_predicate(ltsmin_expr_t e, transition_info_t* ti, int* state);

inline int
eval_predicate(ltsmin_expr_t e, transition_info_t* ti, int* state)
{
    switch (e->token) {
        case LTL_TRUE:
            return 1;
        case LTL_FALSE:
            return 0;
        case LTL_NUM:
            return e->idx;
        case LTL_SVAR:
            return state[e->idx];
        case LTL_EQ:
            return (eval_predicate(e->arg1, ti, state) == eval_predicate(e->arg2, ti, state));
        case LTL_VAR:
            Abort("unbound variable in LTL expression");
        default: {
            char buf[1024];
            ltsmin_expr_print_ltl(e, buf);
            Fatal(1, error, "unhandled predicate expression: %s", buf);
        }
    }
    return 0;
    (void)ti;
}

static void
mark_predicate(ltsmin_expr_t e, matrix_t *m)
{
    switch(e->token) {
        case LTL_TRUE:
        case LTL_FALSE:
        case LTL_NUM:
        case LTL_VAR:
            break;
        case LTL_EQ:
            mark_predicate(e->arg1, m);
            mark_predicate(e->arg2, m);
            break;
        case LTL_SVAR: {
            for(int i=0; i < dm_nrows(m); i++)
                dm_set(m, i, e->idx);
            } break;
        default:
            Fatal(1, error, "unhandled predicate expression in mark_predicate");
    }
}

static void
mark_visible(ltsmin_expr_t e, matrix_t *m, int* group_visibility)
{
    switch(e->token) {
        case LTL_TRUE:
        case LTL_FALSE:
        case LTL_NUM:
        case LTL_VAR:
            break;
        case LTL_EQ:
            mark_visible(e->arg1, m, group_visibility);
            mark_visible(e->arg2, m, group_visibility);
            break;
        case LTL_SVAR: {
            for(int i=0; i < dm_nrows(m); i++) {
                if (dm_is_set(m, i, e->idx)) {
                    group_visibility[i] = 1;
                }
            }
            } break;
        default:
            Fatal(1, error, "unhandled predicate expression in mark_visible");
    }
}


static int
ltl_sl_short(model_t model, int label, int *state)
{
    if (label == ctx->sl_idx_accept) {
        // state[0] must be the buchi automaton, because it's the only dependency
        return state[0] == -1 || ctx->ba->states[state[0]]->accept;
    } else {
        return GBgetStateLabelShort(GBgetParent(model), label, state);
    }
}

static int
ltl_sl_long(model_t model, int label, int *state)
{
    if (label == ctx->sl_idx_accept) {
        return state[ctx->ltl_idx] == -1 || ctx->ba->states[state[ctx->ltl_idx]]->accept;
    } else {
        return GBgetStateLabelLong(GBgetParent(model), label, state);
    }
}

static void
ltl_sl_all(model_t model, int *state, int *labels)
{
    GBgetStateLabelsAll(GBgetParent(model), state, labels);
    labels[ctx->sl_idx_accept] =
        state[ctx->ltl_idx] == -1 || ctx->ba->states[state[ctx->ltl_idx]]->accept;
}


/*
 * TYPE SPIN
 */
void ltl_spin_cb (void*context,transition_info_t*ti,int*dst) {
#define infoctx ((cb_context*)context)
    // copy dst, append ltl never claim in lockstep
    int dst_buchi[ctx->len];
    int dst_pred[1] = {0}; // assume < 32 predicates..
    memcpy(dst_buchi, dst, ctx->len * sizeof(int) );
    dst_buchi[ctx->ltl_idx] = 0;        /* XXX remove? */
    // evaluate predicates
    for(int i=0; i < ctx->ba->predicate_count; i++) {
        if (eval_predicate(ctx->ba->predicates[i], ti, infoctx->src)) /* spin: src instead of dst */
            dst_pred[0] |= (1 << i);
    }

    int i = infoctx->src[ctx->ltl_idx];

    for(int j=0; j < ctx->ba->states[i]->transition_count; j++) {
        // check predicates
        if ((dst_pred[0] & ctx->ba->states[i]->transitions[j].pos[0]) == ctx->ba->states[i]->transitions[j].pos[0] &&
            (dst_pred[0] & ctx->ba->states[i]->transitions[j].neg[0]) == 0) {
            // perform transition
            dst_buchi[ctx->ltl_idx] = ctx->ba->states[i]->transitions[j].to_state;

            // callback, emit new state, move allowed
            infoctx->cb(infoctx->user_context, ti, dst_buchi);
            ++infoctx->ntbtrans;
            /* debug
            {
            for(int k=0 ; k < ctx->len; k++)
                printf("%x ", infoctx->src[k]); printf(" ->");
            for(int k=0 ; k < ctx->len; k++)
                printf("%x ", dst_buchi[k]); printf("\n");
            }
            */
        }
    }
#undef infoctx
}

static int
ltl_spin_long (model_t self, int group, int *src, TransitionCB cb,
           void *user_context)
{
    (void)self;
    cb_context new_ctx = {cb, user_context, src, 0};
    return GBgetTransitionsLong(ctx->parent, group, src, ltl_spin_cb, &new_ctx);
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
    (void)self;
    cb_context new_ctx = {cb, user_context, src, 0};
    return GBgetTransitionsAll(ctx->parent, src, ltl_spin_cb, &new_ctx);
}

/*
 * TYPE TEXTBOOK
 */
void ltl_textbook_cb (void*context,transition_info_t*ti,int*dst) {
    cb_context *infoctx = (cb_context*)context;
    // copy dst, append ltl never claim in lockstep
    int dst_buchi[ctx->len];
    int dst_pred[1] = {0}; // assume < 32 predicates..
    memcpy(dst_buchi, dst, ctx->len * sizeof(int) );
    dst_buchi[ctx->ltl_idx] = 0;        /* XXX remove? */
    // evaluate predicates
    for(int i=0; i < ctx->ba->predicate_count; i++) {
        if (eval_predicate(ctx->ba->predicates[i], ti, dst)) /* textbook: dst instead of src */
            dst_pred[0] |= (1 << i);
    }

    int i = infoctx->src[ctx->ltl_idx];
    if (i == -1) { i=0; } /* textbook: extra initial state */

    for(int j=0; j < ctx->ba->states[i]->transition_count; j++) {
        // check predicates
        if ((dst_pred[0] & ctx->ba->states[i]->transitions[j].pos[0]) == ctx->ba->states[i]->transitions[j].pos[0] &&
            (dst_pred[0] & ctx->ba->states[i]->transitions[j].neg[0]) == 0) {
            // perform transition
            dst_buchi[ctx->ltl_idx] = ctx->ba->states[i]->transitions[j].to_state;

            // callback, emit new state, move allowed
            infoctx->cb(infoctx->user_context, ti, dst_buchi);
            ++infoctx->ntbtrans;
            /* debug
            {
            for(int k=0 ; k < ctx->len; k++)
                printf("%x ", ti->src[k]); printf(" ->");
            for(int k=0 ; k < ctx->len; k++)
                printf("%x ", dst_buchi[k]); printf("\n");
            }
            */
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
ltl_textbook_all (model_t self, int *src, TransitionCB cb,
         void *user_context)
{
    (void)self;
    cb_context new_ctx = {cb, user_context, src, 0};
    if (src[ctx->ltl_idx] == -1) {         /* XXX textbook/spin not reversed? */
        transition_info_t ti = GB_TI(NULL, -1); /* XXX int[nedge_labels] */
        ltl_textbook_cb(&new_ctx, &ti, src);
        return new_ctx.ntbtrans;
    } else {
        return GBgetTransitionsAll(ctx->parent, src, ltl_textbook_cb, &new_ctx);
    }
}

void print_ltsmin_buchi(const ltsmin_buchi_t *ba)
{
    Warning(info, "buchi has %d states", ba->state_count);
    for(int i=0; i < ba->state_count; i++) {
        Warning(info, " state %d: %s", i, ba->states[i]->accept ? "accepting" : "non-accepting");
        for(int j=0; j < ba->states[i]->transition_count; j++) {
            char buf[4096];
            memset(buf, 0, sizeof(buf));
            char* at = buf;
            *at = '\0';
            for(int k=0; k < ba->predicate_count; k++) {
                if (ba->states[i]->transitions[j].pos[k/32] & (1<<(k%32))) {
                    if (at != buf) { sprintf(at, " & "); at += strlen(at); }
                    at = ltsmin_expr_print_ltl(ba->predicates[k], at);
                }
                if (ba->states[i]->transitions[j].neg[k/32] & (1<<(k%32))) {
                    if (at != buf) { sprintf(at, " & "); at += strlen(at); }
                    *at++ = '!';
                    at = ltsmin_expr_print_ltl(ba->predicates[k], at);
                }
            }
            if (at == buf) sprintf(at, "true");
            Warning(info, "  -> %d, | %s", ba->states[i]->transitions[j].to_state, buf);
        }
    }
}

/*
 * SHARED
 * por_model: if por layer is added por_model points to the model returned by the layer,
 *            otherwise this parameter should be NULL
 */
model_t
GBaddLTL (model_t model, const char *ltl_file, pins_ltl_type_t type, model_t por_model)
{
    Warning(info,"Initializing LTL layer.., formula file %s", ltl_file);

    lts_type_t ltstype = GBgetLTStype(model);

    {
        int idx = GBgetAcceptingStateLabelIndex (model);
        if (idx != -1) {
            Abort ("LTL layer initialization failed, model already has a ``%s'' property",
                  lts_type_get_state_label_name (ltstype,idx));
        }
    }

    ltsmin_expr_t ltl = ltl_parse_file(ltstype, ltl_file);
    ltsmin_expr_t notltl = LTSminExpr(UNARY_OP, LTL_NOT, 0, ltl, NULL);
    ltsmin_ltl2ba(notltl);
    const ltsmin_buchi_t* ba = ltsmin_buchi();
    if (NULL == ba)
        Abort ("Empty buchi automaton.");
    print_ltsmin_buchi(ba);
    if (ba->predicate_count > 30)
        Abort("more than 30 predicates in buchi automaton are currently not supported");

    model_t         ltlmodel = GBcreateBase ();

    ctx = RTmalloc (sizeof *ctx);
    ctx->parent = model;
    ctx->ba = ba;
    GBsetContext(ltlmodel, ctx);

    // copy and extend ltstype
    int ltl_idx = lts_type_get_state_length(ltstype);
    // set in context for later use in function
    ctx->ltl_idx = ltl_idx;
    ctx->len = ltl_idx + 1;
    lts_type_t ltstype_new = lts_type_clone(ltstype);
    // set new length
    lts_type_set_state_length(ltstype_new, ctx->len);
    // add type
    int type_count = lts_type_get_type_count(ltstype_new);
    int ltl_type = lts_type_add_type(ltstype_new, "buchi", NULL);
    // sanity check, type ltl is new (added last)
    assert (ltl_type == type_count);
    int bool_is_new;
    int bool_type = lts_type_add_type (ltstype_new, "bool", &bool_is_new);

    matrix_t       *p_sl = GBgetStateLabelInfo (model);
    int             sl_count = dm_nrows (p_sl);
    int             sl_len = dm_ncols (p_sl);
    ctx->sl_idx_accept = sl_count;
    GBsetAcceptingStateLabelIndex (ltlmodel, ctx->sl_idx_accept);

    // add name
    lts_type_set_state_name(ltstype_new, ltl_idx, "ltl");
    lts_type_set_state_typeno(ltstype_new, ltl_idx, ltl_type);

    // copy state labels
    lts_type_set_state_label_count (ltstype_new, sl_count+1);
    for (int i = 0; i < sl_count; ++i) {
        lts_type_set_state_label_name (ltstype_new, i,
                                       lts_type_get_state_label_name(ltstype,i));
        lts_type_set_state_label_typeno (ltstype_new, i,
                                         lts_type_get_state_label_typeno(ltstype,i));
    }
    lts_type_set_state_label_name (ltstype_new, ctx->sl_idx_accept,
                                   "buchi_accept_pins");
    lts_type_set_state_label_typeno (ltstype_new, ctx->sl_idx_accept, bool_type);

    // XXX This messes up the trace, the chunk maps now is one index short! Fixed below
    GBcopyChunkMaps(ltlmodel, model);

    // set new type
    GBsetLTStype(ltlmodel, ltstype_new);

    // extend the chunk maps
    GBgrowChunkMaps(ltlmodel, type_count);

    if (bool_is_new) {
        int         idx_false = GBchunkPut(ltlmodel, bool_type, chunk_str("false"));
        int         idx_true  = GBchunkPut(ltlmodel, bool_type, chunk_str("true"));
        assert (idx_false == 0);
        assert (idx_true == 1);
        (void)idx_false; (void)idx_true;
    }

    matrix_t       *p_new_dm = (matrix_t*) RTmalloc(sizeof(matrix_t));
    matrix_t       *p_new_dm_r = (matrix_t*) RTmalloc(sizeof(matrix_t));
    matrix_t       *p_new_dm_w = (matrix_t*) RTmalloc(sizeof(matrix_t));
    matrix_t       *p_dm = GBgetDMInfo (model);
    matrix_t       *p_dm_r = GBgetDMInfoRead (model);
    matrix_t       *p_dm_w = GBgetDMInfoWrite (model);

    // add one column to the matrix
    int             groups = dm_nrows( p_dm );
    int             len = dm_ncols( p_dm );

    // copy matrix, add buchi automaton
    dm_create(p_new_dm, groups, len+1);
    dm_create(p_new_dm_r, groups, len+1);
    dm_create(p_new_dm_w, groups, len+1);
    for(int i=0; i < groups; i++) {
        // copy old matrix rows
        for(int j=0; j < len; j++) {
            if (dm_is_set(p_dm, i, j))
                dm_set(p_new_dm, i, j);
            if (dm_is_set(p_dm_r, i, j))
                dm_set(p_new_dm_r, i, j);
            if (dm_is_set(p_dm_w, i, j))
                dm_set(p_new_dm_w, i, j);
        }
        // add buchi as dependent
        dm_set(p_new_dm, i, len);
        dm_set(p_new_dm_r, i, len);
        dm_set(p_new_dm_w, i, len);
    }
    // mark the parts the buchi automaton uses for reading
    for(int k=0; k < ba->predicate_count; k++) {
        mark_predicate(ba->predicates[k], p_new_dm);
        mark_predicate(ba->predicates[k], p_new_dm_r);
    }
    GBsetDMInfo(ltlmodel, p_new_dm);
    GBsetDMInfoRead(ltlmodel, p_new_dm_r);
    GBsetDMInfoWrite(ltlmodel, p_new_dm_w);

    // create new state label matrix
    matrix_t       *p_new_sl = RTmalloc (sizeof *p_new_sl);

    dm_create(p_new_sl, sl_count+1, sl_len+1);
    // copy old matrix
    for (int i=0; i < sl_count; ++i) {
        for (int j=0; j < sl_len; ++j) {
            if (dm_is_set(p_sl, i, j))
                dm_set(p_new_sl, i, j);
        }
    }
    dm_set(p_new_sl, ctx->sl_idx_accept, ctx->ltl_idx);

    GBsetStateLabelInfo(ltlmodel, p_new_sl);
    // Now overwrite the state label functions to catch the new state label
    GBsetStateLabelShort (ltlmodel, ltl_sl_short);
    GBsetStateLabelLong (ltlmodel, ltl_sl_long);
    GBsetStateLabelsAll (ltlmodel, ltl_sl_all);

    lts_type_validate(ltstype_new);

    // mark visible groups in POR layer
    if (por_model) {
        int *visibility = RTmallocZero( groups * sizeof(int) );
        for(int k=0; k < ba->predicate_count; k++)
            mark_visible(ba->predicates[k], p_new_dm_w, visibility);
        bitvector_t *bv = RTmalloc (sizeof *bv);
        bitvector_create(bv, groups);
        for(int i=0; i < groups; i++)
            if (visibility[i]) bitvector_set_atomic(bv, i);
        GBsetPorVisibility (por_model, bv);
        RTfree(visibility);
    }

    if (type == PINS_LTL_SPIN) {
        GBsetNextStateLong  (ltlmodel, ltl_spin_long);
        GBsetNextStateShort (ltlmodel, ltl_spin_short);
        GBsetNextStateAll   (ltlmodel, ltl_spin_all);
    } else {
        GBsetNextStateLong  (ltlmodel, ltl_textbook_long);
        GBsetNextStateShort (ltlmodel, ltl_textbook_short);
        GBsetNextStateAll   (ltlmodel, ltl_textbook_all);
    }

    GBinitModelDefaults (&ltlmodel, model);

    int             s0[ctx->len];
    GBgetInitialState (model, s0);
    // set buchi initial state
    s0[ctx->ltl_idx] = (type == PINS_LTL_SPIN ? 0 : -1);

    GBsetInitialState (ltlmodel, s0);

    return ltlmodel;
}
