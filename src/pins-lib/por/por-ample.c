#include <hre/config.h>

#include <stdbool.h>

#include <dm/dm.h>
#include <hre/stringindex.h>
#include <ltsmin-lib/ltsmin-standard.h>
#include <pins-lib/pins-impl.h>
#include <pins-lib/pins-util.h>
#include <pins-lib/por/pins2pins-por.h>
#include <pins-lib/por/por-ample.h>
#include <pins-lib/por/por-internal.h>


/**
 * Ample-set search
 */

typedef uint8_t visited_index_t;

typedef struct scc_ctx_s {
    ample_t                *ample;
    uint64_t                index;
    uint64_t                scc_index;
    visited_index_t        *vset;
    ci_list                *stack;

    ci_list                *scc;
    ci_list                *tmp;
} scc_ctx_t;

typedef struct process_s {
    int                 id;
    char               *name;
    int                 pc_slot;
    ci_list            *groups;

    ci_list            *en;
    ci_list            *succs;
    bool                visible;
    size_t              conflicts;
} process_t;

struct ample_s {
    por_context        *por;
    size_t              num_procs;
    process_t          *procs;
    bool                all;

    ci_list           **dep; // do not accord process: group --> proc
    matrix_t            gnce;
    ci_list           **gmayen;
    int                *g2p;
    ci_list           **fdep; // Future dependencies
            /**
             * If process p has an enabled transition e in s, then it might be
             * non-deterministic with some other transition n of p which is
             * disable in s. Typically, e and n have the same program counter
             * but different guards. As n might be enabled by the outside
             * processes, it needs to be captured in the ample set.
             * We call this a future dependency.
             * We over-estimate the non-determinism using the
             * maybe-coenabled relation. Then we associate with e all
             * processes that have an action dependent on n.
             *
             * WARNING: the derived relation is stronger than this definition,
             * as we substract dependent processes
             * (dependencies are catured first in the algorithm)
             */

    int                 ample; // ample proc
    scc_ctx_t           scc;   // ample procs
};

extern void find_procs (ample_t* ample);

process_t *identify_procs (ample_t *ample, size_t *num_procs, int *group2proc);

static void
init_ample (ample_t* a, int *src)
{
    por_context        *por = a->por;
    HREassert (bms_count(por->exclude, 0) == 0, "Not implemented for ample sets.");

    por_init_transitions (por->parent, por, src);

    for (process_t* p = &a->procs[0]; p != &a->procs[a->num_procs]; p++) {
        ci_clear (p->en);
        p->visible = false;
        p->conflicts = 0;
    }

    ci_list            *en = por->enabled_list;
    for (int* g = ci_begin (en); g != ci_end (en); g++) {
        process_t *proc = &a->procs[a->g2p[*g]];
        ci_add (proc->en, *g);
        proc->conflicts += a->dep[*g]->count + a->fdep[*g]->count;
        proc->visible |= por->visible->set[*g];
    }
}

// empties list
static inline size_t
ample_emit (por_context *ctx, ci_list *list, int *src, prov_t *provctx)
{
    size_t emitted = 0;
    for (int group; ci_count(list) > 0 && ((group = 1 + ci_pop(list)));) { // 0
        emitted += GBgetTransitionsLong (ctx->parent, group - 1, src, hook_cb, provctx);
    }
    return emitted;
}

static inline size_t
handle_proviso (ample_t *a, prov_t *provctx, int *src)
{
    size_t              emitted = 0;
    // emit more if ignoring proviso is violated
    if ((PINS_LTL && provctx->por_proviso_false_cnt != 0) ||
        (!PINS_LTL && provctx->por_proviso_true_cnt == 0)) {
        provctx->force_proviso_true = 1;
        for (size_t j = 0; j < a->num_procs; j++) {
            emitted += ample_emit (a->por, a->procs[j].en, src, &*provctx);
        }
    }
    return emitted;
}

