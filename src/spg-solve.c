/*
 * \file pg-solve.c
 *
 *  Created on: 23 Jan 2012
 *      Author: kant
 */
#include <limits.h>
#if HAVE_PROFILER
#include <gperftools/profiler.h>
#endif

#include <spg-solve.h>
#include <runtime.h>

static int chaining_attractor_flag = 0;
static int saturating_attractor_flag = 0;

struct poptOption spg_solve_options[]={
    { "chaining-attractor" , 0 , POPT_ARG_NONE , &chaining_attractor_flag, 0, "Use attractor with chaining.","" },
    { "saturating-attractor" , 0 , POPT_ARG_NONE , &saturating_attractor_flag, 0, "Use attractor with saturation.","" },
    POPT_TABLEEND
};


static inline void
get_vset_size(vset_t set, long *node_count,
                  char *elem_str, ssize_t str_len)
{
    bn_int_t elem_count;
    int      len;

    vset_count(set, node_count, &elem_count);
    len = bn_int2string(elem_str, str_len, &elem_count);

    if (len >= str_len)
        Fatal(1, error, "Error converting number to string");

    bn_clear(&elem_count);
}


void report_game(const parity_game* g)
{
    long   n_count;
    char   elem_str[1024];
    Warning(info, "");
    get_vset_size(g->v, &n_count, elem_str, sizeof(elem_str));
    Warning(info, "parity_game: g->v has %s elements", elem_str);
    //Warning(info, "parity_game: min_priority = %d, max_priority = %d", g->min_priority, g->max_priority);
    for(int i=g->min_priority; i<= g->max_priority; i++)
    {
        get_vset_size(g->v_priority[i], &n_count, elem_str, sizeof(elem_str));
        Warning(info, "parity_game: g->v_priority[%d] has %s elements", i, elem_str);
    }
    for(int i=0; i < 2; i++)
    {
        get_vset_size(g->v_player[i], &n_count, elem_str, sizeof(elem_str));
        Warning(info, "parity_game: g->v_player[%d] has %s elements", i, elem_str);
    }
}


/**
 * \brief Creates a new parity game.
 */
parity_game* spg_create(const vdom_t domain, int state_length, int num_groups, int min_priority, int max_priority)
{
    parity_game* result = (parity_game*)RTmalloc(sizeof(parity_game));
    result->domain = domain;
    result->state_length = state_length;
    result->src = (int*)RTmalloc(state_length * sizeof(int));
    result->v = vset_create(domain, -1, NULL);
    for(int i=0; i<2; i++) {
        result->v_player[i] = vset_create(domain, -1, NULL);
    }
    result->min_priority = min_priority;
    result->max_priority = max_priority;
    result->v_priority = (vset_t*)RTmalloc((max_priority+1) * sizeof(vset_t));
    for(int i=min_priority; i<=max_priority; i++) {
        result->v_priority[i] = vset_create(domain, -1, NULL);
    }
    result->num_groups = num_groups;
    result->e = (vrel_t*)RTmalloc(num_groups * sizeof(vrel_t));
    return result;
}


/**
 * Clears the parity game from memory.
 */
void spg_destroy(parity_game* g)
{
    RTfree(g->src);
    vset_destroy(g->v);
    vset_destroy(g->v_player[0]);
    vset_destroy(g->v_player[1]);
    RTfree(g->e);
    for(int i = g->min_priority; i<g->max_priority; i++) {
        vset_destroy(g->v_priority[i]);
    }
    RTfree(g->v_priority);
    RTfree(g);
}

/**
 *
 */
