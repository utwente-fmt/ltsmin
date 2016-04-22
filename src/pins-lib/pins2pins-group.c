#include <hre/config.h>

/**
 * TODO: Remove GBgetVarPerm and GBgetColPerm from PINS
 *       Can we totally remove them? The language module can anyway reorder itself
 */

#include <ctype.h>
#include <float.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <math.h>

#include <dm/dm.h>
#include <hre/unix.h>
#include <hre/user.h>
#include <ltsmin-lib/lts-type.h>
#include <ltsmin-lib/ltsmin-standard.h>
#include <pins-lib/pins.h>
#include <pins-lib/pins2pins-group.h>
#include <pins-lib/pins2pins-guards.h>
#include <pins-lib/pins-util.h>
#include <util-lib/dynamic-array.h>
#include <util-lib/util.h>

#ifdef HAVE_BOOST
#include <dm/dm_boost.h>
#endif

#ifdef HAVE_VIENNACL
#include <dm/dm_viennacl.h>
#endif

#define                 USE_GUARDS_OPTION "pins-guards"

static const char      *regroup_spec = NULL;
static int              cw_max_cols = -1;
static int              cw_max_rows = -1;
static const char      *col_ins = NULL;
static int mh_timeout = -1;
static char            *row_perm = NULL;
static char            *col_perm = NULL;
static int              graph_metrics = 0;
static int              group_exit = 0;
static int              group_time = 0;
int                     PINS_USE_GUARDS = 0;

struct poptOption group_options[] = {
    { "regroup" , 'r' , POPT_ARG_STRING, &regroup_spec , 0 ,
          "apply transformations to the dependency matrix: "
          "gs, ga, gsa, gc, gr, cs, cn, cw, ca, csa, rs, rn, ru, w2W, r2+, w2+, W2+, -2r, rb4w, mm, sw, sr, sc"
#if defined(HAVE_BOOST) || defined(HAVE_VIENNACL)
      ", bg, tg"
#endif
#ifdef HAVE_BOOST
      ", bcm, bk, bs, bn"
#endif
#ifdef HAVE_VIENNACL
      ", vcm, vacm, vgps, vn"
#endif
        ", f"
          , "<(T,)+>" },
    { "cw-max-cols", 0, POPT_ARG_INT, &cw_max_cols, 0, "if (<num> > 0): don't apply Column sWaps (cw) when there are more than <num> columns", "<num>" },
    { "cw-max-rows", 0, POPT_ARG_INT, &cw_max_rows, 0, "if (<num> > 0): don't apply Column sWaps (cw) when there are more than <num> rows", "<num>" },
    { "col-ins", 0, POPT_ARG_STRING, &col_ins, 0, "insert column C before column C'", "<(C.C',)+>" },
    { "mh-timeout", 0, POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &mh_timeout, 0, "timeout for metaheuristic algorithms (-1 = no timeout)", "<seconds>" },
    { "row-perm", 0, POPT_ARG_STRING, &row_perm, 0, "apply row permutation (R is a row number)", "<(R,)+>" },
    { "col-perm", 0, POPT_ARG_STRING, &col_perm, 0, "apply column permutation (C is a column number)", "<(C,)+>" },
#if defined(HAVE_BOOST) || defined(HAVE_VIENNACL)
    { "graph-metrics", 0, POPT_ARG_NONE, &graph_metrics, 0, "print metrics of the symmetrized dependency matrix", NULL },
#endif
    { "regroup-exit", 0, POPT_ARG_NONE, &group_exit, 0, "exit after regrouping is done", NULL },
    { "regroup-time", 0, POPT_ARG_NONE, &group_time, 0, "print the timing information of each transformation", NULL },
    { USE_GUARDS_OPTION, 'g', POPT_ARG_VAL|POPT_ARGFLAG_DOC_HIDDEN, &PINS_USE_GUARDS, 1,
      "use guards in reordering layer" , NULL},
    POPT_TABLEEND
};

typedef struct group_context {
    int                 len;
    int                *transbegin;
    int                *transmap;
    int                *statemap;
    int                *groupmap;
    TransitionCB        cb;
    void               *user_context;
    int                **group_cpy;
    int                *cpy;
}                  *group_context_t;


static void
group_cb (void *context, transition_info_t *ti, int *olddst, int*cpy)
{
    group_context_t     ctx = (group_context_t)(context);
    int                 len = ctx->len;
    int                 newdst[len];
    int                 newcpy[len];

    // possibly fix group and cpy in case it is merged
    if (ti->group != -1) {
        if (ctx->cpy == NULL) ctx->cpy = ctx->group_cpy[ti->group];
        ti->group = ctx->groupmap[ti->group];
    }

    for (int i = 0; i < len; i++) {
        newdst[i] = olddst[ctx->statemap[i]];
        newcpy[i] = (ctx->cpy[i] || (cpy != NULL && cpy[ctx->statemap[i]]));
    }

    ctx->cb (ctx->user_context, ti, newdst, newcpy);
}

static int
long_ (model_t self, int group, int *newsrc, TransitionCB cb,
      void *user_context, int (*long_proc)(model_t,int,int*,TransitionCB,void*))
{
    struct group_context ctx;
    group_context_t     _ctx = (group_context_t)GBgetContext (self);
    memcpy(&ctx, _ctx, sizeof(struct group_context));
    ctx.cb = cb;
    ctx.user_context = user_context;

    model_t             parent = GBgetParent (self);
    int                 oldsrc[ctx.len];
    int                 Ntrans = 0;
    int                 begin = ctx.transbegin[group];
    int                 end = ctx.transbegin[group + 1];

    for (int i = 0; i < ctx.len; i++)
        oldsrc[ctx.statemap[i]] = newsrc[i];

    for (int j = begin; j < end; j++) {
        int                 g = ctx.transmap[j];
        ctx.cpy = ctx.group_cpy[g];
        Ntrans += long_proc (parent, g, oldsrc, group_cb, &ctx);
    }

    return Ntrans;
}

static int
group_long (model_t self, int group, int *newsrc, TransitionCB cb,
            void *user_context)
{
    return long_(self, group, newsrc, cb, user_context, &GBgetTransitionsLong);
}

static int
actions_long (model_t self, int group, int *newsrc, TransitionCB cb,
            void *user_context)
{
    return long_(self, group, newsrc, cb, user_context, &GBgetActionsLong);
}

static int
group_all (model_t self, int *newsrc, TransitionCB cb,
            void *user_context)
{
    group_context_t     ctx = (group_context_t)GBgetContext (self);
    model_t             parent = GBgetParent (self);
    int                 len = ctx->len;
    int                 oldsrc[len];
    ctx->cb = cb;
    ctx->user_context = user_context;

    for (int i = 0; i < len; i++)
        oldsrc[ctx->statemap[i]] = newsrc[i];

    return GBgetTransitionsAll(parent, oldsrc, group_cb, ctx);
}