static ci_list *
init_scc_edges (scc_ctx_t *ctx, int i)
{
    ample_t* a = ctx->ample;
    process_t* pp = &a->procs[i];
    ci_clear (pp->succs);
    if (pp->visible || pp->en->count == 0) {
        for (size_t j = 0; j < a->num_procs; j++) {
            ci_add_if (pp->succs, j, j != i);
        }
    } else {
        for (int* t = ci_begin (pp->en); t != ci_end (pp->en); t++) {
            for (int* o = ci_begin (a->dep[*t]); o != ci_end (a->dep[*t]); o++) {
                ci_add (pp->succs, *o);
            }
            for (int* o = ci_begin (a->fdep[*t]); o != ci_end (a->fdep[*t]); o++) {
                ci_add (pp->succs, *o);
            }
        }
    }
    return pp->succs;
}

static bool
scc_check_r (scc_ctx_t *ctx, int i)
{
    HREassert (i < ctx->ample->num_procs);

    if (ctx->vset[i] != 0) return true;
    ctx->vset[i] = ctx->index++;
    HREassert (ctx->vset[i]);

    bool                root = true;
    ci_list            *succs = init_scc_edges (ctx, i);
    for (int *j = ci_begin(succs); j != ci_end(succs); *j++) {
        if (!scc_check_r(ctx, *j)) return false;
        root &= ctx->vset[i] <= ctx->vset[*j];
        ctx->vset[i] = min(ctx->vset[i], ctx->vset[*j]);
    }
    if (root) {
        ci_clear (ctx->tmp);
        ci_add (ctx->tmp, i);
        while (ci_count(ctx->stack) != 0 && ctx->vset[i] <= ctx->vset[ci_top(ctx->stack)]) {
            int                 j = ci_pop (ctx->stack);
            ctx->vset[j] = ctx->scc_index;
            ci_add (ctx->tmp, j);
        }
        ctx->index -= ctx->tmp->count;
        if (ctx->scc->count == 0 || ctx->tmp->count < ctx->scc->count)
            swap (ctx->scc, ctx->tmp);
        if (ctx->scc->count == 1) return false;
        ctx->scc_index--;
    } else {
        ci_add (ctx->stack, i);
    }
    return true;
}

static void
find_sccs (ample_t *a)
{
    a->scc.index = 1;
    a->scc.scc_index = a->num_procs;
    ci_clear (a->scc.scc);
    memset (a->scc.vset, 0, sizeof(visited_index_t[a->num_procs]));

    for (size_t i = 0; i < a->num_procs; i++) {
        ci_clear (a->scc.stack);
        if (!scc_check_r (&a->scc, i)) return;
    }
}

int
ample_search_all (model_t self, int *src, TransitionCB cb, void *uctx)
{
    por_context        *por = ((por_context *)GBgetContext(self));
    ample_t            *a = (ample_t *)por->alg;

    init_ample (a, src);

    find_sccs (a);

    prov_t              provctx = {cb, uctx, 0, 0, 0};
    int                 emitted = 0;
    for (int *i = ci_begin(a->scc.scc); i != ci_end(a->scc.scc); i++)
        emitted += ample_emit (por, a->procs[*i].en, src, &provctx);
    emitted += handle_proviso (a, &provctx, src);
    return emitted;
}

int
ample_search_one (model_t self, int *src, TransitionCB cb, void *uctx)
{
    por_context        *por = ((por_context *)GBgetContext(self));
    ample_t            *a = (ample_t *)por->alg;

    init_ample (a, src);

    size_t              emitted = 0;
    prov_t              provctx = {cb, uctx, 0, 0, 0};
    for (process_t *p = &a->procs[0]; p != &a->procs[a->num_procs]; p++) {

        //        non-empty   /  non-visible  /  non-conflicting
        if (p->en->count != 0 && !p->visible && p->conflicts == 0) {
            Debugf ("AMPLE proc %d is ample with %d enabled\n", p->id, p->en->count);
            a->ample = p->id;
            emitted += ample_emit (por, p->en, src, &provctx);
            emitted += handle_proviso (a, &provctx, src);
            return emitted;
        }
    }

    Debugf ("AMPLE emitting all\n");
    provctx.force_proviso_true = 1;
    return ample_emit (por, por->enabled_list, src, &provctx);
}