void spg_save(FILE* f, parity_game* g)
{
    fprintf(f,"symbolic parity game\n");
    fprintf(f,"state_length=%d\n", g->state_length);
    fprintf(f,"num_groups=%d\n", g->num_groups);
    fprintf(f,"min_priority=%d\n", g->min_priority);
    fprintf(f,"max_priority=%d\n", g->max_priority);
    fprintf(f,"init=");
    for(int i=0; i<g->state_length; i++) {
        fprintf(f,((i<g->state_length-1)?"%d ":"%d"), g->src[i]);
    }
    fprintf(f,"\n");
    for(int i=0; i<g->num_groups; i++) {
        fprintf(f,"rel proj e[%d]\n", i);
        vrel_save_proj(f, g->e[i]);
    }
    for(int i=0; i<g->num_groups; i++) {
        fprintf(f,"rel e[%d]\n", i);
        vrel_save(f, g->e[i]);
    }
    fprintf(f,"set v\n");
    vset_save(f, g->v);
    for(int i=0; i<2; i++) {
        fprintf(f,"set v_player[%d]\n", i);
        vset_save(f, g->v_player[i]);
    }
    for(int i=g->min_priority; i<=g->max_priority; i++) {
        fprintf(f,"set v_priority[%d]\n", i);
        vset_save(f, g->v_priority[i]);
    }
}

parity_game* spg_load(FILE* f, vset_implementation_t impl)
{
    int state_length = 0;
    int num_groups = 0;
    int min_priority = 0;
    int max_priority = 0;
    int res = fscanf(f,"symbolic parity game\n");
    //if (res <= 0) {
    //    Abort("Wrong file format: %d.", res)
    //}
    res &= fscanf(f,"state_length=%d\n", &state_length);
    res &= fscanf(f,"num_groups=%d\n", &num_groups);
    res &= fscanf(f,"min_priority=%d\n", &min_priority);
    res &= fscanf(f,"max_priority=%d\n", &max_priority);
    Warning(info, "Loading symbolic parity game. "
            "state_length=%d, num_groups=%d, min_priority=%d, max_priority=%d",
            state_length, num_groups, min_priority, max_priority);
    vdom_t domain = vdom_create_domain(state_length, impl);
    parity_game* result = spg_create(domain, state_length, num_groups, min_priority, max_priority);
    res &= fscanf(f,"init=");
    for(int i=0; i<state_length; i++) {
        res &= fscanf(f,((i<state_length-1)?"%d ":"%d"), &(result->src[i]));
    }
    res &= fscanf(f,"\n");
    int val;
    for(int i=0; i<num_groups; i++) {
        res &= fscanf(f,"rel proj e[%d]\n", &val);
        //Warning(info, "Reading e[%d] proj.", i);
        result->e[i] = vrel_load_proj(f, domain);
    }
    for(int i=0; i<num_groups; i++) {
        res &= fscanf(f,"rel e[%d]\n", &val);
        //Warning(info, "Reading e[%d].", i);
        vrel_load(f, result->e[i]);
    }
    res &= fscanf(f,"set v\n");
    //Warning(info, "Reading v.");
    vset_t v = vset_load(f, domain);
    vset_copy(result->v, v);
    vset_destroy(v);
    for(int i=0; i<2; i++) {
        res &= fscanf(f,"set v_player[%d]\n", &val);
        //Warning(info, "Reading v_player[%d].", i);
        v = vset_load(f, domain);
        vset_copy(result->v_player[i], v);
        vset_destroy(v);
    }
    for(int i=min_priority; i<=max_priority; i++) {
        res &= fscanf(f,"set v_priority[%d]\n", &val);
        //Warning(info, "Reading v_priority[%d].", i);
        v = vset_load(f, domain);
        vset_copy(result->v_priority[i], v);
        vset_destroy(v);
    }
    Warning(info, "Done loading symbolic parity game.");
    return result;
}

/**
 * \brief Creates a deep copy of g.
 */
parity_game* spg_copy(const parity_game* g)
{
    parity_game* result = spg_create(g->domain, g->state_length, g->num_groups, g->min_priority, g->max_priority);
    for(int i=0; i<g->state_length; i++) {
        result->src[i] = g->src[i];
    }
    vset_copy(result->v, g->v);
    for(int i=0; i<2; i++) {
        vset_copy(result->v_player[i], g->v_player[i]);
    }
    for(int i=g->min_priority; i<=g->max_priority; i++) {
        vset_copy(result->v_priority[i], g->v_priority[i]);
    }
    for(int i=0; i<g->num_groups; i++) {
        result->e[i] = g->e[i];
    }
    return result;
}


/**
 * \brief Creates a new spgsolver_options object.
 */