static int
group_state_labels_short(model_t self, int label, int *state)
{
    group_context_t     ctx = (group_context_t)GBgetContext (self);
    model_t             parent = GBgetParent (self);

    // this needs to be rewritten
    int len = dm_ones_in_row(GBgetStateLabelInfo(self), label);
    int tmpstate[dm_ncols(GBgetStateLabelInfo(self))];
    int oldtmpstate[dm_ncols(GBgetStateLabelInfo(self))];
    int oldstate[len];
    // basic idea:
    // 1) expand the short state to a long state using the permuted state_label info matrix
    // note: expanded vector uses tmpstate as basis for the missing items in state
    // this "should" work because after undoing the regrouping 
    // project_vector should use only the indices that are in state
    memset (tmpstate, 0, sizeof tmpstate);
    dm_expand_vector(GBgetStateLabelInfo(self), label, tmpstate, state, tmpstate);

    // 2) undo the regrouping on the long state
    for (int i = 0; i < dm_ncols(GBgetStateLabelInfo(self)); i++){
        oldtmpstate[ctx->statemap[i]] = tmpstate[i];
    }
    // 3) project this again to a short state, using the parent's state_label info matrix
    dm_project_vector(GBgetStateLabelInfo(parent), label, oldtmpstate, oldstate);

    return GBgetStateLabelShort(parent, label, oldstate);
}

static int
group_state_labels_long(model_t self, int label, int *state)
{
    group_context_t     ctx = (group_context_t)GBgetContext (self);
    model_t             parent = GBgetParent (self);
    int                 len = ctx->len;
    int                 oldstate[len];

    for (int i = 0; i < len; i++)
        oldstate[ctx->statemap[i]] = state[i];

    return GBgetStateLabelLong(parent, label, oldstate);
}

static void
group_state_labels_all(model_t self, int *state, int *labels)
{
    group_context_t     ctx = (group_context_t)GBgetContext (self);
    model_t             parent = GBgetParent (self);
    int                 len = ctx->len;
    int                 oldstate[len];

    for (int i = 0; i < len; i++)
        oldstate[ctx->statemap[i]] = state[i];

    return GBgetStateLabelsAll(parent, oldstate, labels);
}

/* map groups and delete duplicates, because groups may be subsumed */
static int
group_groups_of_edge (model_t self, int edgeno, int index, int** groups)
{
    group_context_t ctx = (group_context_t) GBgetContext(self);

    int* groups_parent = NULL;
    const int n = GBgroupsOfEdge(GBgetParent(self), edgeno, index, &groups_parent);
    
    if (n == 0) return 0;    

    *groups = RTmalloc(sizeof(int) * n);
    
    int c = 0;
    for (int i = 0; i < n; i++) {
        for (int k = 0; k < c; k++) {
            if (*groups[k] == groups_parent[i]) goto duplicate;
        }
        (*groups)[c++] = ctx->groupmap[groups_parent[i]];
        
        duplicate:;
    }
    
    if (c > 0) RTrealloc(*groups, sizeof(int) * c);
    else {
        RTfree(*groups);
        *groups = NULL;
    }

    return c;
}

static int
group_chunk_pretty_print (model_t self, int i, int chunk_no)
{
    group_context_t  ctx    = (group_context_t)GBgetContext (self);
    model_t          parent = GBgetParent (self);
    return GBchunkPrettyPrint(parent, ctx->statemap[i], chunk_no);
}

typedef struct {
    int src;
    int tgt;
} pair_t;

typedef struct __attribute__((__packed__)) {
    matrix_t*   r;
    matrix_t*   mayw;
    matrix_t*   mustw;
    matrix_t*   old_r;
    matrix_t*   old_mayw;
    matrix_t*   old_mustw;
    pair_t*     pairs;
    int         num_pairs;
    pair_t*     sorted_pairs;
    matrix_t*   combined;
} rw_info_t;

static void
inf_copy_row_headers(const matrix_t *src, rw_info_t *tgt)
{
    if (src != tgt->r) dm_copy_row_info(src, tgt->r);
    if (src != tgt->mayw) dm_copy_row_info(src, tgt->mayw);
    if (src != tgt->mustw) dm_copy_row_info(src, tgt->mustw);
    if (src != tgt->old_r) dm_copy_row_info(src, tgt->old_r);
    if (src != tgt->old_mayw) dm_copy_row_info(src, tgt->old_mayw);
    if (src != tgt->old_mustw) dm_copy_row_info(src, tgt->old_mustw);
    if (tgt->combined != NULL && src != tgt->combined) dm_copy_row_info(src, tgt->combined);
}

static void
inf_copy_col_headers(const matrix_t *src, rw_info_t *tgt)
{
    if (src != tgt->r) dm_copy_col_info(src, tgt->r);
    if (src != tgt->mayw) dm_copy_col_info(src, tgt->mayw);
    if (src != tgt->mustw) dm_copy_col_info(src, tgt->mustw);
    if (tgt->combined != NULL && src != tgt->combined) dm_copy_col_info(src, tgt->combined);
}

int
max_row_first (matrix_t *m, int rowa, int rowb)
{
    int                 i,
                        raw,
                        rbw;

    for (i = 0; i < dm_ncols (m); i++) {
        raw = dm_is_set (m, rowa, i);
        rbw = dm_is_set (m, rowb, i);

        if (((raw) && (rbw)) || (!raw && !rbw))
            continue;
        return ((rbw) - (raw));
    }

    return 0;
}

int
max_col_first (matrix_t *m, int cola, int colb)
{
    int                 i,
                        car,
                        cbr;

    for (i = 0; i < dm_nrows (m); i++) {
        car = dm_is_set (m, i, cola);
        cbr = dm_is_set (m, i, colb);

        if ((car && cbr) || (!car && !cbr))
            continue;
        return (cbr - car);
    }

    return 0;
}

int
write_before_read (matrix_t *m, int rowa, int rowb)
{
    int i;

    int ra = 0;
    int rb = 0;

    for (i = 0; i < dm_ncols (m); i++) {
        if (dm_is_set (m, rowa, i)) ra += (i);
        if (dm_is_set (m, rowb, i)) rb += (i);
    }

    return rb - ra;
}

int
read_before_write (matrix_t *m, int cola, int colb)
{
    int i;

    int ca = 0;
    int cb = 0;

    for (i = 0; i < dm_nrows (m); i++) {
        if (dm_is_set (m, i, cola)) ca += (i);
        if (dm_is_set (m, i, colb)) cb += (i);
    }

    return cb - ca;
}

struct group_info {
    guard_t **guards;
    uint64_t *combined;
    int num_ints_per;
};