ample_t *
ample_create_context (por_context *por, bool all)
{
    if (NO_MC) Abort ("Ample sets require a may-be coenabled matrix.");
    model_t             model = por->parent;
    ample_t        *a = RTmalloc(sizeof(ample_t));
    a->por = por;
    a->all = all;

    // find processes:
    a->g2p = RTmallocZero (sizeof(int[por->ngroups]));
    a->procs = identify_procs (a, &a->num_procs, a->g2p);
    //find_procs (ample); // TODO: use dependency matrices for this?

    HREassert (!all || a->num_procs <= 255, "Only up to 255 processes are supported in the ample set.");
    a->scc.ample = a;
    a->scc.index = 1;
    a->scc.scc_index = a->num_procs - 1;
    a->scc.stack = ci_create (a->num_procs);
    a->scc.scc = ci_create (a->num_procs);
    a->scc.tmp = ci_create (a->num_procs);
    a->scc.vset = RTmallocZero(sizeof(visited_index_t[a->num_procs]));

    // Ample sets require more than just DNA:
    // Groups may also not enable ech other.
    matrix_t nes;
    dm_create(&nes, por->ngroups, por->ngroups);
    for (int i = 0; i < por->ngroups; i++) {
        for (int j = 0; j < por->ngroups; j++) {
            if (guard_of(por, i, &por->label_nes_matrix, j)) {
                dm_set( &nes, i, j);
            }
        }
    }

    matrix_t dep;
    dm_create (&dep, por->ngroups, a->num_procs);
    for (int g = 0; g < por->ngroups; g++) {
        int             i = a->g2p[g];
        for (size_t j = 0; j < a->num_procs; j++) {
            if (i == j) continue;
            process_t      *o = &a->procs[j];
            for (int *h = ci_begin(o->groups); h != ci_end(o->groups); h++) {
                if (  dm_is_set(&por->not_accords_with, g, *h) ||
                      dm_is_set(&nes, g, *h) || dm_is_set(&nes, *h, g)  ) {
                    dm_set (&dep, g, j);
                    break;
                }
            }
        }
    }
    a->dep = (ci_list **) dm_rows_to_idx_table (&dep);

    Printf1 (infoLong, "Process --> Conflict groups:\n");
    for (int i = 0; i < a->num_procs; i++) {
        Printf1 (infoLong, "%3d: ", i);
        for (int g = 0; g < por->ngroups; g++) {
            for (int *o = ci_begin(a->dep[g]); o != ci_end(a->dep[g]); o++) {
                if (*o == i) {
                    Printf1 (infoLong, "%3d,", g);
                }
            }
        }
        Printf1 (infoLong, "\n");
    }

    a->fdep = RTmalloc (sizeof(ci_list *[por->ngroups]));
    for (int g = 0; g < por->ngroups; g++) {
        int             i = a->g2p[g];
        a->fdep[g] = ci_create (a->num_procs);
        process_t      *p = &a->procs[i];
        for (size_t j = 0; j < a->num_procs; j++) {
            if (i == j || dm_is_set(&dep, g, j)) continue; // no need to duplicate transitions already dependent
            for (int *h = ci_begin(p->groups); h != ci_end(p->groups); h++) {
                if ( *h == g || dm_is_set(&por->nce, *h, g) ) continue;
                if ( dm_is_set(&dep, *h, j) ) {
                    ci_add (a->fdep[g], j);
                    break;
                }
            }
        }
    }

    Printf1 (infoLong, "g in P --> { O | exists (g,h) in NCE, h in P, f in O, (g,f) in DEP } \n");
    for (int i = 0; i < a->num_procs; i++) {
        Printf1 (infoLong, "%2d: ", i);
        process_t      *p = &a->procs[i];
        for (int *g = ci_begin(p->groups); g != ci_end(p->groups); g++) {
            Printf1 (infoLong, "%3d --> {", *g);
            for (int *j = ci_begin(a->fdep[*g]); j != ci_end(a->fdep[*g]); j++) {
                Printf1 (infoLong, "%2d,", *j);
            }
            Printf1 (infoLong, "},  ");
        }
        Printf1 (infoLong, "\n");
    }

    dm_free (&nes);
    dm_free (&dep);

    return a;
}

