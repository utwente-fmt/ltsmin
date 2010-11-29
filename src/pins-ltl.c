#include <config.h>
#include <stdlib.h>
#include <limits.h>

#include <ltl2ba.h>
#undef Debug
#include <dm/dm.h>
#include <greybox.h>
#include <runtime.h>
#include <ltsmin-syntax.h>
#include <ltsmin-tl.h>
#include <ltsmin-buchi.h>

// TODO fix include file
void ltsmin_ltl2ba(ltsmin_expr_t);
ltsmin_buchi_t *ltsmin_buchi();
// TODO

typedef struct cb_context {
    TransitionCB cb;
    void* user_context;
    int*  src;
} cb_context;

typedef struct ltl_context {
    model_t             parent;
    int                 ltl_idx;
    int                 len;
    ltsmin_buchi_t     *ba;
} ltl_context_t;

ltl_context_t *ctx;

static int tmp_count = 0;

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
            Fatal(1, error, "unbound variable in ltl expression");
        default: {
            char buf[1024];
            ltsmin_expr_print_ltl(e, buf);
            Fatal(1, error, "unhandled predicate expression: %s", buf);
        }
    }
    return 0;
    (void)ti;
}

/*********************************
 * TYPE SPIN
 *********************************/
void ltl_spin_cb (void*context,transition_info_t*ti,int*dst) {
#define infoctx ((cb_context*)context)
    // copy dst, append ltl never claim in lockstep
    int dst_buchi[ctx->len];
    int dst_pred[1] = {0}; // assume < 32 predicates..
    memcpy(&dst_buchi, dst, ctx->len * sizeof(int) );
    dst_buchi[ctx->ltl_idx] = 0;
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
            tmp_count++;
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
    (void)group;
    (void)src;
    (void)cb;
    (void)user_context;
    Fatal(1,error,"Using LTL layer --grey? --reach? ? Still on todo list ;)");
}

static int
ltl_spin_short (model_t self, int group, int *src, TransitionCB cb,
           void *user_context)
{
    (void)self;
    (void)group;
    (void)src;
    (void)cb;
    (void)user_context;
    Fatal(1,error,"Using LTL layer --cached?  Still on todo list ;)");
}


static int
ltl_spin_all (model_t self, int *src, TransitionCB cb,
         void *user_context)
{
    (void)self;
    // TODO: Stefan: use src in new_ctx instead of transition_info...
    cb_context new_ctx = {cb, user_context, src};
    return GBgetTransitionsAll(ctx->parent, src, ltl_spin_cb, &new_ctx);
}

/*********************************
 * TYPE TEXTBOOK
 *********************************/
void ltl_textbook_cb (void*context,transition_info_t*ti,int*dst) {
#define infoctx ((cb_context*)context)
    // copy dst, append ltl never claim in lockstep
    int dst_buchi[ctx->len];
    int dst_pred[1] = {0}; // assume < 32 predicates..
    memcpy(&dst_buchi, dst, ctx->len * sizeof(int) );
    dst_buchi[ctx->ltl_idx] = 0;
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
            tmp_count++;
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
#undef infoctx
}

static int
ltl_textbook_long (model_t self, int group, int *src, TransitionCB cb,
           void *user_context)
{
    (void)self;
    (void)group;
    (void)src;
    (void)cb;
    (void)user_context;
    Fatal(1,error,"Using LTL layer --grey? --reach? ? Still on todo list ;)");
}

static int
ltl_textbook_short (model_t self, int group, int *src, TransitionCB cb,
           void *user_context)
{
    (void)self;
    (void)group;
    (void)src;
    (void)cb;
    (void)user_context;
    Fatal(1,error,"Using LTL layer --cached?  Still on todo list ;)");
}


static int
ltl_textbook_all (model_t self, int *src, TransitionCB cb,
         void *user_context)
{
    (void)self;
    // TODO: Stefan: use src in new_ctx instead of transition_info...
    cb_context new_ctx = {cb, user_context, src};
    if (src[ctx->ltl_idx] == -1) {
        transition_info_t ti = {NULL, -1};
        tmp_count = 0;
        ltl_textbook_cb(&new_ctx, &ti, src);
        return tmp_count;
    } else {
        return GBgetTransitionsAll(ctx->parent, src, ltl_textbook_cb, &new_ctx);
    }
}

/**********************
 * SHARED
 **********************/
int
ltl_is_accepting(int *state)
{
    return ctx->ba->states[state[ctx->ltl_idx]]->accept;
}

model_t
GBaddLTL (model_t model, char* ltl_file, pins_ltl_type_t type)
{
    Warning(info,"Initializing LTL layer.., formula file %s", ltl_file);
    lts_type_t ltstype = GBgetLTStype(model);
    ltsmin_expr_t ltl = ltl_parse_file(ltstype, ltl_file);
    ltsmin_ltl2ba(ltl);
    ltsmin_buchi_t* ba = ltsmin_buchi();

    Warning(info, "buchi has %d states", ba->state_count);
    for(int i=0; i < ba->state_count; i++) {
        Warning(info, " state %d: %s", i, ba->states[i]->accept ? "accepting" : "non-accepting");
        for(int j=0; j < ba->states[i]->transition_count; j++) {
            char buf[4096];
            char* at = buf;
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

    if (ba->predicate_count > 30)
        Fatal(1, error, "more then 30 predicates in buchi automaton are currently not supported");

    model_t             ltlmodel = GBcreateBase ();

    ctx = RTmalloc (sizeof *ctx);
    ctx->parent = model;
    ctx->ba = ba;
    GBsetContext(ltlmodel, ctx);

    // copy and extend ltstype
    int ltl_idx = lts_type_get_state_length(ltstype);
    // set in context for later use in function
    ctx->ltl_idx = ltl_idx;
    ctx->len = ltl_idx + 1;
    GBcopyChunkMaps(ltlmodel, model);
    lts_type_t ltstype_new = lts_type_clone(ltstype);
    // set new length
    lts_type_set_state_length(ltstype_new, ltl_idx+1);
    // add type
    int type_count = lts_type_get_type_count(ltstype_new);
    int ltl_type = lts_type_add_type(ltstype_new, "buchi", NULL);
    // sanity check, type ltl is new (added last)
    if (ltl_type != type_count) Fatal(1, error, "sanity check on type ltl failed");

    // add name
    lts_type_set_state_name(ltstype_new, ltl_idx, "ltl");
    lts_type_set_state_typeno(ltstype_new, ltl_idx, ltl_type);

    // set new type
    GBsetLTStype(ltlmodel, ltstype_new);

    matrix_t           *p_new_dm = (matrix_t*) RTmalloc(sizeof(matrix_t));
    matrix_t           *p_dm = GBgetDMInfo (model);

    // add one column to the matrix
    int groups = dm_nrows( p_dm );
    int len = dm_ncols( p_dm );

    // TODO: mark the parts used for reading?
    dm_create(p_new_dm, groups, len+1);
    for(int i=0; i < groups; i++) {
        for(int j=0; j < len+1; j++) {
            // add buchi as dependent
            if (j == len) {
                dm_set(p_new_dm, i, j);
            } else {
                if (dm_is_set(p_dm, i, j))
                    dm_set(p_new_dm, i, j);
            }
        }
    }
    GBsetDMInfo(ltlmodel, p_new_dm);
    // TODO DMReadInfo, DMWriteInfo

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

    int                 s0[len+1];
    GBgetInitialState (model, s0);
    // set buchi initial state
    s0[len] = (type == PINS_LTL_SPIN? 0 : -1);

    GBsetInitialState (ltlmodel, s0);

    return ltlmodel;
}