static int
eq_guards(guard_t** guards, int i, int j, matrix_t *m) {

    // information about guards has not changed.
    // thus we refer to old rows numbers.
    int old_i = m->row_perm.data[i].becomes;
    int old_j = m->row_perm.data[j].becomes;

    if (guards[old_i]->count != guards[old_j]->count) return 0;

    for (int g = 0; g < guards[old_i]->count; g++) {
        if (guards[old_i]->guard[g] != guards[old_j]->guard[g]) return 0;
    }

    return 1;
}

static int
is_equal(struct group_info *context, int ia, int ib) {
    for (int j = 0; j < context->num_ints_per; j++) {

        // compute the location of the 64-bit integer.
        const int at = ia * context->num_ints_per + j;
        const int bt = ib * context->num_ints_per + j;
        const uint64_t a = *(context->combined + at);
        const uint64_t b = *(context->combined + bt);
        if (a != b) return 0;   // not equal
    }
    return 1;                   // equal
}

static int
eq_rows(matrix_t *m, int rowa, int rowb, void *context)
{
    struct group_info *ctx = (struct group_info*) context;
    if (ctx->guards == NULL || eq_guards(ctx->guards, rowa, rowb, m)) {
        return is_equal(ctx, m->row_perm.data[rowa].becomes, m->row_perm.data[rowb].becomes);
    }
    return 0;                              // unequal
}

static int
is_subsumed(struct group_info *context, int ia, int ib) {
    for (int j = 0; j < context->num_ints_per; j++) {

        // compute the location of the 64-bit integer.
        const int at = ia * context->num_ints_per + j;
        const int bt = ib * context->num_ints_per + j;
        const uint64_t a = *(context->combined + at);
        const uint64_t b = *(context->combined + bt);

        /* Here the idea for subsumption is that
         * if "b" modifies a bit in "a" then a
         * is not larger than or equal to b. */
        if ((a | b) != a) return 0; // not subsumed
    }
    return 1;                       // subsumed
}

int
subsume_rows(matrix_t *m, int rowa, int rowb, void *context)
{
    struct group_info *ctx = (struct group_info*)context;
    if (ctx->guards == NULL || eq_guards(ctx->guards, rowa, rowb, m)) {
        return is_subsumed(ctx, m->row_perm.data[rowa].becomes, m->row_perm.data[rowb].becomes);
    }
    return 0; // not subsumed
}

int
eq_cols(matrix_t *m, int cola, int colb, void *context)
{
    struct group_info *ctx = (struct group_info*) context;
    return is_equal(ctx, m->col_perm.data[cola].becomes, m->col_perm.data[colb].becomes);
}

int
subsume_cols(matrix_t *m, int cola, int colb, void *context)
{
    struct group_info *ctx = (struct group_info*)context;
    return is_subsumed(ctx, m->col_perm.data[cola].becomes, m->col_perm.data[colb].becomes);
}

static void
apply_permutation(rw_info_t *inf, int* row_perm, int* col_perm)
{
    if (row_perm != NULL) {
        permutation_group_t* groups;
        int n;
        dm_create_permutation_groups(&groups, &n, row_perm, dm_nrows(inf->r));
        for (int i = 0; i < n; i++) {
            dm_permute_rows(inf->r, &groups[i]);
            dm_free_permutation_group(&groups[i]);
        }
        inf_copy_row_headers(inf->r, inf);
        RTfree(groups);
    }

    if (col_perm != NULL) {
        permutation_group_t* groups;
        int n;
        dm_create_permutation_groups(&groups, &n, col_perm, dm_ncols(inf->r));
        for (int i = 0; i < n; i++) {
            dm_permute_cols(inf->r, &groups[i]);
            dm_free_permutation_group(&groups[i]);
        }
        inf_copy_col_headers(inf->r, inf);
        RTfree(groups);
    }
}

static const uint16_t READ  = 0x0001; // right most bit denotes read
static const uint16_t WRITE = 0x0002; // middle bit denotes write
static const uint16_t COPY  = 0x0004; // left most bit denotes copy

/* To compare rows with each other may be quadratic in the number of rows.
 * This can be quite expensive. This function allocates 64-bit integers for all rows
 * so that at least every row comparison can by done quickly.
 * In other words, we don't have to compare every individual column of matrices; we
 * can use operations on 64-bit integers. */
static void
prepare_row_compare(rw_info_t *inf, struct group_info* context)
{
    // compute the number of bits per row in the dependency matrix
    const int num_bits = dm_ncols(inf->old_mayw) * 3;

    /* Compute the number of 8-bit integers required to store all the required bits.
     * Also add extra 8-bit integers so that the result is divisible by 64.
     * This allows to compare rows by using 64-bit integers. */
    const int num_ints_per_row = ceil(num_bits / 64.0) * 8;
    context->num_ints_per = num_ints_per_row / 8;
    uint8_t* combined = (uint8_t*) RTalignZero(CACHE_LINE_SIZE, dm_nrows(inf->old_mayw) * sizeof(uint8_t[num_ints_per_row]));
    context->combined = (uint64_t*) combined;

    for (int i = 0; i < dm_nrows(inf->old_mayw); i++) {
        for (int j = 0; j < dm_ncols(inf->old_mayw); j++) {
            /* We temporarily store the three bits in an
             * 16-bit integer, because an 8-bit integer overflows for every third column. */
            uint16_t val = 0;

            /* The offset in the 16-bit integer.
             * For every column the three bits will be shifted left by offset. */
            const int offset = (j * 3) % 8;

            // first set the write bit, so that it can be overwritten if the copy bit is not set.
            if (dm_is_set(inf->old_mustw, i, j)) val |= WRITE << offset;

            // if the may-write entry is set we must set the copy and write bit
            else if (dm_is_set(inf->old_mayw, i, j)) val = val | COPY << offset | WRITE << offset;

            // if the read entry is set we must set the copy and read bit
            if (dm_is_set(inf->old_r, i, j)) val = val | COPY << offset | READ << offset;

            // if a variable is not written we set the copy bit
            else if (!dm_is_set(inf->old_mustw, i, j) && !dm_is_set(inf->old_mayw, i, j)) val |= COPY << offset;

            const int b = inf->old_r->row_perm.data[i].becomes;

            // compute the first 8-bit integer and store val
            const int num_int = (b * num_ints_per_row) + ((j * 3) / 8);
            uint8_t* p = combined + num_int;
            *p |= (uint8_t) val;

            // compute the second 8-bit integer and store the remainder of val
            if (offset >= 6) {
                p++;
                *p |= (uint8_t) (val >> 8);
            }
        }
    }
}

