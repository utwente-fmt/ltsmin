#include <hre/config.h>

#include <stdlib.h>

#include <hre/user.h>
#include <ltsmin-lib/ltsmin-standard.h>
#include <pins-lib/pins-util.h>
#include <pins-lib/pins2pins-check.h>
#include <util-lib/is-balloc.h>
#include <util-lib/util.h>

int PINS_CORRECTNESS_CHECK = 0;

struct poptOption check_options[]={
    { "check", 0, POPT_ARG_VAL|POPT_ARGFLAG_DOC_HIDDEN, &PINS_CORRECTNESS_CHECK, 1,
      "Check correctness of DM (Read / Write and MustWrite)", NULL },
    POPT_TABLEEND
};

#define abort_if(e, ...) \
    if (EXPECT_FALSE(e)) {\
        Print(error, __VA_ARGS__);\
        HREabort(HRE_EXIT_FAILURE);\
    }


typedef struct check_ctx_s {
    model_t             parent;
    int                 N;
    int                 K;
    int                 L;
    int                 S;
    ci_list           **read;
    ci_list           **must;
    matrix_t           *may;
    int                *magic[2];

    // for collect:
    TransitionCB        user_cb;
    void               *user_ctx;
    isb_allocator_t     stack;
    int                *src;
    int                 group;
    ci_list            *check_must;
    int                 reentrent;      // check whether function is re-entered

    // for compare:
    int                *src2;
    int                 call_idx;
    bool                comparison_failed;

    // check_must
    int                *src3;

    // for find:
    int                 idx;
} check_ctx_t;

static char *
str_group (check_ctx_t *ctx, int group)
{
    model_t         model = ctx->parent;
    lts_type_t      ltstype = GBgetLTStype (model);
    int             label = lts_type_find_edge_label (ltstype, LTSMIN_EDGE_TYPE_STATEMENT);
    if (label) return "NULL";
    int             type = lts_type_get_edge_label_typeno (ltstype, label);
    int             count = pins_chunk_count  (model, type);
    if (count < ctx->K) return "NULL";
    chunk           c = pins_chunk_get (model, type, group);
    return c.data;
}

int
print_chunk (model_t model, char *res, int max, int typeno, int val)
{
    chunk           c;
    switch (lts_type_get_format (GBgetLTStype(model), typeno))
    {
        case LTStypeDirect:
        case LTStypeRange:
            return snprintf (res, max, "%d", val);
        case LTStypeEnum:
        case LTStypeChunk:
            c = pins_chunk_get (model, typeno, val);
            return snprintf (res, max, "%s", c.data);
        default: {
            HREassert(false);
            return -1;
        }
    }
}

static char *
str_slot (check_ctx_t *ctx, int *s, int i)
{
    model_t         model = ctx->parent;
    lts_type_t      ltstype = GBgetLTStype (model);
    char           *name = lts_type_get_state_name (ltstype, i);
    char           *type = lts_type_get_state_type (ltstype, i);
    int             typeno = lts_type_get_state_typeno (ltstype, i);
    int             max = 4096;
    char           *res = RTmalloc (max);
    int             l = snprintf (res, max, "%s : %s = ", name, type);
    if (s != NULL) {
        print_chunk (model, res+l, max-l, typeno, s[i]);
    } else {
        res[l-2] = '\0';
    }
    return res;
}

static inline void
print_ti (check_ctx_t *ctx, transition_info_t *ti)
{
    if (ti == NULL || ti->labels == NULL) return;
    model_t         model = ctx->parent;
    lts_type_t      ltstype = GBgetLTStype (model);
    int             Max = 4096;
    char           *tmp = RTmalloc (Max);
    int             l;
    for (int i = 0; i < ctx->L; i++) {
        char           *name = lts_type_get_edge_label_name (ltstype, i);
        char           *type = lts_type_get_edge_label_type (ltstype, i);
        int             typeno = lts_type_get_edge_label_typeno (ltstype, i);

        char           *res = tmp;
        l  = snprintf (res, Max, " --> %s : %s = ", name, type);
        l += print_chunk (model, res+l, Max-l, typeno, ti->labels[i]);
        Printf (error, "%s\n", res);
    }
    RTfree (tmp);
}

static inline void
report (char *s, check_ctx_t *ctx, int g, int i, int *v, transition_info_t *ti)
{
    Printf (error, "\n\n%s: %d X %d (group X slot)\n\n\n", s, g, i);
    print_ti (ctx, ti);
    Printf (error, "\n %s\n \tX\n %s\n \n ",
            str_group (ctx, g), str_slot (ctx, v, i));
    HREabort (LTSMIN_EXIT_FAILURE);
}

/**
 * Fill vector with other values at non-read places
 */
static int *
copy_vec (check_ctx_t *ctx, int group, int *src)
{
    for (int i = 0; i < ctx->N; i++) {
        const int           val[2] = { ctx->magic[0][i], ctx->magic[1][i] };
        ctx->src2[i] = val[src[i] == val[0]];
    }
    ci_list            *row = ctx->read[group];
    for (int *i = ci_begin (row); i != ci_end (row); i++)
        ctx->src2[*i] = src[*i];
    return ctx->src2;
}

