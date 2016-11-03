/*
 * \file spg-solve.c
 *
 *  Created on: 23 Jan 2012
 *      Author: kant
 */
#include <limits.h>
#if HAVE_PROFILER
#include <gperftools/profiler.h>
#endif
#include <assert.h>

#include <hre/config.h>
#include <hre/user.h>
#include <bignum/bignum.h>
#include <util-lib/dfs-stack.h>
#include <util-lib/treedbs.h>
#include <spg-lib/spg-solve.h>


#define INIT_STRATEGY_MAX (64)


void print_vector(FILE* f, int* vector, int state_length) {
    fprintf(f, "[");
    for(int i=0; i < state_length; i++) {
        fprintf(f, "%s%d", ((i==0)?"":","), vector[i]);
    }
    fprintf(f, "]");
}


void print_state(FILE* f, int* vector, void* context) {
    int* length = (int*)context;
    fprintf(f, "state (%d) ", length[0]);
    print_vector(f, vector, length[0]);
    fprintf(f, "\n");
}


/**
 * \brief Creates an empty result.
 */
recursive_result recursive_result_create(vdom_t dom)
{
    recursive_result result;
    result.dom = dom;
    for(int p=0; p < 2; p++) {
        result.win[p] = vset_create(dom, -1, NULL);

        /* for storing strategy as sequence of level sets: */
        result.strategy_levels_max[p] = INIT_STRATEGY_MAX; // max number of sets per player
        result.strategy_levels_count[p] = 0; // number of sets per player
        result.strategy_levels[p] = RTmalloc((result.strategy_levels_max[p])*sizeof(vset_t)); // level sets, computed by the attractor
        result.strategy_boundary_count[p] = 1;
        result.strategy_boundaries[p] = RTmalloc(INIT_STRATEGY_MAX*sizeof(int)); // boundaries between separate attractor computations
        result.strategy_boundaries[p][0] = 0;
    }
    return result;
}


/**
 * \brief Adds level for player to strategy.
 */
void update_strategy_levels(recursive_result* result, int player, vset_t level)
{
    if (result->strategy_levels_count[player]==result->strategy_levels_max[player]) {
        result->strategy_levels_max[player] += INIT_STRATEGY_MAX;
        result->strategy_levels[player] = RTrealloc(result->strategy_levels[player], (result->strategy_levels_max[player])*sizeof(vset_t));
    }
    result->strategy_levels[player][result->strategy_levels_count[player]] = vset_create(result->dom, -1, NULL);
    vset_copy(result->strategy_levels[player][result->strategy_levels_count[player]], level);
    //printf("update_strategy_levels: player=%d, index=%d.\n", player, result->strategy_levels_count[player]);
    result->strategy_levels_count[player]++;
    result->strategy_boundaries[player][result->strategy_boundary_count[player]-1]++;
    //printf("update_strategy_levels: levels_count=%d, boundary[boundary_count-1=%d]=%d.\n",
    //       result->strategy_levels_count[player], result->strategy_boundary_count[player]-1,
    //       result->strategy_boundaries[player][result->strategy_boundary_count[player]-1]);
    assert(result->strategy_boundaries[player][result->strategy_boundary_count[player]-1] == result->strategy_levels_count[player]);
}


/**
 * \brief Combines strategy levels for player.
 */
void
concat_strategy_levels_player (int player, vdom_t domain, vset_t** dst,
                               int* dst_count, int** dst_boundaries,
                               int* dst_boundary_count, vset_t** src,
                               int* src_count, int** src_boundaries,
                               int* src_boundary_count)
{
    //printf("concat_strategy_levels: player=%d, dst_count=%d, src_count=%d, dst_boundary_count=%d, src_boundary_count=%d.\n",
    //       player, dst_count[player], src_count[player], dst_boundary_count[player], src_boundary_count[player]);
    dst[player] = RTrealloc(dst[player], (dst_count[player]+src_count[player])*sizeof(vset_t));
    for (int i = 0; i < src_count[player]; i++) {
        dst[player][dst_count[player] + i] = vset_create(domain, -1, NULL);
        vset_copy(dst[player][dst_count[player] + i], src[player][i]);
    }
    dst_boundaries[player] = RTrealloc(dst_boundaries[player], (dst_boundary_count[player]+src_boundary_count[player])*sizeof(int));
    for (int i = 0; i < src_boundary_count[player]; i++) {
        dst_boundaries[player][dst_boundary_count[player]+i] = src_boundaries[player][i] + dst_count[player];
    }
    dst_count[player] += src_count[player];
    dst_boundary_count[player] += src_boundary_count[player];
    //printf("concat_strategy_levels: player=%d, dst_count=%d, dst_boundary_count=%d.\n",
    //       player, dst_count[player], dst_boundary_count[player]);
}