/* Idem for columns. */
static void
prepare_col_compare(rw_info_t *inf, struct group_info* context)
{
    const int num_bits = dm_nrows(inf->old_mayw) * 3;
    const int num_ints_per_col = ceil(num_bits / 64.0) * 8;
    context->num_ints_per = num_ints_per_col / 8;
    uint8_t* combined = (uint8_t*) RTalignZero(CACHE_LINE_SIZE, dm_ncols(inf->old_mayw) * sizeof(uint8_t[num_ints_per_col]));
    context->combined = (uint64_t*) combined;

    for (int i = 0; i < dm_ncols(inf->old_mayw); i++) {
        for (int j = 0; j < dm_nrows(inf->old_mayw); j++) {
            uint16_t val = 0;
            const int offset = (j * 3) % 8;
            if (dm_is_set(inf->old_mustw, j, i)) val |= WRITE << offset;
            else if (dm_is_set(inf->old_mayw, j, i)) val = val | COPY << offset | WRITE << offset;
            if (dm_is_set(inf->old_r, j, i)) val = val | COPY << offset | READ << offset;
            else if (!dm_is_set(inf->old_mustw, j, i) && !dm_is_set(inf->old_mayw, j, i)) val |= COPY << offset;
            const int num_int = (i * num_ints_per_col) + ((j * 3) / 8);
            uint8_t* p = combined + num_int;
            *p |= (uint8_t) val;
            if (offset >= 6) {
                p++;
                *p |= (uint8_t) (val >> 8);
            }
        }
    }
}
static void
print_event_span(const matrix_t* const m)
{
    int* row_spans = RTmalloc(sizeof(int[dm_nrows(m)]));
    dm_row_spans(m, row_spans);

    const double es = dm_event_span(m, row_spans);

    const double wes = dm_weighted_event_span(m, row_spans);

    const double size = dm_ncols(m) * (double) dm_nrows(m);
    const int sig = (int) ceil(log10(size));

    printf("Event Span: %.*g\n", DBL_DIG, es);
    printf("Normalized Event Span: %.*g\n",
        sig, es / size);
    printf("Weighted Event Span, moment 1 (WES^1): %.*g\n",
        sig, wes);
    printf("Normalized Weighted Event Span, moment 1: %.*g\n",
        sig, wes / size);

    RTfree(row_spans);
}