spgsolver_options* spg_get_solver_options()
{
    spgsolver_options* options = (spgsolver_options*)RTmalloc(sizeof(spgsolver_options));
    options->chaining = (chaining_attractor_flag > 0);
    options->saturation = (saturating_attractor_flag > 0);
    options->spg_solve_timer = SCCcreateTimer();
    return options;
}


/**
 * \brief Destroy options object
 */
void spg_destroy_solver_options(spgsolver_options* options)
{
    SCCdeleteTimer(options->spg_solve_timer);
    RTfree(options);
}


/**
 * \brief Determines if player 0 is the winner of the game.
 */
bool spg_solve(const parity_game* g, spgsolver_options* options)
{
    spgsolver_options* opts;
    if (options==NULL || options==0) {
        opts = spg_get_solver_options();
    } else {
        opts = options;
    }
#if HAVE_PROFILER
    ProfilerStart("spgsolver.perf");
#endif
    SCCstartTimer(opts->spg_solve_timer);
    recursive_result result = spg_solve_recursive(g, opts);
    SCCstopTimer(opts->spg_solve_timer);
#if HAVE_PROFILER
    ProfilerStop();
#endif
    return vset_member(result.win[0], g->src);
}


/**
 * \brief Recursively computes the winning sets for both players.
 */
recursive_result spg_solve_recursive(const parity_game* g,  const spgsolver_options* options)
{
    long   n_count;
    bn_int_t elem_count;
    vset_count(g->v, &n_count, &elem_count);
    Warning(info, "");
    Warning(info, "solve_recursive: game has %d nodes", n_count);
    //report_game(g);
    //vdom_t domain = g->domain;
    recursive_result result;
    if (vset_is_empty(g->v)) {
        Warning(info, "solve_recursive: game is empty.");
        result.win[0] = vset_create(g->domain, -1, NULL);
        result.win[1] = vset_create(g->domain, -1, NULL);
        return result;
    }

    // compute U <- {v \in V | p(v) = m}
    Warning(info, "  min = %d, max = %d", g->min_priority, g->max_priority);
    int m = g->min_priority;
    vset_t u = vset_create(g->domain, -1, NULL);
    vset_copy(u, g->v_priority[m]);
    vset_count(u, &n_count, &elem_count);
    while(vset_is_empty(u)) {
        m++;
        if (m > g->max_priority) {
            Fatal(a, error, "no min priority found!");
        }
        vset_clear(u);
        vset_copy(u, g->v_priority[m]);
        vset_count(u, &n_count, &elem_count);
    }
    Warning(info, "m = %d, u has %d nodes.", m, n_count);

    int player = m % 2;

    // U = attr_player(G, U)
    options->chaining ? spg_attractor_chaining(player, g, u, options) : spg_attractor(player, g, u, options);

    parity_game* g_minus_u = spg_copy(g);
    spg_game_restrict(g_minus_u, u, options);
    //report_game(g_minus_u);

    vset_count(g_minus_u->v, &n_count, &elem_count);
    //Warning(info, "  g_minus_u.v has %d nodes", n_count);
    recursive_result x = spg_solve_recursive(g_minus_u, options);
    spg_destroy(g_minus_u);
    if (vset_is_empty(x.win[1-player])) {
        vset_union(u, x.win[player]);
        vset_destroy(x.win[player]);
        vset_destroy(x.win[1-player]);
        result.win[player] = u;
        result.win[1-player] = vset_create(g->domain, -1, NULL);
    } else {
        vset_destroy(u);
        vset_destroy(x.win[player]);
        vset_t b = vset_create(g->domain, -1, NULL);
        vset_copy(b, x.win[1-player]);
        vset_destroy(x.win[1-player]);
        options->chaining ? spg_attractor_chaining(1-player, g, b, options) : spg_attractor(1-player, g, b, options);
        parity_game* g_minus_b = spg_copy(g);
        spg_game_restrict(g_minus_b, b, options);
        recursive_result y = spg_solve_recursive(g_minus_b, options);
        spg_destroy(g_minus_b);
        result.win[player] = y.win[player];
        vset_union(b, y.win[1-player]);
        vset_destroy(y.win[1-player]);
        result.win[1-player] = b;
    }
    return result;
}


/**
 * \brief Computes and returns G = G - A.
 */