bool
ample_is_stubborn (por_context *por, int group)
{
    ample_t            *a = (ample_t *)por->alg;
    int                 i = a->g2p[group];
    return a->all ? ci_find (a->scc.scc, i) != -1 : i == a->ample;
}

int
pins_slot_with_type_name_is_pc (model_t model, int i)
{
    lts_type_t          ltstype = GBgetLTStype(model);
    char* name = lts_type_get_state_name (ltstype, i);
    char* type = lts_type_get_state_type (ltstype, i);

//#if defined(MAPA)
//        Abort("Undefined PC identification criteria for current frontend");
//#elif defined(MCRL)
//        Abort("Undefined PC identification criteria for current frontend");
//#elif defined(MCRL2)
//        Abort("Undefined PC identification criteria for current frontend");
//#elif defined(LTSMIN_PBES)
//        Abort("Undefined PC identification criteria for current frontend");
//#elif defined(ETF)
//        Abort("Undefined PC identification criteria for current frontend");
//#elif defined(DIVINE)
//        return strcmp(name, type) == 0;
//#elif defined(SPINS)
        return has_suffix(name, "._pc");
//#elif defined(OPAAL)
//        return strcmp(name, type) == 0;
//#else
//        Abort("Undefined PC identification criteria for current frontend");
//#endif
}

char *
pins_get_proc_name_from_pc (model_t model, int i)
{
//#if defined(MAPA)
//        Abort("Undefined PC identification criteria for current frontend");
//#elif defined(MCRL)
//        Abort("Undefined PC identification criteria for current frontend");
//#elif defined(MCRL2)
//        Abort("Undefined PC identification criteria for current frontend");
//#elif defined(LTSMIN_PBES)
//        Abort("Undefined PC identification criteria for current frontend");
//#elif defined(ETF)
//        Abort("Undefined PC identification criteria for current frontend");
//#elif defined(DIVINE)
//        return strcmp(name, type) == 0;
//#elif defined(SPINS)
    lts_type_t          ltstype = GBgetLTStype(model);
    char *name = lts_type_get_state_name (ltstype, i);

    char *dot = strchr (name, '.');
    HREassert (dot[0] == '.');
    dot[0] = '\0';
    char *res = strdup (name);
    dot[0] = '.';//#elif defined(OPAAL)
    return res;
//        return strcmp(name, type) == 0;
//#else
//        Abort("Undefined PC identification criteria for current frontend");
//#endif
}

static char *
prom_group_name (model_t model, int group)
{
    lts_type_t      ltstype = GBgetLTStype (model);
    int             label = lts_type_find_edge_label (ltstype, LTSMIN_EDGE_TYPE_STATEMENT);
    if (label) return NULL;
    int             type = lts_type_get_edge_label_typeno (ltstype, label);
    int             count = pins_chunk_count  (model, type);
    if (count < pins_get_group_count(model)) return NULL;
    chunk           c = pins_chunk_get (model, type, group);
    return c.data;
}