static void
apply_regroup_spec (rw_info_t *inf, const char *spec_, guard_t **guards, const char* sep)
{
    HREassert(
            dm_ncols(inf->r) == dm_ncols(inf->mayw) &&
            dm_nrows(inf->r) == dm_nrows(inf->mayw) &&
            dm_ncols(inf->r) == dm_ncols(inf->mustw) &&
            dm_nrows(inf->r) == dm_nrows(inf->mustw), "matrix sizes do not match");
    
    // parse regrouping arguments
    if (spec_ != NULL) {
        char               *spec = strdup (spec_);
        char               *spec_full = spec;
        HREassert (spec != NULL, "No spec");
        struct group_info context;
        context.guards = guards;

#if defined(HAVE_BOOST) || defined(HAVE_VIENNACL)
        int total_graph = 0;
#endif

        matrix_t *selection = inf->mayw;

        dm_cost_t cost = DM_WEIGHTED_EVENT_SPAN;

        rt_timer_t trans_timer = NULL;
        if (group_time) trans_timer = RTcreateTimer();

        char               *tok;
        while ((tok = strsep (&spec, sep)) != NULL) {
            if (group_time) RTstartTimer(trans_timer);
            if (strcmp (tok, "sw") == 0) {
                Print1 (info, "Regroup Select Write matrix");
                selection = inf->mayw;
            } else if (strcmp (tok, "sr") == 0) {
                Print1 (info, "Regroup Select Read matrix");
                selection = inf->r;
            } else if (strcmp (tok, "sc") == 0) {
                Print1 (info, "Regroup Select Combined matrix");
                if (inf->combined == NULL) {
                    inf->combined = (matrix_t*) RTmalloc(sizeof(matrix_t));
                    dm_copy(inf->r, inf->combined);
                    dm_apply_or(inf->combined, inf->mayw);
                }
                selection = inf->combined;
            } else if (strcasecmp (tok, "ses") == 0) {
                Print1 (info, "Regroup Select Event Span");
                cost = DM_EVENT_SPAN;
            } else if (strcasecmp (tok, "swes") == 0) {
                Print1 (info, "Regroup Select Weighted Event Span");
                cost = DM_WEIGHTED_EVENT_SPAN;
            } else if (strcasecmp (tok, "w2W") == 0) {
                Print1 (info, "Regroup over-approximate must-write to may-write");
                dm_clear(inf->mustw);
                dm_clear(inf->old_mustw);
            } else if (strcasecmp (tok, "r2+") == 0) {
                Print1 (info, "Regroup over-approximate read to read + write");
                dm_apply_or(inf->mayw, inf->r);
                dm_apply_or(inf->old_mayw, inf->old_r);
            } else if (strcmp (tok, "W2+") == 0) {
                Print1 (info, "Regroup over-approximate may-write \\ must-write to read + write");
                matrix_t w;
                dm_copy(inf->mayw, &w);
                dm_apply_xor(&w, inf->mustw);
                dm_apply_or(inf->r, &w);
                dm_free(&w);

                matrix_t old_w;
                dm_copy(inf->old_mayw, &old_w);
                dm_apply_xor(&old_w, inf->old_mustw);
                dm_apply_or(inf->old_r, &old_w);
                dm_free(&old_w);
            } else if (strcmp (tok, "w2+") == 0) {
                Print1 (info, "Regroup over-approximate must-write to read + write");
                dm_apply_or(inf->r, inf->mustw);
                dm_apply_or(inf->old_r, inf->old_mustw);
            } else if (strcmp (tok, "-2r") == 0) {
                Print1 (info, "Regroup over-approximate copy to read");
                dm_fill(inf->r);
                dm_fill(inf->old_r);
            } else if (strcasecmp (tok, "rb4w") == 0) {
                Print1 (info, "Regroup Read BeFore Write");
                dm_sort_cols (inf->r, &read_before_write);
                inf_copy_col_headers(inf->r, inf);
                dm_sort_rows (inf->mayw, &write_before_read);
                inf_copy_row_headers(inf->mayw, inf);
            } else if (strcasecmp (tok, "cs") == 0) {
                Print1 (info, "Regroup Column Sort");
                dm_sort_cols (selection, &max_col_first);
                inf_copy_col_headers(selection, inf);
            } else if (strcasecmp (tok, "cn") == 0) {
                Print1 (info, "Regroup Column Nub");
                prepare_col_compare(inf, &context);
                dm_nub_cols (inf->r, &eq_cols, &context);
                RTfree(context.combined);
                inf_copy_col_headers(inf->r, inf);
            } else if (strcasecmp (tok, "sa") == 0) {
                Print1 (info, "Regroup Simulated Annealing");
                dm_anneal (selection, cost, mh_timeout);
                inf_copy_col_headers(selection, inf);
            } else if (strcasecmp (tok, "cw") == 0) {
                Print1 (info, "Regroup Column sWaps");
                if ((cw_max_cols < 0 || dm_ncols(selection) <= cw_max_cols) && (cw_max_rows < 0 || dm_nrows(selection) <= cw_max_rows)) {
                    dm_optimize (selection);
                    inf_copy_col_headers(selection, inf);
                } else {
                    Print1 (infoLong, "Selected matrix too large for \"cw\" (%d (>%d) columns, (%d (>%d) rows)",
                            dm_ncols(selection), cw_max_cols, dm_nrows(selection), cw_max_rows);
                }
            } else if (strcasecmp (tok, "ca") == 0) {
                Print1 (info, "Regroup Column All permutations");
                dm_all_perm(selection);
                inf_copy_col_headers(selection, inf);
            } else if (strcasecmp (tok, "rs") == 0) {
                Print1 (info, "Regroup Row Sort");
                dm_sort_rows (selection, &max_row_first);
                inf_copy_row_headers(selection, inf);
            } else if (strcasecmp (tok, "rn") == 0) {
                Print1 (info, "Regroup Row Nub");
                prepare_row_compare(inf, &context);
                dm_nub_rows (inf->r, &eq_rows, &context);
                RTfree(context.combined);
                inf_copy_row_headers(inf->r, inf);
            } else if (strcasecmp (tok, "ru") == 0) {
                Print1 (info, "Regroup Row sUbsume");
                prepare_row_compare(inf, &context);
                dm_subsume_rows (inf->r, &subsume_rows, &context);
                RTfree(context.combined);
                inf_copy_row_headers(inf->r, inf);
            } else if (strcasecmp (tok, "hf") == 0) {
                Print1 (info, "Reqroup Horizontal Flip");
                dm_horizontal_flip (inf->r);
                inf_copy_row_headers(inf->r, inf);
            } else if (strcasecmp (tok, "vf") == 0) {
                Print1 (info, "Reqroup Vertical Flip");
                dm_vertical_flip (inf->r);
                inf_copy_col_headers(inf->r, inf);
            }
#if defined(HAVE_BOOST) || defined(HAVE_VIENNACL)
            else if (strcasecmp (tok, "tg") == 0) {
                Print1 (info, "Regroup Total graph");
                total_graph = 1;
            } else if (strcasecmp (tok, "bg") == 0) {
                Print1 (info, "Regroup Bipartite Graph");
                total_graph = 0;
            }
#endif
#ifdef HAVE_BOOST
            else if (strcasecmp (tok, "bcm") == 0) {
                Print1 (info, "Regroup Boost's Cuthill McKee");
                int row_perm[dm_nrows(selection)];
                int col_perm[dm_ncols(selection)];
                boost_ordering(selection, row_perm, col_perm, BOOST_CM, total_graph, graph_metrics);
                apply_permutation(inf, row_perm, col_perm);
            } else if (strcasecmp (tok, "bs") == 0) {
                Print1 (info, "Regroup Boost's Sloan");
                int row_perm[dm_nrows(selection)];
                int col_perm[dm_ncols(selection)];
                boost_ordering(selection, row_perm, col_perm, BOOST_SLOAN, total_graph, graph_metrics);
                apply_permutation(inf, row_perm, col_perm);
            } else if (strcasecmp (tok, "bk") == 0) {
                Print1 (info, "Regroup Boost's King");
                int row_perm[dm_nrows(selection)];
                int col_perm[dm_ncols(selection)];
                boost_ordering(selection, row_perm, col_perm, BOOST_KING, total_graph, graph_metrics);
                apply_permutation(inf, row_perm, col_perm);
            } else if (strcasecmp (tok, "bn") == 0) {
                Print1 (info, "Regroup Boost's None");
                int row_perm[dm_nrows(selection)];
                int col_perm[dm_ncols(selection)];
                boost_ordering(selection, row_perm, col_perm, BOOST_NONE, total_graph, graph_metrics);
                apply_permutation(inf, row_perm, col_perm);
            }
#endif
#ifdef HAVE_VIENNACL
            else if (strcasecmp (tok, "vcm") == 0) {
                Print1 (info, "Regroup ViennaCL's Cuthill McKee");
                int row_perm[dm_nrows(selection)];
                int col_perm[dm_ncols(selection)];
                viennacl_reorder(selection, row_perm, col_perm, VIENNACL_CM, total_graph, graph_metrics);
                apply_permutation(inf, row_perm, col_perm);
            } else if (strcasecmp (tok, "vacm") == 0) {
                Print1 (info, "Regroup ViennaCL's Advanced Cuthill McKee");
                int row_perm[dm_nrows(selection)];
                int col_perm[dm_ncols(selection)];
                viennacl_reorder(selection, row_perm, col_perm, VIENNACL_ACM, total_graph, graph_metrics);
                apply_permutation(inf, row_perm, col_perm);
            } else if (strcasecmp (tok, "vgps") == 0) {
                Print1 (info, "Regroup ViennaCL's Gibbs Poole Stockmeyer");
                int row_perm[dm_nrows(selection)];
                int col_perm[dm_ncols(selection)];
                viennacl_reorder(selection, row_perm, col_perm, VIENNACL_GPS, total_graph, graph_metrics);
                apply_permutation(inf, row_perm, col_perm);
            } else if (strcasecmp (tok, "vn") == 0) {
                Print1 (info, "Regroup ViennaCL's None");
                int row_perm[dm_nrows(selection)];
                int col_perm[dm_ncols(selection)];
                viennacl_reorder(selection, row_perm, col_perm, VIENNACL_NONE, total_graph, graph_metrics);
                apply_permutation(inf, row_perm, col_perm);
            }
#endif
            else if (strcasecmp (tok, "mm") == 0) {
                Print1 (info, "Regroup Matrix Metrics");
                print_event_span(selection);
            } else if (strcasecmp (tok, "f") == 0) {
                Print1 (info, "Regroup FORCE");
                dm_FORCE(selection);
                inf_copy_col_headers(selection, inf);
            } else if (strcasecmp (tok, "gsa") == 0) {
                const char         *macro = "gc,gr,csa,rs";
                Print1 (info, "Regroup macro Simulated Annealing: %s", macro);
                apply_regroup_spec (inf, macro, guards, sep);
            } else if (strcasecmp (tok, "gs") == 0) {
                const char         *macro = "gc,gr,cw,rs";
                Print1 (info, "Regroup macro Group Safely: %s", macro);
                apply_regroup_spec (inf, macro, guards, sep);
            } else if (strcasecmp (tok, "ga") == 0) {
                const char         *macro = "ru,gc,rs,cw,rs";
                Print1 (info, "Regroup macro Group Aggressively: %s", macro);
                apply_regroup_spec (inf, macro, guards, sep);
            } else if (strcasecmp (tok, "gc") == 0) {
                const char         *macro = "cs,cn";
                Print1 (info, "Regroup macro Cols: %s", macro);
                apply_regroup_spec (inf, macro, guards, sep);
            } else if (strcasecmp (tok, "gr") == 0) {
                const char         *macro = "rs,rn";
                Print1 (info, "Regroup macro Rows: %s", macro);
                apply_regroup_spec (inf, macro, guards, sep);
            } else if (tok[0] != '\0') {
                Fatal (1, error, "Unknown regrouping specification: '%s'",
                       tok);
            }
            if (group_time) {
                RTstopTimer(trans_timer);
                RTprintTimer(infoShort, trans_timer, "Transformation took");
                RTresetTimer(trans_timer);
            }
        }
        RTfree(spec_full);
        RTdeleteTimer(trans_timer);
    }
}