void spg_game_restrict(parity_game *g, vset_t a, const spgsolver_options* options)
{
    (void)options;
    long   n_count;
    bn_int_t elem_count;
    vset_count(a, &n_count, &elem_count);
    //Warning(info, "game_restrict: a has %d nodes", n_count);
    vset_minus(g->v, a); // compute V - A:
    vset_count(g->v, &n_count, &elem_count);
    //Warning(info, "  g->v has %d nodes", n_count);
    for(int i=0; i<2; i++) {
        vset_minus(g->v_player[i], a); // compute V - A:
    }
    for(int i=g->min_priority; i<=g->max_priority; i++) {
        //Warning(info, "  priority: %d", i);
        vset_minus(g->v_priority[i], a); // compute V - A:
        vset_count(g->v_priority[i], &n_count, &elem_count);
        //Warning(info, "  result->v_priority[%d] a has %d nodes", i, n_count);
        if (i==g->min_priority && i < g->max_priority && vset_is_empty(g->v_priority[i])) {
            g->min_priority++;
        }
    }
    while(g->max_priority > g->min_priority
            && vset_is_empty(g->v_priority[g->max_priority])) {
        g->max_priority--;
    }
    // TODO: compute E \intersects (V-A x V-A)? -- needs extension of the vset interface
}


/**
 * \brief Computes the attractor set for G, U.
 * The resulting set is stored in U.
 */
void spg_attractor(int player, const parity_game* g, vset_t u, const spgsolver_options* options)
{
    //vdom_t domain = g->domain;
    vset_t v_level = vset_create(g->domain, -1, NULL);
    vset_copy(v_level, u);
    int l = 0;
    // Compute fixpoint
    while (!vset_is_empty(v_level)) {
        /*
        long   u_count;
        bn_int_t u_elem_count;
        vset_count(u, &u_count, &u_elem_count);
        long   level_count;
        bn_int_t level_elem_count;
        vset_count(v_level, &level_count, &level_elem_count);
        SCCstopTimer(options->spg_solve_timer);
        Warning(info, "attr_%d^%d [%5.3f]: u has %ld nodes, v_level has %ld nodes.",
                SCCrealTime(options->spg_solve_timer), player, l, u_count, level_count);
        SCCstartTimer(options->spg_solve_timer);
        */

        // prev_attr = V \intersect prev(attr^k)
        vset_t prev_attr = vset_create(g->domain, -1, NULL);
        vset_t tmp = vset_create(g->domain, -1, NULL);
        for(int group=0; group<g->num_groups; group++) {
            vset_clear(tmp);
            vset_prev(tmp, v_level, g->e[group]);
            vset_intersect(tmp, g->v);
            vset_union(prev_attr, tmp);
        }

        // Compute V_player \intersects prev_attr
        vset_clear(v_level);
        vset_copy(v_level, prev_attr);
        vset_intersect(v_level, g->v_player[player]);

        // B = next(V \intersect prev_attr)
        vset_t b = vset_create(g->domain, -1, NULL);
        for(int group=0; group<g->num_groups; group++) {
            vset_clear(tmp);
            vset_next(tmp, prev_attr, g->e[group]);
            vset_intersect(tmp, g->v);
            vset_union(b, tmp);
        }

        // B = B - U
        vset_minus(b, u);

        // prev_b = V \intersect prev(B)
        vset_t prev_b = vset_create(g->domain, -1, NULL);
        for(int group=0; group<g->num_groups; group++) {
            vset_clear(tmp);
            vset_prev(tmp, b, g->e[group]);
            vset_intersect(tmp, g->v);
            vset_union(prev_b, tmp);
        }
        vset_destroy(tmp);
        vset_destroy(b);

        // Compute V_other_player \intersects (prev_attr - prev_b)
        vset_t attr_other_player = vset_create(g->domain, -1, NULL);
        vset_copy(attr_other_player, prev_attr);
        vset_destroy(prev_attr);
        vset_minus(attr_other_player, prev_b);
        vset_destroy(prev_b);
        vset_intersect(attr_other_player, g->v_player[1-player]);
        vset_union(v_level, attr_other_player);
        vset_destroy(attr_other_player);

        // copy result
        vset_minus(v_level, u);
        vset_union(u, v_level);
        l++;
    }
    Warning(info, "attr_%d: %d levels.", player, l);
    vset_destroy(v_level);
    (void)options;
}


