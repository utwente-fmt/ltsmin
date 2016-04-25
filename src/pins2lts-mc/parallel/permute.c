/*
 * permutor.c
 *
 *  Created on: Sep 20, 2013
 *      Author: laarman
 */

#include <hre/config.h>

#include <popt.h>
#include <stdlib.h>
#include <time.h>

#include <hre/unix.h>
#include <pins-lib/pins2pins-ltl.h>
#include <pins2lts-mc/algorithm/algorithm.h>
#include <pins2lts-mc/algorithm/reach.h>
#include <pins2lts-mc/parallel/global.h>
#include <pins2lts-mc/parallel/options.h>
#include <pins2lts-mc/parallel/permute.h>
#include <pins2lts-mc/parallel/state-store.h>
#include <pins2lts-mc/parallel/worker.h>
#include <util-lib/util.h>


static char            *arg_perm = "unknown";
permutation_perm_t      permutation = Perm_Unknown;

si_map_entry permutations[] = {
    {"shift",   Perm_Shift},
    {"shiftall",Perm_Shift_All},
    {"sort",    Perm_Sort},
    {"otf",     Perm_Otf},
    {"random",  Perm_Random},
    {"rr",      Perm_RR},
    {"sr",      Perm_SR},
    {"none",    Perm_None},
    {"dynamic", Perm_Dynamic},
    {"unknown", Perm_Unknown},
    {NULL, 0}
};

static void
perm_popt (poptContext con, enum poptCallbackReason reason,
               const struct poptOption *opt, const char *arg, void *data)
{
    int                 res;
    switch (reason) {
    case POPT_CALLBACK_REASON_PRE:
        break;
    case POPT_CALLBACK_REASON_POST:
        res = linear_search (permutations, arg_perm);
        if (res < 0)
            Abort ("unknown permutation method %s", arg_perm);
        permutation = res;
        return;
    case POPT_CALLBACK_REASON_OPTION:
        break;
    }
    Abort ("unexpected call to state_db_popt");
    (void)con; (void)opt; (void)arg; (void)data;
}

struct poptOption perm_options[] = {
    {NULL, 0, POPT_ARG_CALLBACK | POPT_CBFLAG_POST | POPT_CBFLAG_SKIPOPTION,
     (void *)perm_popt, 0, NULL, NULL},
    {"perm", 0, POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT,
     &arg_perm, 0, "Select the transition permutation method."
     "Dynamic implements the fresh successor heuristic, rr implements the best"
     "(static = cheaper) randomization technique, and shift implements the"
     "cheapest technique.",
     "<dynamic|random|rr|sort|sr|shift|otf|none>"},
    POPT_TABLEEND
};

typedef struct permute_todo_s {
    ref_t               ref;
    lattice_t           lattice;
    transition_info_t   ti;
    int                 seen;
} permute_todo_t;

struct permute_s {
    void               *call_ctx;   /* GB context */
    void               *run_ctx;
    int               **rand;       /* random permutations */
    int                *pad;        /* scratch pad for otf and dynamic permutation */
    perm_cb_f           real_cb;    /* GB callback */
    state_info_t       *state;      /* the source state */
    double              shift;      /* distance in group-based shift */
    uint32_t            shiftorder; /* shift projected to ref range*/
    int                 start_group;/* fixed index of group-based shift*/
    int                 start_group_index;  /* recorded index higher than start*/
    permute_todo_t     *todos;      /* records states that require late permutation */
    int                *tosort;     /* indices of todos */
    size_t              nstored;    /* number of states stored in to-do */
    permutation_perm_t  permutation;/* kind of permuation */
    model_t             model;      /* GB model */
    state_info_t       *next;       /* state info serializer */
    size_t              labels;     /* number of transition labels */
    alg_state_seen_f    state_seen;
    int                 por_proviso;

    ci_list           **inhibited_by;
    matrix_t           *inhibit_matrix;
    matrix_t           *class_matrix;
    int                 class_label;
};

static int
sort_cmp (const void *a, const void *b, void *arg)
{
    permute_t          *perm = (permute_t *) arg;
    const permute_todo_t     *A = &perm->todos[*((int*)a)];
    const permute_todo_t     *B = &perm->todos[*((int*)b)];
    return A->ref - B->ref + perm->shiftorder;
}