void
combine_rows(matrix_t *matrix_new, matrix_t *matrix_old, int new_rows,
                 int *transbegin, int *transmap)
{
    bitvector_t row_new;

    for (int i = 0; i < new_rows; i++) {
        int begin = transbegin[i];
        int end   = transbegin[i + 1];

        bitvector_create(&row_new, dm_ncols(matrix_old));

        for (int j = begin; j < end; j++) {
            dm_row_union(&row_new, matrix_old, transmap[j]);
        }

        for (int k = 0; k < dm_ncols(matrix_old); k++) {
            if (bitvector_is_set(&row_new, k))
                dm_set(matrix_new, i, k);
            else
                dm_unset(matrix_new, i, k);
        }

        bitvector_free(&row_new);
    }
}

static int
compare_pair(const void *a, const void *b)
{
    const pair_t *pa = (const pair_t *) a;
    const pair_t *pb = (const pair_t *) b;

    return ((*pa).src > (*pb).src) - ((*pa).src < (*pb).src);
}

static pair_t* parse_pair_spec(int* num_pairs, const char *spec_, const int max) {
    pair_t* pairs = NULL;
    char *spec = strdup(spec_);
    char *pair_;
    while ((pair_ = strsep (&spec, ",")) != NULL) {
        char *pair = strdup(pair_);
        char *src = strsep(&pair, ".");
        if (src == NULL) Abort("Invalid pair spec %s", spec_);
        char *tgt = strsep(&pair, ".");
        free(pair);
        if (tgt == NULL) Abort("Invalid pair spec %s", spec_);
        if (pair != NULL)  Abort("Invalid pair spec %s", spec_);
        int s = atoi(src);
        int t = atoi(tgt);
        if (s < 0) s += max;
        if (t < 0) t += max;
        if (s < 0 || s >= max) Abort("Invalid source %d in pair %s", s, pair_);
        if (t < 0 || t >= max) Abort("Invalid target %d in pair %s", t, pair_);
        // if (s == t) Abort("Source and target can not be equal: %s", pair_);
        for (int i = 0; i < *num_pairs; i++) {
            if (pairs[i].src == s) Abort("Source %d in pair %s already given previously", s, pair_);
            // if (pairs[i].tgt == t) Abort("Target %d in pair %s already given previously", t, pair_);
            // if (pairs[i].src == t && pairs[i].tgt == s) Abort("Pair %s already given previously as %d.%d", pair_, pairs[i].src, pairs[i].tgt);
        }
        (*num_pairs)++;
        pairs = (pair_t*) RTrealloc(pairs, sizeof(pair_t[*num_pairs]));
        pairs[(*num_pairs)-1].src = s;
        pairs[(*num_pairs)-1].tgt = t;
    }
    free(spec);
    return pairs;
}

static void
split_matrices(rw_info_t* inf)
{
    if (inf->num_pairs > 0) {
        inf->r = (matrix_t*) RTmalloc(sizeof(matrix_t));
        inf->mayw = (matrix_t*) RTmalloc(sizeof(matrix_t));
        inf->mustw = (matrix_t*) RTmalloc(sizeof(matrix_t));
        dm_create(inf->r, dm_nrows(inf->old_r), dm_ncols(inf->old_r) - inf->num_pairs);
        dm_create(inf->mayw, dm_nrows(inf->old_mayw), dm_ncols(inf->old_mayw) - inf->num_pairs);
        dm_create(inf->mustw, dm_nrows(inf->old_mustw), dm_ncols(inf->old_mustw) - inf->num_pairs);

        // split the old matrices in two.
        for (int i = 0; i < dm_nrows(inf->old_r); i++) {
            int c = 0;
            for (int j = 0; j < dm_ncols(inf->old_r); j++) {
                if (c < inf->num_pairs && inf->sorted_pairs[c].src == j) c++;
                else {
                    if (dm_is_set(inf->old_r, i, j)) dm_set(inf->r, i, j - c);
                    if (dm_is_set(inf->old_mayw, i, j)) dm_set(inf->mayw, i, j - c);
                    if (dm_is_set(inf->old_mustw, i, j)) dm_set(inf->mustw, i, j - c);
                }
            }
        }
    } else {
        inf->r = inf->old_r;
        inf->mayw = inf->old_mayw;
        inf->mustw = inf->old_mustw;
        inf->old_r = (matrix_t*) RTmalloc(sizeof(matrix_t));
        inf->old_mayw = (matrix_t*) RTmalloc(sizeof(matrix_t));
        inf->old_mustw = (matrix_t*) RTmalloc(sizeof(matrix_t));
        dm_copy(inf->r, inf->old_r);
        dm_copy(inf->mayw, inf->old_mayw);
        dm_copy(inf->mustw, inf->old_mustw);
    }
}

static void
merge_matrices(rw_info_t* inf)
{
    if (inf->num_pairs > 0) {
        int offsets[dm_ncols(inf->r)];
        for (int i = 0, c = 0; i < dm_ncols(inf->old_r); i++) {
            if (c < inf->num_pairs && inf->sorted_pairs[c].src == i) c++;
            offsets[i - c] = i;
        }

        int col_perm[dm_ncols(inf->r)];
        for (int i = 0; i < dm_ncols(inf->r); i++) col_perm[i] = i;
        for (int i = 0; i < dm_ncols(inf->r); i++) {
            col_perm[offsets[i]] = offsets[inf->r->col_perm.data[i].becomes];
        }

        permutation_group_t* pg;
        int n;
        dm_create_permutation_groups(&pg, &n, col_perm, dm_ncols(inf->r));

        dm_free(inf->r);
        dm_free(inf->mayw);
        dm_free(inf->mustw);
        RTfree(inf->r);
        RTfree(inf->mayw);
        RTfree(inf->mustw);

        for (int i = 0; i < n; i++) {
            dm_permute_cols(inf->old_r, &(pg[i]));
            dm_permute_cols(inf->old_mayw, &(pg[i]));
            dm_permute_cols(inf->old_mustw, &(pg[i]));
        }

        for (int i = 0; i < inf->num_pairs; i++) {
            const int src = inf->old_r->col_perm.data[inf->pairs[i].src].at;
            const int tgt = inf->pairs[i].tgt;
            const int diff = abs(src - tgt);
            permutation_group_t pg;
            int rot[diff + 1];
            dm_create_permutation_group(&pg, diff + 1, rot);
            dm_add_to_permutation_group(&pg, tgt);
            for (int j = src; j != tgt; src < tgt ? j++ : j--) {
                dm_add_to_permutation_group(&pg, j);
            }
            dm_permute_cols(inf->old_r, &pg);
            dm_permute_cols(inf->old_mayw, &pg);
            dm_permute_cols(inf->old_mustw, &pg);
            dm_free_permutation_group(&pg);
        }
    } else {
        dm_free(inf->old_r);
        dm_free(inf->old_mayw);
        dm_free(inf->old_mustw);
        RTfree(inf->old_r);
        RTfree(inf->old_mayw);
        RTfree(inf->old_mustw);
        inf->old_r = inf->r;
        inf->old_mayw = inf->mayw;
        inf->old_mustw = inf->mustw;
    }
}

