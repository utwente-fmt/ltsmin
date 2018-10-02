#include <hre/config.h>

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
#include <pins-lib/pg-types.h>
#include <pins-lib/pins.h>
#include <pins-lib/pins-impl.h>
#include <pins-lib/pins-util.h>
#include <pins-lib/pins2pins-group.h>
#include <pins-lib/pins2pins-mucalc.h>
#include <pins-lib/property-semantics.h>
#include <pins2lts-sym/alg/pg.h>
#include <pins2lts-sym/aux/options.h>
#include <ltsmin-lib/ltsmin-standard.h>
#include <ltsmin-lib/ltsmin-syntax.h>
#include <ltsmin-lib/ltsmin-tl.h>
#include <mc-lib/atomics.h>
#include <mc-lib/bitvector-ll.h>
#include <util-lib/bitset.h>
#include <util-lib/dfs-stack.h>
#include <util-lib/util.h>


static bool debug_output_enabled = false;

/**
 * \brief Computes the subset of v that belongs to player <tt>player</tt>.
 * \param vars the indices of variables of player <tt>player</tt>.
 */
static inline void
add_variable_subset(vset_t dst, vset_t src, vdom_t domain, int var_index)
{
    //Warning(info, "add_variable_subset: var_index=%d", var_index);
    int p_len = 1;
    int proj[1] = {var_pos}; // position 0 encodes the variable
    int match[1] = {var_index}; // the variable
    vset_t u = vset_create(domain, -1, NULL);
    vset_copy_match_proj(u, src, p_len, proj, variable_projection, match);

    if (debug_output_enabled && log_active(infoLong))
    {
        double e_count;
        vset_count(u, NULL, &e_count);
        if (e_count > 0) Print(infoLong, "add_variable_subset: %d: %.*g states", var_index, DBL_DIG, e_count);
    }

    vset_union(dst, u);
    vset_destroy(u);
}

/**
 * \brief Initialises the data structures for generating symbolic parity games.
 */
void
init_spg(model_t model)
{
    lts_type_t type = GBgetLTStype(model);
    var_pos = 0;
    var_type_no = 0;
    for(int i=0; i<N; i++)
    {
        //Printf(infoLong, "%d: %s (%d [%s])\n", i, lts_type_get_state_name(type, i), lts_type_get_state_typeno(type, i), lts_type_get_state_type(type, i));
        char* str1;
        if (is_pbes_tool) {
            str1 = "string"; // for the PBES language module
        } else {
            str1 = "mu"; // for the mu-calculus PINS layer
        }
        size_t strlen1 = strlen(str1);
        char* str2 = lts_type_get_state_type(type, i);
        size_t strlen2 = strlen(str2);
        if (strlen1==strlen2 && strncmp(str1, str2, strlen1)==0)
        {
            var_pos = i;
            var_type_no = lts_type_get_state_typeno(type, i);
            if (GBhaveMucalc()) {
                true_index = 0; // enforced by mucalc parser (mucalc-grammar.lemon / mucalc-syntax.c)
                false_index = 1;
            } else { // required for the PBES language module.
                true_index = pins_chunk_put (model, var_type_no, chunk_str("true"));
                false_index = pins_chunk_put (model, var_type_no, chunk_str("false"));
            }
        }
    }
    int p_len = 1;
    int proj[1] = {var_pos}; // position 0 encodes the variable
    variable_projection = vproj_create(domain, p_len, proj);

    num_vars = pins_chunk_count (model, var_type_no); // number of propositional variables
    if (GBhaveMucalc()) {
        num_vars = GBgetMucalcNodeCount(); // number of mu-calculus subformulae
    }
    Print(infoLong, "init_spg: var_type_no=%d, num_vars=%zu", var_type_no, num_vars);
    priority = RTmalloc(num_vars * sizeof(int)); // priority of variables
    player = RTmalloc(num_vars * sizeof(int)); // player of variables
    for(size_t i=0; i<num_vars; i++)
    {
        lts_type_t type = GBgetLTStype(model);
        int state_length = lts_type_get_state_length(type);
        // create dummy state with variable i:
        int state[state_length];
        for(int j=0; j < state_length; j++)
        {
            state[j] = 0;
        }
        state[var_pos] = i;
        int label = GBgetStateLabelLong(model, PG_PRIORITY, state); // priority
        priority[i] = label;
        if (label < min_priority) {
            min_priority = label;
        }
        if (label > max_priority) {
            max_priority = label;
        }
        //Print(infoLong, "  label %d (priority): %d", 0, label);
        label = GBgetStateLabelLong(model, PG_PLAYER, state); // player
        player[i] = label;
        //Print(infoLong, "  label %d (player): %d", 1, label);
    }
    true_states = vset_create(domain, -1, NULL);
    false_states = vset_create(domain, -1, NULL);
}