bool
pins_group_is_in_proc_with_name (model_t model, int group, char *name)
{
//#if defined(MAPA)
//        Abort("Undefined PC identification criteria for current frontend");
//#elif defined(MCRL)
//        Abort("Undefined PC identification criteria for current frontend");
//#elif defined(MCRL2)
//        Abort("Undefined PC identification criteria for current frontend");
//#elif defined(LTSMIN_PBES)
//        Abort("Undefined PC identification criteria for current frontend");
//#elif defined(ETF)
//        Abort("Undefined PC identification criteria for current frontend");
//#elif defined(DIVINE)
//        return strcmp(name, type) == 0;
//#elif defined(SPINS)
    char *gname = prom_group_name (model, group);
    return strncmp (name, strchr(gname, '(') + 1, strlen(name)) == 0;
//        return strcmp(name, type) == 0;
//#else
//        Abort("Undefined PC identification criteria for current frontend");
//#endif
}

process_t *
identify_procs (ample_t *ample, size_t *num_procs, int *group2proc)
{
    *num_procs = 0;
    por_context        *ctx = ample->por;
    model_t             model = ctx->parent;
    process_t          *procs = RTmalloc (sizeof(process_t[ctx->ngroups]));
    for (size_t i = 0; i < ctx->nslots; i++) {
        if (pins_slot_with_type_name_is_pc (model, i)) {
            procs[*num_procs].pc_slot = i;
            procs[*num_procs].name = pins_get_proc_name_from_pc (model, i);
            procs[*num_procs].id = *num_procs;
            procs[*num_procs].groups = ci_create (ample->por->ngroups);
            procs[*num_procs].en = ci_create (ample->por->ngroups);
            procs[*num_procs].succs = ci_create (ample->por->ngroups);
            (*num_procs)++;
        }
    }
    //matrix_t           *writes = GBgetDMInfoMayWrite(model);

    for (size_t j = 0; j < ctx->ngroups; j++) {
        bool found = false;
        for (size_t i = 0; i < *num_procs; i++) {
            if (pins_group_is_in_proc_with_name(model, j, procs[i].name)) {
                if (found) Warning (info, "Group %d doubly assigned to an ample-set process %s (chosing first encountered)", j, procs[i].name);
                ci_add (procs[i].groups, j);
                group2proc[j] = i;
                found = true;
            }
//            if (dm_is_set(writes, j, procs[i].pc_slot)) {
//                if (found) {
////                    Warning (info, "Group %d doubly assigned to an ample-set process (chosing first encountered)", j);
//                } else {
//                    Printf1 (infoLong, "%2d,", i);
//                    ci_add (procs[i].groups, j);
//                    group2proc[j] = i;
//                    found = true;
//                }
//            }
        }
        HREassert (found, "Group %d not assigned to an ample-set process", j);
    }

    Printf1 (infoLong, "Process --> Groups:\n");
    for (process_t *p = &procs[0]; p != &procs[*num_procs]; p++) {
        Printf1 (infoLong, "%3d: ", p->id);
        for (int *g = ci_begin(p->groups); g != ci_end(p->groups); g++) {
            Printf1 (infoLong, "%3d,", *g);
        }
        Printf1 (infoLong, "\n");
    }
    return procs;
}

// Search may-enabled relation over groups that cannot be mutually enabled.
static bool
add_trans (ample_t *ample, int t)
{
    if (ample->g2p[t] != -1)
        return false;
    ample->g2p[t] = ample->num_procs;
    ci_add (ample->procs[ample->num_procs].groups, t);

    for (int *tt = ci_begin(ample->gmayen[t]); tt != ci_end(ample->gmayen[t]); tt++) {
        if (!dm_is_set(&ample->gnce, t, *tt)) continue;

        bool added = add_trans (ample, *tt);
        if (!added && ample->g2p[*tt] != (int) ample->num_procs) {
            Warning (info, "Group %d already added to proc %d. Enabled by group %d.",
                     *tt, ample->g2p[*tt], t);
        }
    }
    return true;
}