static void
str2vec(const int max_size, const char *perm, int *vec)
{
    char *p = strdup(perm);
    char *e;
    int i = 0;
    while ((e = strsep(&p, ",")) != NULL && strlen(e) > 0) {
        if (i == max_size) {
            Warning(error, "to many numbers (%d) given", i + 1);
            HREexit(LTSMIN_EXIT_FAILURE);
        }
        const int j = atoi(e);

        if (j < 0) {
            Warning(error, "%d at %d should be >= 0", j, i);
            HREexit(LTSMIN_EXIT_FAILURE);
        }

        if (j >= max_size) {
            Warning(error, "%d at %d should be < %d", j, i, max_size);
            HREexit(LTSMIN_EXIT_FAILURE);
        }

        for (int k = 0; k < i; k++) {
            if (j == vec[k]) {
                Warning(error, "%d at %d already given previously at %d", j, i, k);
                HREexit(LTSMIN_EXIT_FAILURE);
            }
        }

        vec[i] = j;

        i++;
    }
    if (i != max_size) {
        Warning(error, "to few numbers");
        HREexit(LTSMIN_EXIT_FAILURE);
    }
}

model_t
GBregroup (model_t model)
{
    if (!GBhasGuardsInfo(model)) {
        if (PINS_USE_GUARDS)
            Warning (info, "Ignoring option " USE_GUARDS_OPTION " for lack of guards!");
        PINS_USE_GUARDS = 0;
    }

    if (regroup_spec == NULL && col_ins == NULL &&
             row_perm == NULL && col_perm == NULL &&
             GBgetVarPerm(model) == NULL && GBgetGroupPerm(model) == NULL) {
        return model;
    }

    Print1(info, "Initializing regrouping layer");

    rt_timer_t t = RTcreateTimer();
    RTstartTimer(t);

    // note: context information is available via matrix, doesn't need to
    // be stored again
    matrix_t           *r       = RTmalloc (sizeof (matrix_t));
    matrix_t           *mayw    = RTmalloc (sizeof (matrix_t));
    matrix_t           *mustw   = RTmalloc (sizeof (matrix_t));
    matrix_t           *m       = GBgetDMInfo (model);

    matrix_t           *original_may_write = GBgetDMInfoMayWrite (model);
    matrix_t           *original_must_write = GBgetDMInfoMustWrite (model);

    dm_copy (original_may_write, mayw);
    dm_copy (original_must_write, mustw);

    if (PINS_USE_GUARDS) {
        const int gid = GBgetMatrixID (model, LTSMIN_MATRIX_ACTIONS_READS);
        dm_copy (GBgetMatrix(model, gid), r);
    } else {
        dm_copy (GBgetDMInfoRead(model), r);
    }

    rw_info_t inf;
    memset(&inf, 0, sizeof(rw_info_t));

    if (col_ins != NULL) {
        Print1(info, "Column insert: %s", col_ins);
        inf.pairs = parse_pair_spec(&(inf.num_pairs), col_ins, dm_ncols(r));
        if (inf.num_pairs > 0) {
            // sort by src; makes it easer to split and merge matrices later
            inf.sorted_pairs = (pair_t*) RTmalloc(sizeof(pair_t[inf.num_pairs]));
            memcpy(inf.sorted_pairs, inf.pairs, sizeof(pair_t[inf.num_pairs]));
            qsort(inf.sorted_pairs, inf.num_pairs, sizeof(pair_t), compare_pair);
        } else Abort("option --col-ins requires at least one pair");
    }
    inf.old_r = r;
    inf.old_mayw = mayw;
    inf.old_mustw = mustw;

    split_matrices(&inf);

    if (GBgetGroupPerm(model) != NULL) {
        Warning(info, "Got group permutation from language front-end; permuting rows");
        apply_permutation(&inf, GBgetGroupPerm(model), NULL);
    }

    if (GBgetVarPerm(model) != NULL) {
        Warning(info, "Got state vector permutation from language front-end; permuting columns");
        apply_permutation(&inf, NULL, GBgetVarPerm(model));
    }

    if (row_perm != NULL) {
        Warning(info, "Permuting rows according to given vector");
        int perm[dm_nrows(inf.r)];
        str2vec(dm_nrows(inf.r), row_perm, perm);
        apply_permutation(&inf, perm, NULL);
    }

    if (col_perm != NULL) {
        Warning(info, "Permuting columns according to given vector");
        int perm[dm_ncols(inf.r)];
        str2vec(dm_ncols(inf.r), col_perm, perm);
        apply_permutation(&inf, NULL, perm);
    }

    if (regroup_spec != NULL) {
        Print1 (info, "Regroup specification: %s", regroup_spec);
        if (PINS_USE_GUARDS) {
            apply_regroup_spec (&inf, regroup_spec, GBgetGuardsInfo(model), ",");
        } else {
            apply_regroup_spec (&inf, regroup_spec, NULL, ",");
        }
    }

    // post processing regroup specification
    if (inf.combined != NULL) {
        dm_free(inf.combined);
        RTfree(inf.combined);
    }

    // undo column grouping
    dm_ungroup_cols(inf.r);
    dm_ungroup_cols(inf.mayw);
    dm_ungroup_cols(inf.mustw);

    merge_matrices(&inf);

    if (col_ins != NULL) {
        RTfree(inf.pairs);
        RTfree(inf.sorted_pairs);
    }

    r = inf.old_r;
    mayw = inf.old_mayw;
    mustw = inf.old_mustw;
    // create new model
    model_t             group = GBcreateBase ();
    GBcopyChunkMaps (group, model);

    struct group_context *ctx = RTmalloc (sizeof *ctx);

    GBsetContext (group, ctx);

    GBsetNextStateLong (group, group_long);
    GBsetActionsLong(group, actions_long);
    GBsetNextStateAll (group, group_all);

    // fill statemapping: assumption this is a bijection
    {
        int                 Nparts = dm_ncols (r);
        if (Nparts != lts_type_get_state_length (GBgetLTStype (model)))
            Fatal (1, error,
                   "state mapping in file doesn't match the specification");
        ctx->len = Nparts;
        ctx->statemap = RTmalloc (Nparts * sizeof (int));
        for (int i = 0; i < Nparts; i++) {
            int                 s = r->col_perm.data[i].becomes;
            ctx->statemap[i] = s;
        }
    }

    // fill transition mapping: assumption: this is a surjection
    {
        int                 oldNgroups = dm_nrows (m);
        int                 newNgroups = dm_nrows (r);
        Print1  (info, "Regrouping: %d->%d groups", oldNgroups,
                 newNgroups);
        ctx->transbegin = RTmalloc ((1 + newNgroups) * sizeof (int));
        ctx->transmap = RTmalloc (oldNgroups * sizeof (int));
        // maps old group to new group
        ctx->groupmap = RTmalloc (oldNgroups * sizeof (int));

        // stores which states slots have to be copied
        ctx->group_cpy = RTmalloc (oldNgroups * sizeof(int*));

        for (int i = 0; i < oldNgroups; i++)
            ctx->group_cpy[i] = RTmalloc (dm_ncols(mayw) * sizeof(int));

        int                 p = 0;
        for (int i = 0; i < newNgroups; i++) {
            int                 first = r->row_perm.data[i].becomes;
            int                 all_in_group = first;
            ctx->transbegin[i] = p;
            int                 n = 0;
            do {

                // for each old transition group set for each slot whether the value
                // needs to be copied. The value needs to be copied if the new dependency
                // is a may-write (W) and the old dependency is a copy (-).
                for (int j = 0; j < dm_ncols(mayw); j++) {
                    if (    dm_is_set(mayw, i, j) &&
                            !dm_is_set(mustw, i, j) &&
                            !dm_is_set(original_may_write, all_in_group, ctx->statemap[j])) {
                        ctx->group_cpy[all_in_group][j] = 1;
                    } else {
                        ctx->group_cpy[all_in_group][j] = 0;
                    }
                }

                ctx->groupmap[all_in_group] = i;
                ctx->transmap[p + n] = all_in_group;
                n++;
                all_in_group = r->row_perm.data[all_in_group].group;
            } while (all_in_group != first);
            p = p + n;
        }
        ctx->transbegin[newNgroups] = p;

    }

    lts_type_t ltstype=GBgetLTStype (model);
    if (log_active(debug)){
        lts_type_printf(debug,ltstype);
    }
    Warning(debug,"permuting ltstype");
    ltstype=lts_type_permute(ltstype,ctx->statemap);
    if (log_active(debug)){
        lts_type_printf(debug,ltstype);
    }
    GBsetLTStype (group, ltstype);

    // set new dependency matrices
    GBsetDMInfoMayWrite (group, mayw);
    GBsetDMInfoMustWrite(group, mustw);

    // here we either transform the read matrix or the actions read matrix
    if (PINS_USE_GUARDS) { // we have transformed the actions read matrix

        // set the new actions read matrix
        GBsetMatrix(group, LTSMIN_MATRIX_ACTIONS_READS, r, PINS_MAY_SET,
                    PINS_INDEX_GROUP, PINS_INDEX_STATE_VECTOR);

        // transform the read matrix and set it
        matrix_t *read = RTmalloc (sizeof (matrix_t));
        dm_create(read, dm_nrows (r), dm_ncols (r));
        combine_rows(read, GBgetDMInfoRead (model), dm_nrows (r), ctx->transbegin,
                         ctx->transmap);
        dm_copy_col_info(r, read);
        GBsetDMInfoRead(group, read);
    } else { // we have transformed the read matrix

        // transform the actions read matrix and set it
        matrix_t *read = RTmalloc (sizeof (matrix_t));
        dm_create(read, dm_nrows (r), dm_ncols (r));
        combine_rows(read, GBgetMatrix(model, GBgetMatrixID(model, LTSMIN_MATRIX_ACTIONS_READS)), dm_nrows (r), ctx->transbegin,
                         ctx->transmap);
        dm_copy_col_info(r, read);
        GBsetMatrix(group, LTSMIN_MATRIX_ACTIONS_READS, read, PINS_MAY_SET,
                    PINS_INDEX_GROUP, PINS_INDEX_STATE_VECTOR);
        // set the new read matrix
        GBsetDMInfoRead(group, r);
    }


    // create a new combined dependency matrix
    matrix_t *new_dm = RTmalloc (sizeof (matrix_t));
    dm_copy (GBgetDMInfoRead(group), new_dm);
    dm_apply_or(new_dm, mayw);
    GBsetDMInfo (group, new_dm);

    // copy state label matrix and apply the same permutation
    matrix_t           *s = RTmalloc (sizeof (matrix_t));

    if (GBgetStateLabelInfo(model) != NULL) {
        dm_copy (GBgetStateLabelInfo (model), s);

        dm_copy_col_info(r, s);

        GBsetStateLabelInfo(group, s);
    }

    // set the guards per transition group
    if (PINS_USE_GUARDS) {
        guard_t** guards_info = RTmalloc(sizeof(guard_t*) * dm_nrows(r));
        for (int i = 0; i < dm_nrows(r); i++) {
            int oldGroup = r->row_perm.data[i].becomes;
            guard_t* guards = RTmalloc(sizeof(guard_t) + sizeof(int[GBgetGuard(model, oldGroup)->count]));
            guards->count = GBgetGuard(model, oldGroup)->count;
            for (int g = 0; g < guards->count; g++) {
                guards->guard[g] = GBgetGuard(model, oldGroup)->guard[g];
            }
            guards_info[i] = guards;
        }
        GBsetGuardsInfo(group, guards_info);
    }

    GBsetStateLabelShort (group, group_state_labels_short);
    GBsetStateLabelLong (group, group_state_labels_long);
    GBsetStateLabelsAll (group, group_state_labels_all);
    GBsetGroupsOfEdge (group, group_groups_of_edge);
    GBsetPrettyPrint (group, group_chunk_pretty_print);

    GBinitModelDefaults (&group, model);

    // permute initial state
    {
        int                 len = ctx->len;
        int                 s0[len], news0[len];
        GBgetInitialState (model, s0);
        for (int i = 0; i < len; i++)
            news0[i] = s0[ctx->statemap[i]];
        GBsetInitialState (group, news0);
    }

    RTstopTimer(t);
    RTprintTimer(infoShort, t, "Regrouping took");
    RTdeleteTimer(t);

    if (group_exit) {
        GBExit(model);
        HREabort(LTSMIN_EXIT_SUCCESS);
    }

    // who is responsible for freeing matrix_t dm_info in group?
    // probably needed until program termination
    return group;
}
