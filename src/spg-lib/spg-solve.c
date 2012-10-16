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

#include <hre/user.h>
#include <spg-lib/spg-solve.h>

static vset_implementation_t vset_impl = VSET_IMPL_AUTOSELECT;

static int swap_flag = 0;
static int chaining_attractor_flag = 0;
static int saturating_attractor_flag = 0;

#ifdef LTSMIN_DEBUG
static int dot_flag = 0;
static int dot_count = 0;
#endif

struct poptOption spg_solve_options[]={
    { "chaining-attractor" , 0 , POPT_ARG_NONE , &chaining_attractor_flag, 0, "Use attractor with chaining.","" },
    { "saturating-attractor" , 0 , POPT_ARG_NONE , &saturating_attractor_flag, 0, "Use attractor with saturation.","" },
    { "pg-swap" , 0 , POPT_ARG_NONE , &swap_flag, 0, "Swap parity games to disk.","" },
#ifdef LTSMIN_DEBUG
    { "pg-write-dot" , 0 , POPT_ARG_NONE , &dot_flag, 0, "Write dot files to disk.","" },
#endif
    POPT_TABLEEND
};


static void
get_vset_size(vset_t set, long *node_count,
                  char *elem_str, ssize_t str_len)
{
    {
        bn_int_t elem_count;
        int      len;

        vset_count(set, node_count, &elem_count);

        len = bn_int2string(elem_str, str_len, &elem_count);

        if (len >= str_len)
            Abort("Error converting number to string");

        bn_clear(&elem_count);
    }
}


