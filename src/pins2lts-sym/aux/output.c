#include <hre/config.h>

#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif

#include <float.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <dm/dm.h>
#include <hre/stringindex.h>
#include <hre/user.h>
#include <lts-io/user.h>
#include <ltsmin-lib/ltsmin-syntax.h>
#include <pins-lib/pins.h>
#include <pins-lib/pins-impl.h>
#include <pins-lib/pins-util.h>
#include <pins-lib/pins2pins-group.h>
#include <pins-lib/property-semantics.h>
#include <pins2lts-sym/alg/aux.h>
#include <pins2lts-sym/aux/options.h>
#include <pins2lts-sym/aux/output.h>
#include <ltsmin-lib/ltsmin-standard.h>
#include <ltsmin-lib/ltsmin-syntax.h>
#include <ltsmin-lib/ltsmin-tl.h>
#include <mc-lib/atomics.h>
#include <mc-lib/bitvector-ll.h>
#include <util-lib/bitset.h>
#include <util-lib/dfs-stack.h>
#include <util-lib/util.h>



typedef struct {
    model_t  model;
    FILE    *tbl_file;
    int      tbl_count;
    int      group;
    int     *src;
} output_context;

static void
etf_edge(void *context, transition_info_t *ti, int *dst, int *cpy)
{
    output_context* ctx = (output_context*)context;

    ctx->tbl_count++;

    for(int i = 0, k = 0 ; i < N; i++) {
        if (dm_is_set(GBgetDMInfo(ctx->model), ctx->group, i)) {
            fprintf(ctx->tbl_file, " %d/%d", ctx->src[k], dst[k]);
            k++;
        } else {
            fprintf(ctx->tbl_file," *");
        }
    }

    for(int i = 0; i < eLbls; i++)
        fprintf(ctx->tbl_file, " %d", ti->labels[i]);

    fprintf(ctx->tbl_file,"\n");
    (void)cpy;
}

static void enum_edges(void *context, int *src)
{
    output_context* ctx = (output_context*)context;

    ctx->src = src;
    GBgetTransitionsShort(model, ctx->group, ctx->src, etf_edge, context);
}

typedef struct {
    FILE *tbl_file;
    int   mapno;
    int   len;
    int  *used;
} map_context;

static void
enum_map(void *context, int *src){
    map_context *ctx = (map_context*)context;
    int val = GBgetStateLabelShort(model, ctx->mapno, src);

    for (int i = 0, k = 0; i < N; i++) {
        if (k < ctx->len && ctx->used[k] == i){
            fprintf(ctx->tbl_file, "%d ", src[k]);
            k++;
        } else {
            fprintf(ctx->tbl_file, "* ");
        }
    }

    fprintf(ctx->tbl_file, "%d\n", val);
}

static void
output_init(FILE *tbl_file)
{
    int state[N];

    GBgetInitialState(model, state);
    fprintf(tbl_file, "begin state\n");

    for (int i = 0; i < N; i++) {
        fprint_ltsmin_ident(tbl_file, lts_type_get_state_name(ltstype, i));
        fprintf(tbl_file, ":");
        fprint_ltsmin_ident(tbl_file, lts_type_get_state_type(ltstype, i));
        fprintf(tbl_file, (i == (N - 1))?"\n":" ");
    }

    fprintf(tbl_file,"end state\n");

    fprintf(tbl_file,"begin edge\n");
    for(int i = 0; i < eLbls; i++) {
        fprint_ltsmin_ident(tbl_file, lts_type_get_edge_label_name(ltstype, i));
        fprintf(tbl_file, ":");
        fprint_ltsmin_ident(tbl_file, lts_type_get_edge_label_type(ltstype, i));
        fprintf(tbl_file, (i == (eLbls - 1))?"\n":" ");
    }

    fprintf(tbl_file, "end edge\n");

    fprintf(tbl_file, "begin init\n");

    for(int i = 0; i < N; i++)
        fprintf(tbl_file, "%d%s", state[i], (i == (N - 1))?"\n":" ");

    fprintf(tbl_file,"end init\n");
}

static void
output_trans(FILE *tbl_file)
{
    int tbl_count = 0;
    output_context ctx;

    ctx.model = model;
    ctx.tbl_file = tbl_file;

    for(int g = 0; g < nGrps; g++) {
        ctx.group = g;
        ctx.tbl_count = 0;
        fprintf(tbl_file, "begin trans\n");
        vset_enum(group_explored[g], enum_edges, &ctx);
        fprintf(tbl_file, "end trans\n");
        tbl_count += ctx.tbl_count;
    }

    Warning(info, "Symbolic tables have %d reachable transitions", tbl_count);
}