/**
 * \brief Combines strategy levels for both players.
 */
void
concat_strategy_levels(vdom_t domain, vset_t** dst, int* dst_count,int** dst_boundaries,
                       int* dst_boundary_count, vset_t** src, int* src_count,
                       int** src_boundaries, int* src_boundary_count)
{
    for (int player=0; player < 2; player++) {
        concat_strategy_levels_player(player, domain, dst, dst_count, dst_boundaries, dst_boundary_count,
                                    src, src_count, src_boundaries, src_boundary_count);
    }
}


/**
 * \brief Destroys vrels in the result and frees dynamically allocated arrays.
 */
void recursive_result_destroy(recursive_result result) {
    for (int p=0; p < 2; p++) {
        for (int i=0; i < result.strategy_levels_count[p]; i++) {
            vset_destroy(result.strategy_levels[p][i]);
        }
        RTfree(result.strategy_boundaries[p]);
        RTfree(result.strategy_levels[p]);
    }
}


/**
 * \brief Writes the symbolic result to a file.
 */
void result_save(FILE* f, const recursive_result r) {
    fprintf(f,"symbolic result\n");
    vset_pre_save(f, r.dom);
    vdom_save(f, r.dom);
    for (int p=0; p < 2; p++) {
        fprintf(f,"set win[%d]\n", p);
        vset_save(f, r.win[p]);
    }
    for (int p=0; p < 2; p++) {
        fprintf(f,"strategy_levels_count[%d]=%d\n", p, r.strategy_levels_count[p]);
        for(int i=0; i < r.strategy_levels_count[p]; i++) {
            fprintf(f,"set strategy_levels[%d]\n", i);
            vset_save(f, r.strategy_levels[p][i]);
        }
        fprintf(f,"strategy_boundary_count[%d]=%d\n", p, r.strategy_boundary_count[p]);
        for(int i=0; i < r.strategy_boundary_count[p]; i++) {
            fprintf(f,"strategy_boundaries[%d]=%d\n", i, r.strategy_boundaries[p][i]);
        }
    }
    vset_post_save(f, r.dom);
}


/**
 * \brief Reads a symbolic result from file.
 */
recursive_result result_load(FILE* f, vset_implementation_t impl, vdom_t dom) {
    size_t size = 1024;
    char* buf = malloc(sizeof(char)*size);
    ssize_t res = getline(&buf, &size, f); // "symbolic result\n" fscanf(f,"symbolic result\n");
    if (res < 0) Abort("error reading symbolic result.");
    //res &= getline(&buf, &size, f); // "\n"
    //if (res <= 0) {
    //    Abort("Wrong file format: %d.", res)
    //}
    Print(infoShort, "Loading symbolic strategy.");

    vdom_t domain = vdom_create_domain_from_file(f, impl);
    if (domain==NULL)
    {
        domain = dom;
    }
    if (domain==NULL)
    {
        Abort("Domain cannot be loaded.");
    }
    vset_pre_load(f, domain);
    recursive_result result = recursive_result_create(domain);
    for(int i=0; i<2; i++) {
        res = getline(&buf, &size, f); // "set win[%d]\n"
        if (res < 0) Abort("error reading winning set.");
        result.win[i] = vset_load(f, domain);
    }
    for (int p=0; p < 2; p++) {
        int player;
        res = fscanf(f,"strategy_levels_count[%d]=%d\n", &player, &(result.strategy_levels_count[p]));
        if (res < 0) Abort("error reading strategy levels count.");
        if (player != p) {
            Abort("Invalid strategy_levels_count in strategy file.");
        }
        result.strategy_levels[p] = RTrealloc(result.strategy_levels[p], (result.strategy_levels_count[p])*sizeof(vset_t));
        for(int i=0; i < result.strategy_levels_count[p]; i++) {
            res = getline(&buf, &size, f); // "set strategy_levels[%d]\n"
            if (res < 0) Abort("error reading strategy levels.");
            result.strategy_levels[p][i] = vset_load(f, domain);
        }
        res = fscanf(f,"strategy_boundary_count[%d]=%d\n", &player, &(result.strategy_boundary_count[p]));
        if (res < 0) Abort("error reading boundary count.");
        if (player != p) {
            Abort("Invalid strategy_boundary_count in strategy file.");
        }
        result.strategy_boundaries[p] = RTrealloc(result.strategy_boundaries[p], (result.strategy_boundary_count[p])*sizeof(int));
        for(int i=0; i < result.strategy_boundary_count[p]; i++) {
            res = fscanf(f,"strategy_boundaries[%d]=%d\n", &player, &(result.strategy_boundaries[p][i]));
            if (res < 0) Abort("error reading boundary.");
        }
    }
    Print(infoShort, "Done loading symbolic strategy.");
    vset_post_load(f, domain);
    free(buf);
    return result;
}