static void report_game(const parity_game* g)
{
    if (log_active(infoLong))
    {
        long   total_count;
        long   n_count;
        char   elem_str[1024];
        Warning(infoLong, "");
        get_vset_size(g->v, &n_count, elem_str, sizeof(elem_str));
        total_count = n_count;
        Print(infoLong, "parity_game: g->v has %s elements (%ld nodes)", elem_str, n_count);
        //Print(infoLong, "parity_game: min_priority = %d, max_priority = %d", g->min_priority, g->max_priority);
        for(int i=g->min_priority; i<= g->max_priority; i++)
        {
            get_vset_size(g->v_priority[i], &n_count, elem_str, sizeof(elem_str));
            total_count += n_count;
            Print(infoLong, "parity_game: g->v_priority[%d] has %s elements (%ld nodes)", i, elem_str, n_count);
        }
        for(int i=0; i < 2; i++)
        {
            get_vset_size(g->v_player[i], &n_count, elem_str, sizeof(elem_str));
            total_count += n_count;
            Print(infoLong, "parity_game: g->v_player[%d] has %s elements (%ld nodes)", i, elem_str, n_count);
        }
        Print(infoLong, "parity_game: %ld nodes total", total_count);
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
    result->v_priority_swapfile = RTmalloc((max_priority+1) * sizeof(tmp_file_t *));
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
    RTfree(g->v_priority_swapfile);
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
    Print(infoShort, "Loading symbolic parity game. "
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
        //Print(infoLong, "Reading e[%d] proj.", i);
        result->e[i] = vrel_load_proj(f, domain);
    }
    for(int i=0; i<num_groups; i++) {
        res &= fscanf(f,"rel e[%d]\n", &val);
        //Print(infoLong, "Reading e[%d].", i);
        vrel_load(f, result->e[i]);
    }
    res &= fscanf(f,"set v\n");
    //Print(infoLong, "Reading v.");
    vset_t v = vset_load(f, domain);
    vset_copy(result->v, v);
    vset_destroy(v);
    for(int i=0; i<2; i++) {
        res &= fscanf(f,"set v_player[%d]\n", &val);
        //Print(infoLong, "Reading v_player[%d].", i);
        v = vset_load(f, domain);
        vset_copy(result->v_player[i], v);
        vset_destroy(v);
    }
    for(int i=min_priority; i<=max_priority; i++) {
        res &= fscanf(f,"set v_priority[%d]\n", &val);
        //Print(infoLong, "Reading v_priority[%d].", i);
        v = vset_load(f, domain);
        vset_copy(result->v_priority[i], v);
        vset_destroy(v);
    }
    Print(infoShort, "Done loading symbolic parity game.");
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
    options->swap = (swap_flag > 0);
    options->dot = false;
#ifdef LTSMIN_DEBUG
    options->dot = (dot_flag > 0);
#endif
    options->chaining = (chaining_attractor_flag > 0);
    options->saturation = (saturating_attractor_flag > 0);
    options->spg_solve_timer = RTcreateTimer();
    return options;
}


/**
 * \brief Destroy options object
 */
void spg_destroy_solver_options(spgsolver_options* options)
{
    RTdeleteTimer(options->spg_solve_timer);
    RTfree(options);
}

tmp_file_t *
spg_swapfilename()
{
    tmp_file_t         *tmp = RTmalloc(sizeof(tmp_file_t));
    strcpy(tmp->buffer, "spg_XXXXXX");
    if ((tmp->fd = mkstemp(tmp->buffer)) == -1)
        AbortCall ("Unable to open file ``%s'' for writing", tmp->buffer);
    return tmp;
}

// hibernate
void spg_swap_game(parity_game* g, tmp_file_t *swap)
{
    FILE* swapfile = fdopen(swap->fd, "w");
    if (swapfile == NULL)
        AbortCall ("Unable to open file for writing ``%s''", swap->buffer);
    Print(infoLong, "Writing game to temporary file %s.", swap->buffer);
    spg_save(swapfile, g);
    fclose(swapfile);
}

// resume
parity_game* spg_unswap_game(tmp_file_t *swap)
{
    FILE* swapfile = fdopen(swap->fd, "r");
    if (swapfile == NULL)
        AbortCall ("Unable to open file ``%s''", swap->buffer);
    parity_game* g = spg_load(swapfile, vset_impl);
    fclose(swapfile);
    return g;
}

// hibernate
void vset_swap(vset_t s, char* swapfilename)
{
    FILE* swapfile = fopen(swapfilename, "w");
    if (swapfile == NULL)
        AbortCall ("Unable to open file ``%s'' for writing", swapfilename);
    Print(infoLong, "Writing set to temporary file %s.", swapfilename);
    vset_save(swapfile, s);
    fclose(swapfile);
}

// resume
void vset_unswap(vset_t s, char* swapfilename, vdom_t domain)
{
    FILE* swapfile = fopen(swapfilename, "r");
    if (swapfile == NULL)
        AbortCall ("Unable to open file ``%s''", swapfilename);
    vset_t t = vset_load(swapfile, domain);
    vset_copy(s, t);
    vset_destroy(t);
    fclose(swapfile);
}

void spg_partial_swap_game(parity_game* g)
{
    Print(infoLong, "Partially swapping game.");
    for(int i=g->min_priority; i <= g->max_priority; i++)
    {
        g->v_priority_swapfile[i] = spg_swapfilename();
        FILE* swapfile = fdopen(g->v_priority_swapfile[i]->fd, "w");
        if (swapfile == NULL)
            AbortCall ("Unable to open file ``%s'' for writing", g->v_priority_swapfile[i]->buffer);
        vset_save(swapfile, g->v_priority[i]);
        fclose(swapfile);
        vset_destroy(g->v_priority[i]);
    }
}

void spg_restore_swapped_game(parity_game* g)
{
    Print(infoLong, "Restoring partially swapped game.");
    for(int i=g->min_priority; i <= g->max_priority; i++)
    {
        FILE* swapfile = fdopen(g->v_priority_swapfile[i]->fd, "r");
        if (swapfile == NULL)
            AbortCall ("Unable to open file ``%s''", g->v_priority_swapfile[i]->buffer);
        g->v_priority[i] = vset_load(swapfile, g->domain);
        fclose(swapfile);
        free(g->v_priority_swapfile[i]); // tempnam()
    }
}

/**
 * \brief Determines if player 0 is the winner of the game.
 */
bool spg_solve(parity_game* g, spgsolver_options* options)
{
    spgsolver_options* opts;
    if (options==NULL || options==0) {
        opts = spg_get_solver_options();
    } else {
        opts = options;
    }
    int src[g->state_length];
    for(int i=0; i<g->state_length; i++)
    {
        src[i] = g->src[i];
    }
#if HAVE_PROFILER
    Print(infoLong, "Start profiling now.");
    ProfilerStart("spgsolver.perf");
#endif
    RTstartTimer(opts->spg_solve_timer);
    recursive_result result = spg_solve_recursive(g, opts);  // Note: g will be destroyed
    RTstopTimer(opts->spg_solve_timer);
#if HAVE_PROFILER
    ProfilerStop();
    Print(infoLong, "Done profiling.");
#endif
    return vset_member(result.win[0], src);
}


/**
 * \brief Recursively computes the winning sets for both players.
 * As a side-effect destroys the game g.
 */
recursive_result spg_solve_recursive(parity_game* g,  const spgsolver_options* options)
{
    if (log_active(infoLong))
    {
        long   n_count;
        bn_int_t elem_count;
        vset_count(g->v, &n_count, &elem_count);
        bn_clear(&elem_count);
        Print(infoLong, "");
        Print(infoLong, "solve_recursive: game has %ld nodes", n_count);
        report_game(g);
    }

#ifdef LTSMIN_DEBUG
    char dotfilename[255];
    FILE* dotfile;
    if (options->dot)
    {
        sprintf(dotfilename, "spg_set_%05d_v.dot", dot_count++);
        dotfile = fopen(dotfilename,"w");
        vset_dot(dotfile, g->v);
        fclose(dotfile);
    }
#endif

    recursive_result result;
    if (vset_is_empty(g->v)) {
        Print(infoLong, "solve_recursive: game is empty.");
        result.win[0] = vset_create(g->domain, -1, NULL);
        result.win[1] = vset_create(g->domain, -1, NULL);
        return result;
    }

    int player;
    vset_t u = vset_create(g->domain, -1, NULL);
    {
        long   n_count;
        bn_int_t elem_count;
        bool have_deadlock_states[2];
        vset_t deadlock_states[2];
        for(int p=0; p<2; p++)
        {
            have_deadlock_states[p] = false;
            deadlock_states[p] = vset_create(g->domain, -1, NULL);
            vset_copy(deadlock_states[p], g->v_player[p]);
            {
                vset_t t = vset_create(g->domain, -1, NULL);
                for(int group=0; group<g->num_groups; group++) {
                    vset_clear(t);
                    vset_prev(t, g->v, g->e[group]);
                    vset_minus(deadlock_states[p], t);
                }
                vset_destroy(t);
            }
            if (!vset_is_empty(deadlock_states[p]))
            {
                if(log_active(infoLong))
                {
                    vset_count(deadlock_states[p], &n_count, &elem_count);
                    size_t size = 1024;
                    char s[size];
                    bn_int2string(s, size, &elem_count);
                    bn_clear(&elem_count);
                    //Print(infoLong, "player[%d] - prev(V) = %d", 1-p, n_count);
                    Print(infoLong, "%s deadlock states (%ld nodes) with result '%s' (p=%d).", s, n_count, ((p==0)?"false":"true"), p);
                }
                have_deadlock_states[p] = true;
            }
        }

        // compute U <- {v \in V | p(v) = m}
        Print(infoLong, "  min = %d, max = %d", g->min_priority, g->max_priority);
        int m = g->min_priority;
        vset_copy(u, g->v_priority[m]);
        if (log_active(infoLong))
        {
            vset_count(u, &n_count, &elem_count);
            bn_clear(&elem_count);
        }
        while(vset_is_empty(u)) {
            m++;
            if (m > g->max_priority) {
                Abort("no min priority found!");
            }
            vset_clear(u);
            vset_copy(u, g->v_priority[m]);
            if (log_active(infoLong))
            {
                vset_count(u, &n_count, &elem_count);
                bn_clear(&elem_count);
            }
        }
        if (m > 0 && have_deadlock_states[1]) // deadlock states of player 1 (and) result in true: nu X0 = X0
        {
            m = 0;
        }
        else if (m > 1 && have_deadlock_states[0]) // deadlock states of player 0 (or) result in false: mu X1 = X1
        {
            m = 1;
        }

        player = m % 2;

        // Add deadlock states
        if (m < 2)
        {
            Print(infoLong, "Adding deadlock states (m=%d).", m);
            if (m >= g->min_priority)
            {
                vset_copy(u, g->v_priority[m]);
            }
            vset_union(u, deadlock_states[1-player]);
            if (log_active(infoLong))
            {
                vset_count(u, &n_count, &elem_count);
                bn_clear(&elem_count);
            }
        }
        for(int p=0; p<2; p++)
        {
            vset_destroy(deadlock_states[p]);
        }

        Print(infoLong, "m = %d, u has %ld nodes.", m, n_count);
    }

    // partial hibernate/swap parity game here: Save priorities to disk.
    //if (options->swap) spg_partial_swap_game(g);

    // U = attr_player(G, U)
    options->chaining ? spg_attractor_chaining(player, g, u, options) : spg_attractor(player, g, u, options);

    // restore the partially swapped game.
    //if (options->swap) spg_restore_swapped_game(g);

    recursive_result x;
    vset_t empty = vset_create(g->domain, -1, NULL);
    tmp_file_t *g_filename = NULL;
    {
        parity_game* g_minus_u = spg_copy(g);

        // save and destroy g
        if (options->swap)
        {
            g_filename = spg_swapfilename();
            spg_swap_game(g, g_filename);
            spg_destroy(g);
        }

        Print(infoLong, "g_minus_u:");
        report_game(g_minus_u);
        spg_game_restrict(g_minus_u, u, options);

        x = spg_solve_recursive(g_minus_u, options);
        //spg_destroy(g_minus_u); // has already been destroyed in spg_solve_recursive
    }

    if (vset_is_empty(x.win[1-player])) {
        vset_union(u, x.win[player]);
        vset_destroy(x.win[player]);
        vset_destroy(x.win[1-player]);
        result.win[player] = u;
        result.win[1-player] = empty;
    } else {
        vset_destroy(empty);
        vset_destroy(u);
        vset_destroy(x.win[player]);

        // load g
        if (options->swap)
        {
            g = spg_unswap_game(g_filename);
        }

        vset_t b = vset_create(g->domain, -1, NULL);
        vset_copy(b, x.win[1-player]);
        vset_destroy(x.win[1-player]);
        if (options->swap)
        {
            //spg_partial_swap_game(g);
        }

        options->chaining ? spg_attractor_chaining(1-player, g, b, options) : spg_attractor(1-player, g, b, options);

        if (options->swap)
        {
            //spg_restore_swapped_game(g);
        }

        {
            parity_game* g_minus_b = spg_copy(g);

            spg_destroy(g);

            spg_game_restrict(g_minus_b, b, options);
            recursive_result y = spg_solve_recursive(g_minus_b, options);
            //spg_destroy(g_minus_b); // has already been destroyed in spg_solve_recursive
            result.win[player] = y.win[player];
            vset_union(b, y.win[1-player]);
            vset_destroy(y.win[1-player]);
            result.win[1-player] = b;
        }
    }
    if (options->swap)
    {
        free(g_filename); // tempnam()
    }

    if (log_active(infoLong))
    {
        long   n_count;
        bn_int_t elem_count;
        vset_count(result.win[0], &n_count, &elem_count);
        bn_clear(&elem_count);
        Print(infoLong, "result.win[0]: %ld nodes.", n_count);
        vset_count(result.win[1], &n_count, &elem_count);
        bn_clear(&elem_count);
        Print(infoLong, "result.win[1]: %ld nodes.", n_count);
    }

#ifdef LTSMIN_DEBUG
    if (options->dot)
    {
        sprintf(dotfilename, "spg_set_%05d_win0.dot", dot_count++);
        dotfile = fopen(dotfilename,"w");
        vset_dot(dotfile, result.win[0]);
        fclose(dotfile);
        sprintf(dotfilename, "spg_set_%05d_win1.dot", dot_count++);
        dotfile = fopen(dotfilename,"w");
        vset_dot(dotfile, result.win[1]);
        fclose(dotfile);
    }
#endif
    return result;
}


/**
 * \brief Computes and returns G = G - A.
 */
void spg_game_restrict(parity_game *g, vset_t a, const spgsolver_options* options)
{
    vset_minus(g->v, a); // compute V - A:
    for(int i=0; i<2; i++) {
        vset_minus(g->v_player[i], a); // compute V - A:
    }
    for(int i=g->min_priority; i<=g->max_priority; i++) {
        vset_minus(g->v_priority[i], a); // compute V - A:
        if (i==g->min_priority && i < g->max_priority && vset_is_empty(g->v_priority[i])) {
            g->min_priority++;
        }
    }
    while(g->max_priority > g->min_priority
            && vset_is_empty(g->v_priority[g->max_priority])) {
        g->max_priority--;
    }
    // TODO: compute E \intersects (V-A x V-A)? -- needs extension of the vset interface
    (void)options;
}


/**
 * \brief Computes the attractor set for G, U.
 * The resulting set is stored in U.
 */
void spg_attractor(int player, const parity_game* g, vset_t u, const spgsolver_options* options)
{
#ifdef LTSMIN_DEBUG
    char dotfilename[255];
    FILE* dotfile;
    if (options->dot)
    {
        sprintf(dotfilename, "spg_set_%05d_u.dot", dot_count++);
        dotfile = fopen(dotfilename,"w");
        vset_dot(dotfile, u);
        fclose(dotfile);
    }
#endif

    //char* swapfilename_u;
    //char* swapfilename_v_level;
    if (options->swap)
    {
        //swapfilename_u = spg_swapfilename();
        //swapfilename_v_level = spg_swapfilename();
    }
    //vdom_t domain = g->domain;
    vset_t v_level = vset_create(g->domain, -1, NULL);
    vset_copy(v_level, u);
    int l = 0;
    // Compute fixpoint
    while (!vset_is_empty(v_level)) {
        if (log_active(infoLong))
        {
            long   u_count;
            bn_int_t u_elem_count;
            vset_count(u, &u_count, &u_elem_count);
            bn_clear(&u_elem_count);
            long   level_count;
            bn_int_t level_elem_count;
            vset_count(v_level, &level_count, &level_elem_count);
            bn_clear(&level_elem_count);
            RTstopTimer(options->spg_solve_timer);
            Print(infoLong, "attr_%d^%d [%5.3f]: u has %ld nodes, v_level has %ld nodes.",
                    player, l, RTrealTime(options->spg_solve_timer), u_count, level_count);
            RTstartTimer(options->spg_solve_timer);
        }

        // swap U
        if (options->swap)
        {
            //vset_swap(u, swapfilename_u);
            //vset_clear(u);
        }

        // prev_attr = V \intersect prev(attr^k)
        vset_t prev_attr = vset_create(g->domain, -1, NULL);
        vset_t tmp = vset_create(g->domain, -1, NULL);
        for(int group=0; group<g->num_groups; group++) {
            vset_clear(tmp);
            vset_prev(tmp, v_level, g->e[group]);
            vset_intersect(tmp, g->v);
            vset_union(prev_attr, tmp);
        }
        vset_clear(tmp);

        // Compute V_player \intersects prev_attr
        vset_clear(v_level);
        vset_copy(v_level, prev_attr);
        vset_intersect(v_level, g->v_player[player]);

        // swap V_level
        if (options->swap)
        {
            //vset_swap(v_level, swapfilename_v_level);
            //vset_clear(v_level);
        }

        // B = V \intersect next(prev_attr)
        vset_t b = vset_create(g->domain, -1, NULL);
        for(int group=0; group<g->num_groups; group++) {
            vset_clear(tmp);
            vset_next(tmp, prev_attr, g->e[group]);
            vset_intersect(tmp, g->v);
            vset_union(b, tmp);
        }
        vset_clear(tmp);

        // unswap U
        if (options->swap)
        {
            //vset_unswap(u, swapfilename_u, g->domain);
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

        // unswap V_level
        if (options->swap)
        {
            //vset_unswap(v_level, swapfilename_v_level, g->domain);
        }

        vset_union(v_level, attr_other_player);
        vset_destroy(attr_other_player);

        // copy result:
        // U := U \union v_level
        // v_level := v_level - U
        vset_zip(u, v_level);

#ifdef LTSMIN_DEBUG
        if (options->dot)
        {
            sprintf(dotfilename, "spg_set_%05d_u_level_%d.dot", dot_count++, l);
            dotfile = fopen(dotfilename,"w");
            vset_dot(dotfile, u);
            fclose(dotfile);
        }
#endif

        l++;
    }
    Print(infoLong, "attr_%d: %d levels.", player, l);
    if (options->swap)
    {
        //free(swapfilename_u);
        //free(swapfilename_v_level);
    }
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
    long   u_count;
    bn_int_t u_elem_count;
    long   v_level_count;
    bn_int_t v_level_elem_count;

    // Compute fixpoint
    while (!vset_is_empty(v_level)) {
        if (log_active(infoLong))
        {
            vset_count(u, &u_count, &u_elem_count);
            bn_clear(&u_elem_count);
            vset_count(v_level, &v_level_count, &v_level_elem_count);
            bn_clear(&v_level_elem_count);
            RTstopTimer(options->spg_solve_timer);
            Print(infoLong, "attr_%d^%d [%5.3f]: u has %ld nodes, v_level has %ld nodes, v_group has %ld nodes max.",
                    player, l, RTrealTime(options->spg_solve_timer), u_count, v_level_count, peak_group_count);
            RTstartTimer(options->spg_solve_timer);
            peak_group_count = 0;
        }

        vset_copy(v_previous_level, v_level);
        vset_clear(v_level);
        for(int group=0; group<g->num_groups; group++) {
            vset_copy(v_group, v_previous_level);
            int k = 0;
            while ((options->saturation || k < 1) && !vset_is_empty(v_group)) {
                if (log_active(infoLong))
                {
                    vset_count(u, &u_count, &u_elem_count);
                    bn_clear(&u_elem_count);
                    vset_count(v_level, &v_level_count, &v_level_elem_count);
                    bn_clear(&v_level_elem_count);
                    long   v_group_count;
                    bn_int_t v_group_elem_count;
                    vset_count(v_group, &v_group_count, &v_group_elem_count);
                    bn_clear(&v_group_elem_count);
                    Print(infoLong, "  %d: u has %ld nodes, v_level has %ld nodes, v_group has %ld nodes.", k, u_count, v_level_count, v_group_count);
                    if (v_group_count > peak_group_count) {
                        peak_group_count = v_group_count;
                    }
                }

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
            Print(infoLong, "  group  %d: %d iterations.", group, k);
        }
        l++;
    }
    Print(infoLong, "attr_%d: %d levels.", player, l);
    vset_destroy(v_group);
    vset_destroy(v_level);
    vset_destroy(v_previous_level);
}