static void
output_lbls(FILE *tbl_file, vset_t visited)
{
    matrix_t *sl_info = GBgetStateLabelInfo(model);

    nGuards = dm_nrows(sl_info);

    if (dm_nrows(sl_info) != lts_type_get_state_label_count(ltstype))
        Warning(error, "State label count mismatch!");

    for (int i = 0; i < nGuards; i++){
        int len = dm_ones_in_row(sl_info, i);
        int used[len];

        // get projection
        for (int pi = 0, pk = 0; pi < dm_ncols (sl_info); pi++) {
            if (dm_is_set (sl_info, i, pi))
                used[pk++] = pi;
        }

        vset_t patterns = vset_create(domain, len, used);
        map_context ctx;

        vset_project(patterns, visited);
        ctx.tbl_file = tbl_file;
        ctx.mapno = i;
        ctx.len = len;
        ctx.used = used;
        fprintf(tbl_file, "begin map ");
        fprint_ltsmin_ident(tbl_file, lts_type_get_state_label_name(ltstype,i));
        fprintf(tbl_file, ":");
        fprint_ltsmin_ident(tbl_file, lts_type_get_state_label_type(ltstype,i));
        fprintf(tbl_file,"\n");
        vset_enum(patterns, enum_map, &ctx);
        fprintf(tbl_file, "end map\n");
        vset_destroy(patterns);
    }
}

static void
output_types(FILE *tbl_file)
{
    int type_count = lts_type_get_type_count(ltstype);

    for (int i = 0; i < type_count; i++) {
        Warning(info, "dumping type %s", lts_type_get_type(ltstype, i));
        fprintf(tbl_file, "begin sort ");
        fprint_ltsmin_ident(tbl_file, lts_type_get_type(ltstype, i));
        fprintf(tbl_file, "\n");

        int values = pins_chunk_count (model,i);

        for (int j = 0; j < values; j++) {
            chunk c    = pins_chunk_get (model, i, j);
            size_t len = c.len * 2 + 6;
            char str[len];

            chunk2string(c, len, str);
            fprintf(tbl_file, "%s\n", str);
        }

        fprintf(tbl_file,"end sort\n");
    }
}

void
do_output(char *etf_output, vset_t visited)
{
    FILE      *tbl_file;
    rt_timer_t  timer    = RTcreateTimer();

    RTstartTimer(timer);
    Warning(info, "writing output");
    tbl_file = fopen(etf_output, "w");

    if (tbl_file == NULL)
        AbortCall("could not open %s", etf_output);

    if (vdom_separates_rw(domain)) {
        /*
         * This part is necessary because the ETF format does not yet support
         * read, write and copy. This part should thus be removed when ETF is
         * extended.
         */
        Warning(info, "Note: ETF format does not yet support read, write and copy.");
        transitions_short = GBgetTransitionsShort;

        RTfree (r_projs);
        RTfree (w_projs);
        w_projs = r_projs = (ci_list **) dm_rows_to_idx_table (GBgetDMInfo(model));
        for (int i = 0; i < nGrps; i++) {
            vset_destroy(group_explored[i]);
            group_explored[i] = vset_create(domain, r_projs[i]->count, r_projs[i]->data);
            vset_project(group_explored[i], visited);
        }
    }

    output_init(tbl_file);
    output_trans(tbl_file);
    output_lbls(tbl_file, visited);
    output_types(tbl_file);

    fclose(tbl_file);
    RTstopTimer(timer);
    RTprintTimer(info, timer, "writing output took");
}

static void
save_snapshot_vset(FILE *f)
{
    /* Call hook */
    vset_pre_save(f, domain);

    /* Write domain */
    vdom_save(f, domain);

    /* Write initial state */
    vset_save(f, initial);

    /* Write number of transitions and all transitions */
    fwrite(&nGrps, sizeof(int), 1, f);
    for (int i=0; i<nGrps; i++) vrel_save_proj(f, group_next[i]);
    for (int i=0; i<nGrps; i++) vrel_save(f, group_next[i]);

    /* Write reachable states */
    int save_reachable = 1;
    fwrite(&save_reachable, sizeof(int), 1, f);
    vset_save(f, visited);

    /* Call hook */
    vset_post_save(f, domain);

    /* Now write action labels */
    int action_count = 0;
    if (act_label != -1) action_count = pins_chunk_count(model, action_typeno);
    fwrite(&action_count, sizeof(int), 1, f);
    for (int i=0; i<action_count; i++) {
        chunk ch = pins_chunk_get(model, action_typeno, i);
        uint32_t len = ch.len;
        char *action = ch.data;
        fwrite(&len, sizeof(uint32_t), 1, f);
        fwrite(action, sizeof(char), len, f);
    }
}

void
do_dd_output (vset_t initial, vset_t visited, char* file)
{
    // if not .etf, then the filename ends with .bdd or .ldd, symbolic LTS
    FILE *f = fopen(file, "w");
    if (f == NULL) Abort("Cannot open '%s' for writing!", file);

    save_snapshot_vset(f);

    /* Done! */
    fclose(f);

    Print(infoShort, "Result symbolic LTS written to '%s'", file);
}