int get_priority(const parity_game* g, const int* s)
{
    int priority = -1;
    bool found = false;
    for (priority=g->min_priority; priority <= g->max_priority; priority++)
    {
        if (vset_member(g->v_priority[priority], s)) { found = true; break; }
    }
    assert(found); (void) found;
    return priority;
}


/**
 * Computes the next level from src according to the strategy in result.
 */
static inline void compute_strategy_level(vset_t strategy_level, const int* src, const int player, const recursive_result* strategy) {
    // Initialise strategy level to the empty set
    vset_clear(strategy_level);
    // Find the right strategy level. For each level that contains src, there is either a transition
    // to the same level, or (preferrably) to a lower level.
    for (int i=0; i < strategy->strategy_boundary_count[player]; i++) {
        int begin = (i==0) ? 0 : strategy->strategy_boundaries[player][i-1];
        int end = strategy->strategy_boundaries[player][i];
        //printf("considering segment %d (begin=%d, end=%d).\n", i, begin, end);
        //assert(first <= last);
        assert(strategy->strategy_boundaries[player][i]<=strategy->strategy_levels_count[player]);
        bool src_found = false;
        for (int j = begin; j < end && !src_found; j++) {
            if (vset_member(strategy->strategy_levels[player][j], src)) {
                if (j==begin) {
                    vset_union(strategy_level, strategy->strategy_levels[player][j]);
                    //printf("choosing move from segment %d, moving from level %d to level %d.\n", i, j, j);
                } else {
                    vset_union(strategy_level, strategy->strategy_levels[player][j-1]);
                    //printf("choosing move from segment %d, moving from level %d to level %d.\n", i, j, j-1);
                }
                assert(!vset_is_empty(strategy_level));
                src_found = true;
            }
        }
    }
    // strategy_level now contains any target states that can be reached from src
    // in one step and are conform the strategy.
}