static inline int
find_diff (check_ctx_t *ctx, int *dst, int *dst2)
{
    for (int idx = 0; idx < ctx->N; idx++) {
        if ((ctx->src[idx] == ctx->src2[idx] || //  read
             ctx->src[idx] != dst[idx]) &&      // !copied
             dst[idx] != dst2[idx])
            return idx;
    }
    return ctx->N;
}

static void
find (void *context, transition_info_t *ti, int *dst2, int *cpy)
{
    check_ctx_t        *ctx = (check_ctx_t *) context;

    abort_if (ti->group != ctx->group,
              "Transition info does not return correct group for group %d (found: %d)",
              ctx->group, ti->group);

    int                *dst = isba_index (ctx->stack, ctx->call_idx++);
    int                 idx = find_diff (ctx, dst, dst2);
    ctx->comparison_failed |= ( idx != ctx->N );

    int             found = isba_size_int(ctx->stack);
    if (!ctx->comparison_failed && found == ctx->call_idx) {
        report ("Read dependency missing", ctx, ctx->group, ctx->idx, ctx->src2, ti);
    }

    (void) cpy;
}

static void
find_culprit_slot (check_ctx_t *ctx, int g)
{
    int             count;

    for (int i = 0; i < ctx->N; i++) {
        while (ctx->src2[i] == ctx->src[i]) {
            i++;
            abort_if (i >= ctx->N, "No-deterministic GBnextLong for group %d", g);
        }
        ctx->src2[i] = ctx->src[i]; // fill

        // retry with src2 more similar to src:
        ctx->idx = i;
        ctx->call_idx = 0;
        ctx->comparison_failed = false;
        count = GBgetTransitionsLong (ctx->parent, g, ctx->src2, find, ctx);
        abort_if (count != ctx->call_idx , "Wrong count returned by GBnextLong(compare): %d (Found: %d).",
                  count, ctx->call_idx);
    }
    HREassert (false);
}

static void
collect (void *context, transition_info_t *ti, int *dst, int *cpy)
{
    check_ctx_t        *ctx = (check_ctx_t *) context;

    abort_if (ti->group != ctx->group,
              "Transition info does not return correct group for group %d (found: %d)",
              ctx->group, ti->group);

    for (int i = 0; i < ctx->N; i++) {
         if (!dm_is_set(ctx->may, ctx->group, i) && ctx->src[i] != dst[i]) {
             report("May write dependency missing", ctx, ctx->group, i, dst, ti);
         }
    }

    ctx->user_cb (ctx->user_ctx, ti, dst, cpy);

    ci_list            *row = ctx->must[ctx->group];
    for (int *i = ci_begin (row); i != ci_end (row); i++) {
        if (dst[*i] == ctx->src[*i]) ci_add (ctx->check_must, *i);
    } // in second round we recheck

    isba_push_int (ctx->stack, dst);
}

static void
dbg_found_read_dep_error (check_ctx_t *ctx, int *dst, int *dst2, int idx)
{
    char               *res = RTmalloc (1024);
    lts_type_t          ltstype = GBgetLTStype (ctx->parent);
    int                 typeno = lts_type_get_state_typeno (ltstype, idx);
    print_chunk (ctx->parent, res, 1024, typeno, dst[idx]);
    Warning (error, "Found missing read dependency in group %d (diff slot: %d -- %s != %s).\\"
                    "Identifying culprit read slot...",
             ctx->group, idx, str_slot(ctx, dst2, idx), res);
    RTfree (res);
}

static void
compare (void *context, transition_info_t *ti, int *dst2, int *cpy)
{
    check_ctx_t        *ctx = (check_ctx_t *) context;

    abort_if (ti->group != ctx->group,
              "Transition info does not return correct group for group %d (found: %d)",
              ctx->group, ti->group);

    int                *dst = isba_index (ctx->stack, ctx->call_idx++);
    int                 idx = find_diff (ctx, dst, dst2);
    ctx->comparison_failed |= ( idx != ctx->N );

    if (ctx->comparison_failed && isba_size_int(ctx->stack) == (size_t)ctx->call_idx) {
        dbg_found_read_dep_error (ctx, dst, dst2, idx);
    }

    for (int *i = ci_begin (ctx->check_must); i != ci_end (ctx->check_must); i++) {
        if (ctx->src2[*i] != ctx->src[*i] && dst2[*i] == ctx->src2[*i]) {
            report ("Must write dependency violated", ctx, ctx->group, *i, ctx->src, ti);
        }
    }
    (void) cpy;
}