void
stats_and_progress_report(vset_t current, vset_t visited, int level)
{
    long   n_count;
    long double e_count;

    if (sat_strategy == NO_SAT || log_active (infoLong)) {
        Print(infoShort, "level %d is finished", level);
    }
    if (log_active (infoLong) || peak_nodes) {
        if (current != NULL) {
            int digs = vset_count_fn (current, &n_count, &e_count);
            Print(infoLong, "level %d has %.*Lg states ( %ld nodes )", level, digs, e_count, n_count);
            if (n_count > max_lev_count) max_lev_count = n_count;
        }
        int digs = vset_count_fn (visited, &n_count, &e_count);
        Print(infoLong, "visited %d has %.*Lg states ( %ld nodes )", level, digs, e_count, n_count);

        if (n_count > max_vis_count) max_vis_count = n_count;

        if (log_active (debug)) {
            Debug("transition caches ( grp nds elts ):");

            for (int i = 0; i < nGrps; i++) {
                vrel_count(group_next[i], &n_count, NULL);
                Debug("( %d %ld ) ", i, n_count);

                if (n_count > max_trans_count) max_trans_count = n_count;
            }

            Debug("\ngroup explored    ( grp nds elts ): ");

            for (int i = 0; i < nGrps; i++) {
                vset_count(group_explored[i], &n_count, NULL);
                Debug("( %d %ld) ", i, n_count);

                if (n_count > max_grp_count) max_grp_count = n_count;
            }
        }
    }

    if (dot_dir != NULL) {

        FILE *fp;
        char *file;

        file = "%s/current-l%d.dot";
        char fcbuf[snprintf(NULL, 0, file, dot_dir, level)];
        sprintf(fcbuf, file, dot_dir, level);

        fp = fopen(fcbuf, "w+");
        vset_dot(fp, current);
        fclose(fp);

        file = "%s/visited-l%d.dot";
        char fvbuf[snprintf(NULL, 0, file, dot_dir, level)];
        sprintf(fvbuf, file, dot_dir, level);

        fp = fopen(fvbuf, "w+");
        vset_dot(fp, visited);
        fclose(fp);

        for (int i = 0; i < nGrps; i++) {
            file = "%s/group_next-l%d-k%d.dot";
            char fgbuf[snprintf(NULL, 0, file, dot_dir, level, i)];
            sprintf(fgbuf, file, dot_dir, level, i);
            fp = fopen(fgbuf, "w+");
            vrel_dot(fp, group_next[i]);
            fclose(fp);
        }

        for (int g = 0; g < nGuards && PINS_USE_GUARDS; g++) {
            file = "%s/guard_false-l%d-g%d.dot";
            char fgfbuf[snprintf(NULL, 0, file, dot_dir, level, g)];
            sprintf(fgfbuf, file, dot_dir, level, g);
            fp = fopen(fgfbuf, "w+");
            vset_dot(fp, label_false[g]);
            fclose(fp);

            file = "%s/guard_true-l%d-g%d.dot";
            char fgtbuf[snprintf(NULL, 0, file, dot_dir, level, g)];
            sprintf(fgtbuf, file, dot_dir, level, g);
            fp = fopen(fgtbuf, "w+");
            vset_dot(fp, label_true[g]);
            fclose(fp);
        }
    }
}

void
final_stat_reporting(vset_t visited)
{
    RTprintTimer(info, reach_timer, "reachability took");

    if (dlk_detect) Warning(info, "No deadlocks found");

    if (act_detect != NULL) {
        Warning(info, "%d different actions with prefix \"%s\" are found", ErrorActions, act_detect);
    }

    long n_count;
    Print(infoShort, "counting visited states...");
    rt_timer_t t = RTcreateTimer();
    RTstartTimer(t);
    char states[128];
    long double e_count;
    int digs = vset_count_fn(visited, &n_count, &e_count);
    snprintf(states, 128, "%.*Lg", digs, e_count);

    RTstopTimer(t);
    RTprintTimer(infoShort, t, "counting took");
    RTresetTimer(t);

    int is_precise = strstr(states, "e") == NULL && strstr(states, "inf") == NULL;

    Print(infoShort, "state space has%s %s states, %ld nodes", precise && is_precise ? " precisely" : "", states, n_count);

    if (!is_precise && precise) {
        if (vdom_supports_precise_counting(domain)) {
            Print(infoShort, "counting visited states precisely...");
            RTstartTimer(t);
            bn_int_t e_count;
            vset_count_precise(visited, n_count, &e_count);
            RTstopTimer(t);
            RTprintTimer(infoShort, t, "counting took");

            size_t len = bn_strlen(&e_count);
            char e_str[len];
            bn_int2string(e_str, len, &e_count);
            bn_clear(&e_count);

            Print(infoShort, "state space has precisely %s states (%zu digits)", e_str, strlen(e_str));
        } else Warning(info, "vset implementation does not support precise counting");
    }


    RTdeleteTimer(t);

    if (log_active (infoLong) || peak_nodes) {
        log_t l;
        if (peak_nodes) l = info;
        else l = infoLong;
        if (max_lev_count == 0) {
            Print(l, "( %ld final BDD nodes; %ld peak nodes )", n_count, max_vis_count);
        } else {
            Print(l,
                  "( %ld final BDD nodes; %ld peak nodes; %ld peak nodes per level )",
                  n_count, max_vis_count, max_lev_count);
        }

        if (log_active (debug)) {
            Debug("( peak transition cache: %ld nodes; peak group explored: " "%ld nodes )\n",
                  max_trans_count, max_grp_count);
        }
    }
}