int choose_strategy_move(const parity_game* g, const recursive_result* strategy, const int player, const int* src,
                          int* current_player, bool* strategy_play, bool* result, bool* deadlock, int* dst)
{
    int group = -1;
    vset_t level = vset_create(g->domain, -1, NULL);
    vset_t tmp = vset_create(g->domain, -1, NULL);
    vset_t singleton = vset_create(g->domain, -1, NULL);
    vset_add(singleton, src);
    // Do a random run through the game, conforming to
    // the strategy in result.
    *current_player = (vset_member(g->v_player[player], src) ? player : 1-player);
    Print(hre_debug, "Using strategy of player %d. The position is owned by player %d.", player, *current_player);
    if (*current_player==player && vset_member(strategy->win[player], src))
    {
        //printf("Player %d chooses according to his/her winning strategy.\n", player);
        // Choose a transition according to the strategy of player
        *strategy_play = true;
        vset_t strategy_level = vset_create(g->domain, -1, NULL);
        compute_strategy_level(strategy_level, src, player, strategy);

        vset_clear(level);
        for (int i=0; i < g->num_groups; i++) {
            vset_next(tmp, singleton, g->e[i]);
            vset_intersect(tmp, strategy_level);
            if (!vset_is_empty(tmp))
            {
                vset_copy(level, tmp);
                // Choose a random element:
                //printf("Choosing a transition from transition group %d.\n", i);
                group = i;
                vset_random(level, dst);
                break;
            }
        }
        // Check for deadlocks
        if (vset_is_empty(level)) {
            *deadlock = true;
            *result = (*current_player != player);
            Print(infoLong, "Deadlock for player %d, player %s has won, result is %s.",
                   *current_player,
                   (*current_player==1) ? "0 (even / or)" : "1 (odd / and)",
                           *result ? "true" : "false");
        }
    }
    else
    {
        //Print(info, "Not player %d's turn or player %d has no winning strategy. Choosing an arbitrary move for player %d.",
        //       player, player, *current_player);
        // this states belongs to (1-player) or player does not have a winning strategy
        *strategy_play = false;
        vset_t strategy_level = vset_create(g->domain, -1, NULL);
        /*
        if (vset_member(result.win[current_player], src))
        {
            // winning move after all
            compute_strategy_level(strategy_level, src, current_player, result);
            if (vset_is_empty(strategy_level)) {
                Print(info, "Unexpected: src is in win[%d], but strategy_level is empty.", current_player);
            } else {
                strategy_play = true;
            }
        }
        */
        // choose a random move
        vset_clear(level);
        for (int i=0; i < g->num_groups; i++) {
            vset_next(tmp, singleton, g->e[i]);
            if (*strategy_play && !vset_is_empty(tmp)) {
                vset_intersect(tmp, strategy_level);
                if (!vset_is_empty(tmp))
                {
                    vset_copy(level, tmp);
                    // Choose a random element:
                    //printf("Choosing a transition from transition group %d.\n", i);
                    group = i;
                    vset_random(level, dst);
                    break;
                }
            } else {
                vset_union(level, tmp);
            }
        }
        // Check for deadlocks
        if (vset_is_empty(level)) {
            *deadlock = true;
            *result = (*current_player != player);
            Print(infoLong, "Deadlock for player %d, player %s has won, result is %s.",
                   *current_player,
                   (*current_player==1) ? "0 (even / or)" : "1 (odd / and)",
                           *result ? "true" : "false");
        } else if (!*strategy_play) {
            //Print(info, "choose randomly");
            vset_random(level, dst);
        }
    }
    return group;
}


/**
 * \brief Plays a random play starting in the initial state according to the strategy of player.
 * If player wins, returns true, else returns false.
 */
bool random_strategy_play(const parity_game* g, const recursive_result* strategy, const int player) {
    treedbs_t tree = TreeDBScreate(g->state_length);
    int initial_state = TreeFold(tree, g->src);
    int priority = get_priority(g, g->src);
    int src[g->state_length];
    int dst[g->state_length];
    // start from the initial state of g
    int s = initial_state;
    int t = s;

    int l = 0; // how many moves have been played
    dfs_stack_t states = dfs_stack_create(1);
    dfs_stack_t priorities = dfs_stack_create(1);
    dfs_stack_push(states, (int[]){s});
    dfs_stack_push(priorities, (int[]){priority});

    int current_player;
    bool strategy_play;
    bool deadlock = false;
    bool cycle = false;
    bool result; // does player even/or/0 win?
    size_t cycle_begin = 0;

    srand(time(NULL));

    while (!(cycle || deadlock))
    {
        TreeUnfold(tree, s, src);

        choose_strategy_move(g, strategy, player, src,
                                  &current_player, &strategy_play, &result, &deadlock, dst);

        // store states on a stack (use tree compression,
        // store the state on one, priority on another stack),
        // when a cycle is detected, check the lowest priority
        // if even, this was indeed a winning play.
        if (!deadlock)
        {
            t = TreeFold(tree, dst);
            priority = get_priority(g, dst);

            if (log_active(infoLong))
            {
                FILE* f = stderr;
                print_vector(f, src, g->state_length);
                fprintf(f, " (%3d) --> ", s);
                print_vector(f, dst, g->state_length);
                fprintf(f, " (%3d)", t);
                fprintf(f, " <priority=%d, player=%d, %s>\n",
                        priority, current_player, (strategy_play ? "strategy" : "random"));
            }

            for(size_t i=0; i < dfs_stack_size(states); i++) {
                int* e = dfs_stack_peek(states, i);
                if (e[0] == t) { cycle = true; cycle_begin = i; break; }
            }

            if (!cycle)
            {
                dfs_stack_push(states, (int[]){t});
                dfs_stack_push(priorities, (int[]){priority});
                s = t;
            }
        }
        l++;
    }
    if (cycle)
    {
        Print(infoLong, "Cycle found!");
        // Looking for the least priority on the cycle:
        int min_priority = g->max_priority;
        for(size_t i=cycle_begin; i < dfs_stack_size(priorities); i++) {
            int* e = dfs_stack_peek(priorities, i);
            if (e[0] < min_priority) { min_priority = e[0]; }
        }

        result = (min_priority % 2 == player);
        Print(infoLong, "min priority is %d, player %s has won, result is %s.",
               min_priority,
               (min_priority % 2 == 0) ? "0 (even / or)" : "1 (odd / and)",
                       result ? "true" : "false");
    }
    return result;
}