static int
check_long (model_t model, int g, int *src, TransitionCB cb, void *context)
{
    check_ctx_t    *ctx = GBgetContext (model);
    int             count;
    int             found;

    // collect
    ctx->src = src;
    ctx->group = g;
    ctx->user_cb = cb;
    ctx->user_ctx = context;
    ci_clear (ctx->check_must);
    isba_discard_int (ctx->stack, isba_size_int(ctx->stack));
    HREassert (!ctx->reentrent, "INTERFACE ERROR: GBgetTransitions* is not re-entrant");
    ctx->reentrent = 1;
    count = GBgetTransitionsLong (ctx->parent, g, src, collect, ctx);
    ctx->reentrent = 0;
    found = isba_size_int(ctx->stack);
    abort_if (count != found, "Wrong count returned by GBnextLong(collect): %d (Found: %d).",
              count, found);

    // compare
    ctx->src2 = copy_vec (ctx, g, src);
    ctx->comparison_failed = false;
    ctx->call_idx = 0;
    count = GBgetTransitionsLong (ctx->parent, g, ctx->src2, compare, ctx);
    abort_if (count != ctx->call_idx , "Wrong count returned by GBnextLong(compare): %d (Found: %d).",
              count, ctx->call_idx );

    if (ctx->call_idx != found || ctx->comparison_failed) {
        find_culprit_slot (ctx, g);
    }

    return count;
}

static int
check_all (model_t model, int *src, TransitionCB cb, void *context)
{
    check_ctx_t    *ctx = GBgetContext (model);
    int res = 0;
    for (int i = 0; i < ctx->K; i++) {
        res += check_long (model, i, src, cb, context);
    }
    return res;
}

static int
check_short (model_t model, int group, int *src, TransitionCB cb, void *context)
{
    abort_if (true, "Checking layer designed for algorithms using PINS long/all calls");
    (void) model; (void) group; (void) src; (void) cb; (void) context;
    return 0;
}

static int
type_min (check_ctx_t *ctx, int idx)
{
    model_t         model = ctx->parent;
    lts_type_t      ltstype = GBgetLTStype (model);
    int             typeno = lts_type_get_state_typeno (ltstype, idx);
    switch (lts_type_get_format (ltstype, typeno))
    {
        case LTStypeRange:
            return lts_type_get_min (ltstype, typeno);
        case LTStypeDirect:
        case LTStypeEnum:
        case LTStypeChunk:
        case LTStypeBool:
        case LTStypeTrilean:
        case LTStypeSInt32:
            return 0;
        default: {
            HREassert(false);
            return -1;
        }
    }
}

static int
type_max (check_ctx_t *ctx, int idx)
{
    model_t         model = ctx->parent;
    lts_type_t      ltstype = GBgetLTStype (model);
    int             typeno = lts_type_get_state_typeno (ltstype, idx);
    int              c;
    switch (lts_type_get_format (ltstype, typeno))
    {
        case LTStypeDirect:
            GBgetInitialState (model, ctx->src2);
            c = ctx->src2[idx];
            return c == 0 ? 1 : c;
        case LTStypeRange:
            return lts_type_get_min (ltstype, typeno);
        case LTStypeEnum:
            c = pins_chunk_count (model, typeno);
            HREassert (c > 0, "Empty enum table for slot: %d -- %s", idx, str_slot(ctx, NULL, idx));
            return c;
        case LTStypeChunk:
            c = pins_chunk_count (model, typeno);
            return c == 0 ? 1 : c;
        case LTStypeBool:
            return 1;
        case LTStypeTrilean:
            return 2;
        case LTStypeSInt32:
            return (1UL<<31) - 1;
        default: {
                HREassert(false);
                return -1;
        }
    }
}

model_t
GBaddCheck (model_t model)
{
    HREassert (model != NULL, "No model");
    if (!PINS_CORRECTNESS_CHECK) return model;

    Print1 (info, "Matrix checking layer activated.");

    model_t             check = GBcreateBase ();

    check_ctx_t        *ctx = RTmalloc (sizeof(check_ctx_t));
    ctx->N = pins_get_state_variable_count (model);
    ctx->K = pins_get_group_count (model);
    ctx->L = pins_get_edge_label_count (model);
    ctx->S = pins_get_state_label_count (model);
    ctx->src2 = RTmalloc(sizeof(int[ctx->N]));
    ctx->check_must = ci_create (ctx->N);
    ctx->read = (ci_list **) dm_rows_to_idx_table (GBgetDMInfoRead(model));
    ctx->must = (ci_list **) dm_rows_to_idx_table (GBgetDMInfoMustWrite(model));
    ctx->may = GBgetDMInfoMayWrite(model);
    ctx->stack = isba_create (ctx->N);
    ctx->parent = model;
    ctx->magic[0] = RTmalloc(sizeof(int[ctx->N]));
    ctx->magic[1] = RTmalloc(sizeof(int[ctx->N]));
    for (int i = 0; i < ctx->N; i++) {
        int         max = type_max (ctx, i);
        int         min = type_min (ctx, i);
        int         c = max - min;
        HREassert (c > 0, "Empty type range for slot: %d -- %s", i, str_slot(ctx, NULL, i));
        ctx->magic[0][i] = min;
        ctx->magic[1][i] = min + 1;
    }
    ctx->reentrent = 0;

    GBsetContext (check, ctx);
    GBsetNextStateAll (check, check_all);
    GBsetNextStateLong (check, check_long);
    GBsetNextStateShort (check, check_short);
    //GBsetActionsLong (check, check_long);
    //GBsetActionsShort (check, check_short);

    GBinitModelDefaults (&check, model);

    return check;
}

