#include <hre/config.h>

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include <dm/dm.h>
#include <hre/unix.h>
#include <hre/user.h>
#include <pins-lib/pins.h>
#include <ltsmin-lib/lts-type.h>
#include <util-lib/dynamic-array.h>

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
group_long (model_t self, int group, int *newsrc, TransitionCB cb,
            void *user_context)
{
    group_context_t     ctx = (group_context_t)GBgetContext (self);
    model_t             parent = GBgetParent (self);
    int                 len = ctx->len;
    int                 oldsrc[len];
    int                 Ntrans = 0;
    int                 begin = ctx->transbegin[group];
    int                 end = ctx->transbegin[group + 1];
    ctx->cb = cb;
    ctx->user_context = user_context;

    for (int i = 0; i < len; i++)
        oldsrc[ctx->statemap[i]] = newsrc[i];


    for (int j = begin; j < end; j++) {
        int                 g = ctx->transmap[j];
        ctx->cpy = ctx->group_cpy[g];
        Ntrans += GBgetTransitionsLong (parent, g, oldsrc, group_cb, ctx);
    }

    return Ntrans;
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

static int
group_transition_in_group (model_t self, int* labels, int group)
{
    group_context_t  ctx    = (group_context_t)GBgetContext (self);
    model_t          parent = GBgetParent (self);
    int              begin  = ctx->transbegin[group];
    int              end    = ctx->transbegin[group + 1];

    for (int i = begin; i < end; i++) {
        int g = ctx->transmap[i];
        if (GBtransitionInGroup(parent, labels, g))
            return 1;
    }

    return 0;
}

int
max_row_first (matrix_t *r, matrix_t *w, int rowa, int rowb)
{
    HREassert(
                dm_ncols(r) == dm_ncols(w) &&
                dm_nrows(r) == dm_nrows(w), "matrix sizes do not match");
    
    int                 i,
                        raw,
                        rbw;

    for (i = 0; i < dm_ncols (r); i++) {
        raw = dm_is_set (w, rowa, i);
        rbw = dm_is_set (w, rowb, i);

        if (((raw) && (rbw)) || (!raw && !rbw))
            continue;
        return ((rbw) - (raw));
    }

    return 0;
}

int
max_col_first (matrix_t *r, matrix_t *w, int cola, int colb)
{
    HREassert(dm_ncols(r) == dm_ncols(w) && dm_nrows(r) == dm_nrows(w), "matrix sizes do not match");

    int                 i,
                        car,
                        cbr;

    for (i = 0; i < dm_nrows (r); i++) {
        car = dm_is_set (r, i, cola);
        cbr = dm_is_set (r, i, colb);

        if ((car && cbr) || (!car && !cbr))
            continue;
        return (cbr - car);
    }

    return 0;
}

int
write_before_read (matrix_t *r, matrix_t *w, int rowa, int rowb)
{
    HREassert(
                dm_ncols(r) == dm_ncols(w) &&
                dm_nrows(r) == dm_nrows(w), "matrix sizes do not match");

    int i;

    int ra = 0;
    int rb = 0;

    for (i = 0; i < dm_ncols (r); i++) {
        if (dm_is_set (w, rowa, i)) ra += (i);
        if (dm_is_set (w, rowb, i)) rb += (i);
    }

    return rb - ra;
}

int
read_before_write (matrix_t *r, matrix_t *w, int cola, int colb)
{
    HREassert(
                dm_ncols(r) == dm_ncols(w) &&
                dm_nrows(r) == dm_nrows(w), "matrix sizes do not match");
    
    int i;

    int ca = 0;
    int cb = 0;

    for (i = 0; i < dm_nrows (r); i++) {
        if (dm_is_set (r, i, cola)) ca += (i);
        if (dm_is_set (r, i, colb)) cb += (i);
    }

    return cb - ca;
}

static void
apply_regroup_spec (matrix_t* r, matrix_t* mayw, matrix_t* mustw, const char *spec_)
{
    
    HREassert(
            dm_ncols(r) == dm_ncols(mayw) &&
            dm_nrows(r) == dm_nrows(mayw) &&
            dm_ncols(r) == dm_ncols(mustw) &&
            dm_nrows(r) == dm_nrows(mustw), "matrix sizes do not match");
    
    // parse regrouping arguments
    if (spec_ != NULL) {
        char               *spec = strdup (spec_);
        char               *spec_full = spec;
        HREassert (spec != NULL, "No spec");
        
        char               *tok;
        while ((tok = strsep (&spec, ",")) != NULL) {
            if (strcasecmp (tok, "w2W") == 0) {
                Print1 (info, "Regroup over-approximate must-write to may-write");
                dm_clear(mustw);
            } else if (strcasecmp (tok, "r2+") == 0) {
                Print1 (info, "Regroup over-approximate read to read + write");
                dm_apply_or(mayw, r);
            } else if (strcmp (tok, "W2+") == 0) {
                Print1 (info, "Regroup over-approximate may-write \\ must-write to read + write");
                matrix_t *w = RTmalloc(sizeof(matrix_t));
                dm_copy(mayw, w);
                dm_apply_xor(w, mustw);
                dm_apply_or(r, w);
                dm_free(w);
            } else if (strcmp (tok, "w2+") == 0) {
                Print1 (info, "Regroup over-approximate must-write to read + write");
                dm_apply_or(r, mustw);
            } else if (strcasecmp (tok, "rb4w") == 0) {
                Print1 (info, "Regroup Read BeFore Write");
                dm_sort_cols (r, mayw, mustw, &read_before_write);
                dm_sort_rows (r, mayw, mustw, &write_before_read);
            } else if (strcasecmp (tok, "cs") == 0) {
                Print1 (info, "Regroup Column Sort");
                dm_sort_cols (r, mayw, mustw, &max_col_first);
            } else if (strcasecmp (tok, "cn") == 0) {
                Print1 (info, "Regroup Column Nub");
                dm_nub_cols (r, mayw, mustw);
            } else if (strcasecmp (tok, "csa") == 0) {
                Print1 (info, "Regroup Column swap with Simulated Annealing");
                dm_anneal (r, mayw, mustw);
            } else if (strcasecmp (tok, "cw") == 0) {
                Print1 (info, "Regroup Column sWaps");
                dm_optimize (r, mayw, mustw);
            } else if (strcasecmp (tok, "ca") == 0) {
                Print1 (info, "Regroup Column All permutations");
                dm_all_perm (r, mayw, mustw);
            } else if (strcasecmp (tok, "rs") == 0) {
                Print1 (info, "Regroup Row Sort");
                dm_sort_rows (r, mayw, mustw, &max_row_first);
            } else if (strcasecmp (tok, "rn") == 0) {
                Print1 (info, "Regroup Row Nub");
                dm_nub_rows (r, mayw, mustw);
            } else if (strcasecmp (tok, "ru") == 0) {
                Print1 (info, "Regroup Row sUbsume");
                dm_subsume_rows (r, mayw, mustw);
            } else if (strcasecmp (tok, "gsa") == 0) {
                const char         *macro = "gc,gr,csa,rs";
                Print1 (info, "Regroup macro Simulated Annealing: %s", macro);
                apply_regroup_spec (r, mayw, mustw, macro);
            } else if (strcasecmp (tok, "gs") == 0) {
                const char         *macro = "gc,gr,cw,rs";
                Print1 (info, "Regroup macro Group Safely: %s", macro);
                apply_regroup_spec (r, mayw, mustw, macro);
            } else if (strcasecmp (tok, "ga") == 0) {
                const char         *macro = "ru,gc,rs,cw,rs";
                Print1 (info, "Regroup macro Group Aggressively: %s", macro);
                apply_regroup_spec (r, mayw, mustw, macro);
            } else if (strcasecmp (tok, "gc") == 0) {
                const char         *macro = "cs,cn";
                Print1 (info, "Regroup macro Cols: %s", macro);
                apply_regroup_spec (r, mayw, mustw, macro);
            } else if (strcasecmp (tok, "gr") == 0) {
                const char         *macro = "rs,rn";
                Print1 (info, "Regroup macro Rows: %s", macro);
                apply_regroup_spec (r, mayw, mustw, macro);
            } else if (tok[0] != '\0') {
                Fatal (1, error, "Unknown regrouping specification: '%s'",
                       tok);
            }
        }
        RTfree(spec_full);
    }
}

void
combine_rows(matrix_t *matrix_new, matrix_t *matrix_old, int new_rows,
                 int *transbegin, int *transmap)
{
    bitvector_t row_new, row_old;

    bitvector_create(&row_old, dm_ncols(matrix_old));

    for (int i = 0; i < new_rows; i++) {
        int begin = transbegin[i];
        int end   = transbegin[i + 1];

        bitvector_create(&row_new, dm_ncols(matrix_old));

        for (int j = begin; j < end; j++) {
            dm_bitvector_row(&row_old, matrix_old, transmap[j]);
            bitvector_union(&row_new, &row_old);
        }

        for (int k = 0; k < dm_ncols(matrix_old); k++) {
            if (bitvector_is_set(&row_new, k))
                dm_set(matrix_new, i, k);
            else
                dm_unset(matrix_new, i, k);
        }

        bitvector_free(&row_new);
    }

    bitvector_free(&row_old);
}

model_t
GBregroup (model_t model, const char *regroup_spec)
{
    // note: context information is available via matrix, doesn't need to
    // be stored again
    matrix_t           *r       = RTmalloc (sizeof (matrix_t));
    matrix_t           *mayw    = RTmalloc (sizeof (matrix_t));
    matrix_t           *mustw   = RTmalloc (sizeof (matrix_t));
    matrix_t           *m       = GBgetDMInfo (model);

    matrix_t           *original_may_write = GBgetDMInfoMayWrite (model);
    matrix_t           *original_must_write = GBgetDMInfoMustWrite (model);

    dm_copy (GBgetDMInfoRead (model), r);
    dm_copy (original_may_write, mayw);
    dm_copy (original_must_write, mustw);

    Print1 (info, "Regroup specification: %s", regroup_spec);
    apply_regroup_spec (r, mayw, mustw, regroup_spec);
    // post processing regroup specification
    // undo column grouping
    dm_ungroup_cols(r);
    dm_ungroup_cols(mayw);
    dm_ungroup_cols(mustw);

    // create new model
    model_t             group = GBcreateBase ();
    GBcopyChunkMaps (group, model);

    struct group_context *ctx = RTmalloc (sizeof *ctx);

    GBsetContext (group, ctx);

    GBsetNextStateLong (group, group_long);
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
    
    matrix_t           *new_dm = RTmalloc (sizeof (matrix_t));
    dm_copy(r, new_dm);
    dm_apply_or(new_dm, mayw);

    // set new dependency matrices
    GBsetDMInfo (group, new_dm);
    GBsetDMInfoRead(group, r);
    GBsetDMInfoMayWrite (group, mayw);
    GBsetDMInfoMustWrite(group, mustw);

    // copy state label matrix and apply the same permutation
    matrix_t           *s = RTmalloc (sizeof (matrix_t));

    dm_copy (GBgetStateLabelInfo (model), s);
    HREassert (dm_ncols(m) == dm_ncols(s), "Error in DM copy");

    // probably better to write some functions to do this,
    // i.e. dm_get_column_permutation
    dm_copy_header(&(r->col_perm), &(s->col_perm));

    GBsetStateLabelInfo(group, s);

    GBsetStateLabelShort (group, group_state_labels_short);
    GBsetStateLabelLong (group, group_state_labels_long);
    GBsetStateLabelsAll (group, group_state_labels_all);
    GBsetTransitionInGroup (group, group_transition_in_group);

    GBinitModelDefaults (&group, model);

    // apparently we have over-approximated w to W.
    // so now we support copy.
    if (dm_is_empty(mustw) && original_must_write == original_may_write)
        GBsetSupportsCopy(group);

    // permute initial state
    {
        int                 len = ctx->len;
        int                 s0[len], news0[len];
        GBgetInitialState (model, s0);
        for (int i = 0; i < len; i++)
            news0[i] = s0[ctx->statemap[i]];
        GBsetInitialState (group, news0);
    }

    // who is responsible for freeing matrix_t dm_info in group?
    // probably needed until program termination
    return group;
}