void play_strategy_interactive(const parity_game* g, const recursive_result* strategy, const int player)
{
    srand(time(NULL));
    int src[g->state_length];
    bool result;
    bool deadlock = false;
    bool strategy_play;
    int current_player;
    int dst[g->state_length];
    int res = -1;
    bool error = false;
    bool done = false;

    while (!done)
    {
        Print(infoLong, "Read state...");
        error = false;
        size_t size = 1024;
        char* s = malloc(size*sizeof(char));
        ssize_t bytes = getline(&s, &size, stdin);
        if (bytes == -1) {
            //Print(info, "EOF (%d)", bytes);
            error = true;
            done = true;
        } else {
            int pos = 0;
            //Print(info, "%d bytes read. Reading values...", bytes);
            for (int i=0; i < g->state_length; i++) {
                if (pos < bytes-1) {
                    int b = -1;
                    if (i < g->state_length - 1) {
                        res = sscanf(s + pos, "%d,%n", &(src[i]), &b);
                    } else {
                        res = sscanf(s + pos, "%d %n", &(src[i]), &b);
                    }
                    //Print(info, "Read %d bytes", b);
                    pos += b;
                    if (res == 1) {
                        //Print(info, "Read value: %d", src[i]);
                    } else {
                        Print(info, "Error reading state.");
                        error = true;
                        break;
                    }
                } else {
                    Print(info, "Reached end of input. Not a valid state vector.");
                    error = true;
                    break;
                }
            }
        }
        if (!error)
        {
            Print(infoLong, "Read: ");
            if (log_active(infoLong))
            {
                print_vector(stderr, src, g->state_length);
                fprintf(stderr, "\n");
                fflush(stderr);
            }
            if (!vset_member(g->v, src))
            {
                Print(info, "State is not in the set of reachable states.");
            }
            else
            {
                Print(infoLong, "From this state, player %d has a winning strategy.", vset_member(strategy->win[0], src) ? 0 : 1);
                int group = choose_strategy_move(g, strategy, player, src,
                                                  &current_player, &strategy_play, &result, &deadlock, dst);
                if (!deadlock) {
                    Print(infoLong, "Player %d moves %s, group %d:", current_player,
                          strategy_play ? "according to strategy" : "randomly", group);
                    print_vector(stdout, dst, g->state_length);
                    printf("\n");
                    fflush(stdout);
                }
            }
        }
    }
}


void check_strategy(const parity_game* g, const recursive_result* strategy, const int player, const bool result, const int n)
{
    /*
    for (int p=0; p < 2; p++) {
        for (int i=0; i < strategy->strategy_boundary_count[p]; i++)
        {
            Print(info, "strategy: player %d, boundary %d: %d", p, i, strategy->strategy_boundaries[p][i]);
        }
    }
    int p = result ? player : 1-player;
    bool random_run = random_strategy_play(g, strategy, p);
    assert(random_run);
    */
    for (int p=0; p < 2; p++) {
        for(int i=0; i < n; i++) {
            Print(infoLong, "Player %d, random run %d", p, i);
            bool random_run = random_strategy_play(g, strategy, p);
            if ((p == player && result) || (p != player && !result)) {
                assert(random_run); (void) random_run;
            }
        }
    }
    Print(info, "Test succeeded.");
}