static const int            RR_ARRAY_SIZE = 16;

static int
rr_cmp (const void *a, const void *b, void *arg)
{
    permute_t          *perm = (permute_t *) arg;
    int                *rand = *perm->rand;
    ref_t               A = perm->todos[*((int*)a)].ref;
    ref_t               B = perm->todos[*((int*)b)].ref;
    return ((((1UL<<dbs_size)-1)&rand[A & ((1<<RR_ARRAY_SIZE)-1)])^A) -
           ((((1UL<<dbs_size)-1)&rand[B & ((1<<RR_ARRAY_SIZE)-1)])^B);
}

static int
rand_cmp (const void *a, const void *b, void *arg)
{
    permute_t          *perm = (permute_t *) arg;
    int                *rand = *perm->rand;
    const permute_todo_t     *A = &perm->todos[*((int*)a)];
    const permute_todo_t     *B = &perm->todos[*((int*)b)];
    return rand[A->ti.group] - rand[B->ti.group];
}

static int
dyn_cmp (const void *a, const void *b, void *arg)
{
    permute_t          *perm = (permute_t *) arg;
    int                *rand = *perm->rand;
    permute_todo_t     *A = &perm->todos[*((int*)a)];
    permute_todo_t     *B = &perm->todos[*((int*)b)];

    if (A->seen == B->seen) // if dynamically no difference, then randomize:
        return rand[A->ti.group] - rand[B->ti.group];
    return A->seen - B->seen;
}

static inline void
perm_todo (permute_t *perm, transition_info_t *ti, int seen)
{
    HREassert (perm->nstored < K+TODO_MAX);
    permute_todo_t     *todo = perm->todos + perm->nstored;
    perm->tosort[perm->nstored] = perm->nstored;
    todo->ref = perm->next->ref;
    todo->seen = seen;
    todo->seen = perm->state_seen (perm->call_ctx, ti, todo->ref, seen);
    todo->lattice = perm->next->lattice;
    todo->ti.group = ti->group;
    todo->ti.por_proviso = ti->por_proviso;
    if (EXPECT_FALSE(act_detect || files[1] || (PINS_BUCHI_TYPE == PINS_BUCHI_TYPE_TGBA)))
        memcpy (todo->ti.labels, ti->labels, sizeof(int*[perm->labels]));
    perm->nstored++;
    ti->por_proviso = perm->por_proviso;
}

static inline void
perm_do (permute_t *perm, int i)
{
    permute_todo_t *todo = perm->todos + i;
    state_info_set (perm->next, todo->ref, todo->lattice);
    perm->real_cb (perm->call_ctx, perm->next, &todo->ti, todo->seen);
}

static inline void
perm_do_all (permute_t *perm)
{
    for (size_t i = 0; i < perm->nstored; i++) {
        perm_do (perm, perm->tosort[i]);
    }
}

static void
permute_one (void *arg, transition_info_t *ti, state_data_t dst, int *cpy)
{

    (void) cpy;

    permute_t          *perm = (permute_t*) arg;
    int                 seen;
    seen = state_info_new_state (perm->next, dst, ti, perm->state);
    if (EXPECT_FALSE(seen < 0)) {
        if (run_stop(perm->run_ctx)) {
            Warning (info, "Error: %s full! Change -s/--ratio.",
                           state_store_full_msg(seen));
        }
        return;
    }
    switch (perm->permutation) {
    case Perm_Shift:
        if (ti->group < perm->start_group) {
            perm_todo (perm, ti, seen);
            break;
        }
    case Perm_None:
        perm->real_cb (perm->call_ctx, perm->next, ti, seen);
        ti->por_proviso &= perm->por_proviso;
        break;
    case Perm_Shift_All:
        if (0 == perm->start_group_index && ti->group >= perm->start_group)
            perm->start_group_index = perm->nstored;
    case Perm_Dynamic:
    case Perm_Random:
    case Perm_SR:
    case Perm_RR:
    case Perm_Otf:
    case Perm_Sort:
        perm_todo (perm, ti, seen);
        break;
    default:
        Abort ("Unknown permutation!");
    }
}