/**
 * \brief Computes the attractor set for G, U.
 * The resulting set is stored in U.
 */
void spg_attractor_chaining(int player, const parity_game* g, vset_t u, const spgsolver_options* options)
{
    //vdom_t domain = g->domain;
    vset_t v_level = vset_create(g->domain, -1, NULL);
    vset_t v_previous_level = vset_create(g->domain, -1, NULL);
    vset_t v_group = vset_create(g->domain, -1, NULL);
    vset_copy(v_level, u);
    int l = 0;
    long peak_group_count = 0;
    // Compute fixpoint
    while (!vset_is_empty(v_level)) {
        /*
        long   u_count;
        bn_int_t u_elem_count;
        vset_count(u, &u_count, &u_elem_count);
        long   v_level_count;
        bn_int_t v_level_elem_count;
        vset_count(v_level, &v_level_count, &v_level_elem_count);
        SCCstopTimer(options->spg_solve_timer);
        Warning(info, "attr_%d^%d [%5.3f]: u has %ld nodes, v_level has %ld nodes, v_group has %ld nodes max.",
                SCCrealTime(options->spg_solve_timer), player, l, u_count, v_level_count, peak_group_count);
        SCCstartTimer(options->spg_solve_timer);
        peak_group_count = 0;
        */

        vset_copy(v_previous_level, v_level);
        vset_clear(v_level);
        for(int group=0; group<g->num_groups; group++) {
            vset_copy(v_group, v_previous_level);
            int k = 0;
            while ((options->saturation || k < 1) && !vset_is_empty(v_group)) {
                /*
                vset_count(u, &u_count, &u_elem_count);
                vset_count(v_level, &v_level_count, &v_level_elem_count);
                long   v_group_count;
                bn_int_t v_group_elem_count;
                vset_count(v_group, &v_group_count, &v_group_elem_count);
                //Warning(info, "  %d: u has %ld nodes, v_level has %ld nodes, v_group has %ld nodes.", k, u_count, v_level_count, v_group_count);
                if (v_group_count > peak_group_count) {
                    peak_group_count = v_group_count;
                }
                */

                // prev_attr = V \intersect prev(attr^k)
                vset_t prev_attr = vset_create(g->domain, -1, NULL);
                vset_prev(prev_attr, v_group, g->e[group]);
                vset_copy(v_group, prev_attr);
                vset_intersect(v_group, g->v_player[player]);

                vset_t prev_attr_other_player = prev_attr;
                vset_intersect(prev_attr_other_player, g->v_player[1-player]);
                // B = next(V \intersect prev_attr)
                vset_t b = vset_create(g->domain, -1, NULL);
                vset_t tmp = vset_create(g->domain, -1, NULL);
                for(int group=0; group<g->num_groups; group++) {
                    vset_clear(tmp);
                    vset_next(tmp, prev_attr_other_player, g->e[group]);
                    vset_intersect(tmp, g->v);
                    vset_union(b, tmp);
                }

                // B = B - U
                vset_minus(b, u);

                // prev_b = V \intersect prev(B)
                vset_t prev_b = vset_create(g->domain, -1, NULL);
                for(int group=0; group<g->num_groups; group++) {
                    vset_clear(tmp);
                    vset_prev(tmp, b, g->e[group]);
                    vset_intersect(tmp, g->v);
                    vset_union(prev_b, tmp);
                }
                vset_destroy(tmp);
                vset_destroy(b);

                // Compute V_other_player \intersects (prev_attr - prev_b)
                vset_minus(prev_attr_other_player, prev_b);
                vset_destroy(prev_b);
                vset_union(v_group, prev_attr_other_player);
                vset_minus(v_group, u);
                vset_destroy(prev_attr);

                // copy group result
                vset_union(v_level, v_group);
                vset_union(v_previous_level, v_group);
                vset_union(u, v_group);
                k++;
            }
            Warning(debug, "  group  %d: %d iterations.", group, k);
        }
        l++;
    }
    Warning(info, "attr_%d: %d levels.", player, l);
    vset_destroy(v_group);
    vset_destroy(v_level);
    vset_destroy(v_previous_level);
}