/**
 * \brief Creates a symbolic parity game from the generated LTS.
 */
parity_game *
compute_symbolic_parity_game(vset_t visited, int *src)
{
    Print(infoShort, "Computing symbolic parity game.");
    debug_output_enabled = true;

    // num_vars and player have been pre-computed by init_pbes.
    parity_game* g = spg_create(domain, N, nGrps, min_priority, max_priority);
    for(int i=0; i < N; i++)
    {
        g->src[i] = src[i];
    }
    vset_copy(g->v, visited);
    for(size_t i = 0; i < num_vars; i++)
    {
        // players
        Print(infoLong, "Adding nodes for var %zu (player %d).", i, player[i]);
        add_variable_subset(g->v_player[player[i]], g->v, g->domain, i);
        // priorities
        add_variable_subset(g->v_priority[priority[i]], g->v, g->domain, i);
    }
    if (log_active(infoLong))
    {
        for(int p = 0; p < 2; p++)
        {
            long   n_count;
            double elem_count;
            vset_count(g->v_player[p], &n_count, &elem_count);
            Print(infoLong, "player %d: %ld nodes, %.*g elements.", p, n_count, DBL_DIG, elem_count);
        }
        for(int p = min_priority; p <= max_priority; p++)
        {
            long   n_count;
            double elem_count;
            vset_count(g->v_priority[p], &n_count, &elem_count);
            Print(infoLong, "priority %d: %ld nodes, %.*g elements.", p, n_count, DBL_DIG, elem_count);
        }
    }
    for(int i = 0; i < nGrps; i++)
    {
        g->e[i] = group_next[i];
    }
    return g;
}

void
lts_to_pg_solve (vset_t visited, int* src)
{
    // converting the LTS to a symbolic parity game, save and solve.
    vset_destroy (true_states);
    vset_destroy (false_states);
    if (pg_output || pgsolve_flag) {
        rt_timer_t compute_pg_timer = RTcreateTimer ();
        RTstartTimer (compute_pg_timer);
        parity_game* g = compute_symbolic_parity_game (visited, src);
        RTstopTimer (compute_pg_timer);
        RTprintTimer (info, compute_pg_timer,
                      "computing symbolic parity game took");
        if (pg_output) {
            Print(info, "Writing symbolic parity game to %s.", pg_output);
            FILE* f = fopen (pg_output, "w");
            spg_save (f, g);
            fclose (f);
        }
        if (pgsolve_flag) {
            spgsolver_options* spg_options = spg_get_solver_options ();
            rt_timer_t pgsolve_timer = RTcreateTimer ();
            Print(info, "Solving symbolic parity game for player %d.",
                  spg_options->player);
            RTstartTimer (pgsolve_timer);
            recursive_result strategy;
            parity_game* copy = NULL;
            if (spg_options->check_strategy) {
                copy = spg_copy (g);
            }
            bool result = spg_solve (g, &strategy, spg_options);
            Print(info, " ");
            Print(info, "The result is: %s.", result ? "true" : "false");
            RTstopTimer (pgsolve_timer);
            Print(info, " ");
            RTprintTimer (info, reach_timer, "reachability took   ");
            RTprintTimer (info, compute_pg_timer, "computing game took ");
            RTprintTimer (info, pgsolve_timer, "solving took        ");
            if (spg_options->strategy_filename != NULL) {
                Print(info, "Writing winning strategies to %s",
                      spg_options->strategy_filename);
                FILE* f = fopen (spg_options->strategy_filename, "w");
                result_save (f, strategy);
                fclose (f);
            }
            if (spg_options->check_strategy) {
                check_strategy (copy, &strategy, spg_options->player, result,
                                10);
            }
        } else {
            spg_destroy (g);
        }
    }
    if (player != 0) {
        RTfree (player);
        RTfree (priority);
    }
}