void
find_procs (ample_t* ample)
{
    ample->procs[0].groups = ci_create (ample->por->ngroups);
    ample->procs[0].en = ci_create (ample->por->ngroups);
    for (int i = 0; i < ample->por->ngroups; i++)
        ample->g2p[i] = -1;

    model_t model = ample->por->parent;
    matrix_t* label_mce_matrix = GBgetGuardCoEnabledInfo (model);
    matrix_t* guard_group_not_coen = NULL;
    int id = GBgetMatrixID (model, LTSMIN_GUARD_GROUP_NOT_COEN);
    if (id != SI_INDEX_FAILED) {
        guard_group_not_coen = GBgetMatrix (model, id);
        HREassert(dm_nrows (guard_group_not_coen) >= ample->por->nguards
                        && dm_ncols (guard_group_not_coen) == ample->por->ngroups);
    }
    if (label_mce_matrix == NULL && guard_group_not_coen == NULL) {
        Abort("No maybe-coenabled matrix found. Ample sets not supported.");
    }
    HREassert(dm_nrows (label_mce_matrix) >= ample->por->nguards
                    && dm_ncols (label_mce_matrix) >= ample->por->nguards);
    // GROUP COEN
    dm_create (&ample->gnce, ample->por->ngroups, ample->por->ngroups);
    for (int g = 0; g < ample->por->nguards; g++) {
        if (guard_group_not_coen != NULL) {
            for (int tt = 0; tt < ample->por->ngroups; tt++) {
                if (!dm_is_set (guard_group_not_coen, g, tt)) continue;

                for (int t = 0; t < ample->por->guard2group[g]->count; t++) {
                    dm_set (&ample->gnce, ample->por->guard2group[g]->data[t], tt);
                }
            }
            continue;
        }
        for (int gg = 0; gg < ample->por->nguards; gg++) {
            if (dm_is_set (label_mce_matrix, g, gg)) continue;

            for (int t = 0; t < ample->por->guard2group[g]->count; t++) {
                for (int tt = 0; tt < ample->por->guard2group[gg]->count; tt++) {
                    dm_set (&ample->gnce, ample->por->guard2group[g]->data[t],
                            ample->por->guard2group[gg]->data[tt]);
                }
            }
        }
    }
    id = GBgetMatrixID (model, LTSMIN_MUST_ENABLE_MATRIX);
    ci_list** musten_list = NULL;
    if (id != SI_INDEX_FAILED) {
        matrix_t* musten = GBgetMatrix (model, id);
        HREassert(dm_nrows (musten) >= ample->por->nguards
                        && dm_ncols (musten) == ample->por->ngroups);
        musten_list = (ci_list**) dm_rows_to_idx_table (musten);
    }
    // GROUP MAY ENABLE
    matrix_t gmayen;
    ci_list** nes = musten_list != NULL ? musten_list : ample->por->label_nes;
    ci_list** g2g = ample->por->guard2group;
    dm_create (&gmayen, ample->por->ngroups, ample->por->ngroups);
    for (int g = 0; g < ample->por->nguards; g++) {
        for (int* t = ci_begin (nes[g]); t != ci_end (nes[g]); t++) {
            for (int* tt = ci_begin (g2g[g]); tt != ci_end (g2g[g]); tt++) {
                dm_set (&gmayen, *t, *tt);
            }
        }
    }
    ample->gmayen = (ci_list**) dm_rows_to_idx_table (&gmayen);
    for (int i = 0; i < ample->por->ngroups; i++) {
        bool added = add_trans (ample, i);
        if (added) {
            ample->num_procs++;
            process_t* proc = &ample->procs[ample->num_procs];
            proc->groups = ci_create (ample->por->ngroups);
            proc->en = ci_create (ample->por->ngroups);
        }
    }
    for (int i = 0; i < ample->por->ngroups; i++) {
        Printf(info, "%2d, ", i);
    }
    Printf(info, "\n");
    for (int i = 0; i < ample->por->ngroups; i++) {
        Printf(info, "%2d, ", ample->g2p[i]);
    }
    Printf(info, "\n");
}