int
permute_next (permute_t *perm, state_info_t *state, int group, perm_cb_f cb, void *ctx)
{
    perm->permutation   = Perm_None;
    perm->call_ctx      = ctx;
    perm->real_cb       = cb;
    perm->state         = state;
    perm->nstored       = perm->start_group_index = 0;
    int                 count;
    state_data_t        data = state_info_pins_state (state);
    count = GBgetTransitionsLong (((wctx_t *)ctx)->model, group, data, permute_one, perm);
    return count;
}

static inline bool
is_inhibited (permute_t* perm, int *class_count, int i)
{
    for (int c = 0; c < perm->inhibited_by[i]->count; c++) {
        int j = perm->inhibited_by[i]->data[c];
        if (j >= i) return false;
        if (class_count[j] > 0) return true;
    }
    return false;
}

int
permute_trans (permute_t *perm, state_info_t *state, perm_cb_f cb, void *ctx)
{
    perm->call_ctx = ctx;
    perm->real_cb = cb;
    perm->state = state;
    perm->nstored = perm->start_group_index = 0;
    int                 count = 0;
    state_data_t        data = state_info_pins_state (state);

    if (inhibit) {
        int N = dm_nrows (perm->inhibit_matrix);
        int class_count[N];
        for (int i = 0; i < N; i++) {
            class_count[i] = 0;
            if (is_inhibited(perm, class_count, i)) continue;
            if (perm->class_label >= 0) {
                class_count[i] = GBgetTransitionsMatching (perm->model, perm->class_label, i, data, permute_one, perm);
            } else if (perm->class_matrix != NULL) {
                class_count[i] = GBgetTransitionsMarked (perm->model, perm->class_matrix, i, data, permute_one, perm);
            } else {
                Abort ("inhibit set, but no known classification found.");
            }
            count += class_count[i];
        }
    } else {
        count = GBgetTransitionsAll (perm->model, data, permute_one, perm);
    }

    switch (perm->permutation) {
    case Perm_Otf:
        randperm (perm->pad, perm->nstored, state->ref + perm->shiftorder);
        for (size_t i = 0; i < perm->nstored; i++)
            perm_do (perm, perm->pad[i]);
        break;
    case Perm_Random:
        for (size_t i = 0; i < perm->nstored; i++)
            perm_do (perm, perm->rand[perm->nstored][i]);
        break;
    case Perm_Dynamic:
        qsortr (perm->tosort, perm->nstored, sizeof(int), dyn_cmp, perm);
        perm_do_all (perm);
        break;
    case Perm_RR:
        qsortr (perm->tosort, perm->nstored, sizeof(int), rr_cmp, perm);
        perm_do_all (perm);
        break;
    case Perm_SR:
        qsortr (perm->tosort, perm->nstored, sizeof(int), rand_cmp, perm);
        perm_do_all (perm);
        break;
    case Perm_Sort:
        qsortr (perm->tosort, perm->nstored, sizeof(int), sort_cmp, perm);
        perm_do_all (perm);
        break;
    case Perm_Shift:
        perm_do_all (perm);
        break;
    case Perm_Shift_All:
        for (size_t i = 0; i < perm->nstored; i++) {
            size_t j = (perm->start_group_index + i);
            j = j < perm->nstored ? j : 0;
            perm_do (perm, j);
        }
        break;
    case Perm_None:
        break;
    default:
        Abort ("Unknown permutation!");
    }
    return count;
}

void
permute_set_model (permute_t *perm, model_t model)
{
    perm->model = model;
}

void
permute_set_por (permute_t *perm, int por)
{
    perm->por_proviso = por;
}

permute_t *
permute_create (permutation_perm_t permutation, model_t model, alg_state_seen_f ssf,
                int worker_index, void *run_ctx)
{
    permute_t          *perm = RTalign (CACHE_LINE_SIZE, sizeof(permute_t));
    perm->todos = RTalign (CACHE_LINE_SIZE, sizeof(permute_todo_t[K+TODO_MAX]));
    perm->tosort = RTalign (CACHE_LINE_SIZE, sizeof(int[K+TODO_MAX]));
    perm->shift = ((double)K)/W;
    perm->shiftorder = (1UL<<dbs_size) / W * worker_index;
    perm->start_group = perm->shift * worker_index;
    perm->model = model;
    perm->state_seen = ssf;
    perm->por_proviso = 1;
    perm->permutation = permutation;
    perm->run_ctx = run_ctx;
    perm->next = state_info_create ();
    if (Perm_Otf == perm->permutation)
        perm->pad = RTalign (CACHE_LINE_SIZE, sizeof(int[K+TODO_MAX]));
    if (Perm_Random == perm->permutation) {
        perm->rand = RTalignZero (CACHE_LINE_SIZE, sizeof(int*[K+TODO_MAX]));
        for (size_t i = 1; i < K+TODO_MAX; i++) {
            perm->rand[i] = RTalign (CACHE_LINE_SIZE, sizeof(int[ i ]));
            randperm (perm->rand[i], i, perm->shiftorder);
        }
    }
    if (Perm_RR == perm->permutation) {
        perm->rand = RTalignZero (CACHE_LINE_SIZE, sizeof(int*));
        perm->rand[0] = RTalign (CACHE_LINE_SIZE, sizeof(int[1<<RR_ARRAY_SIZE]));
        srandom (time(NULL) + 9876432*worker_index);
        for (int i =0; i < (1<<RR_ARRAY_SIZE); i++)
            perm->rand[0][i] = random();
    }
    if (Perm_SR == perm->permutation || Perm_Dynamic == perm->permutation) {
        perm->rand = RTalignZero (CACHE_LINE_SIZE, sizeof(int*));
        perm->rand[0] = RTalign (CACHE_LINE_SIZE, sizeof(int[K+TODO_MAX]));
        randperm (perm->rand[0], K+TODO_MAX, (time(NULL) + 9876*worker_index));
    }
    perm->labels = lts_type_get_edge_label_count (GBgetLTStype(model));
    for (size_t i = 0; i < K+TODO_MAX; i++) {
        if (act_detect || files[1] || (PINS_BUCHI_TYPE == PINS_BUCHI_TYPE_TGBA)) {
            perm->todos[i].ti.labels = RTmalloc (sizeof(int*[perm->labels]));
        } else {
            perm->todos[i].ti.labels = NULL;
        }
    }

    perm->class_label = lts_type_find_edge_label (GBgetLTStype(model),LTSMIN_EDGE_TYPE_ACTION_CLASS);
    if (inhibit){
        int id=GBgetMatrixID(model,"inhibit");
        if (id>=0){
            perm->inhibit_matrix = GBgetMatrix (model, id);
            Warning(infoLong,"inhibit matrix is:");
            if (log_active(infoLong)) dm_print (stderr, perm->inhibit_matrix);
            perm->inhibited_by = (ci_list **)dm_cols_to_idx_table (perm->inhibit_matrix);
        } else {
            Warning(infoLong,"no inhibit matrix");
        }
        id = GBgetMatrixID(model,LTSMIN_EDGE_TYPE_ACTION_CLASS);
        if (id>=0){
            perm->class_matrix=GBgetMatrix(model,id);
            Warning(infoLong,"inhibit class matrix is:");
            if (log_active(infoLong)) dm_print(stderr,perm->class_matrix);
        } else {
            Warning(infoLong,"no inhibit class matrix");
        }
        if (perm->class_label>=0) {
            Warning(infoLong,"inhibit class label is %d",perm->class_label);
        } else {
            Warning(infoLong,"no inhibit class label");
        }
    }

    return perm;
}

state_info_t *
permute_state_info (permute_t *perm)
{
    return perm->next;
}

void
permute_free (permute_t *perm)
{
    RTfree (perm->tosort);
    if (Perm_Otf == perm->permutation)
        RTfree (perm->pad);
    if (Perm_Random == perm->permutation) {
        for (size_t i = 0; i < K+TODO_MAX; i++)
            RTfree (perm->rand[i]);
        RTfree (perm->rand);
    }
    if ( Perm_RR == perm->permutation || Perm_SR == perm->permutation ||
         Perm_Dynamic == perm->permutation ) {
        RTfree (perm->rand[0]);
        RTfree (perm->rand);
    }
    if (act_detect || files[1] || (PINS_BUCHI_TYPE == PINS_BUCHI_TYPE_TGBA)) {
        for (size_t i = 0; i < K+TODO_MAX; i++) {
            RTfree (perm->todos[i].ti.labels);
        }
    }
    RTfree (perm->todos);
    RTfree (perm);
}
