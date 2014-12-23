/*
 * \file spg-options.h
 */
#ifndef SPG_OPTIONS_H_
#define SPG_OPTIONS_H_

#include <stdbool.h>

#include <hre/runtime.h>
#include <spg-lib/spg.h>


typedef struct
{
    vdom_t dom;
    vset_t win[2];
    /* for storing strategy relation: */
    int strategy_count[2];
    int strategy_max[2];
    vrel_t* strategy[2];
    /* for storing strategy as sequence of level sets: */
    int strategy_levels_max[2]; // max number of sets per player
    int strategy_levels_count[2]; // number of sets per player
    vset_t* strategy_levels[2]; // level sets, computed by the attractor
    int strategy_boundary_count[2]; // number boundaries between separate attractor computations
    int* strategy_boundaries[2]; // boundaries between separate attractor computations
} recursive_result;


typedef struct
{
    bool dot;
#ifdef LTSMIN_DEBUG
    size_t dot_count;
#endif
    bool saturation;
    rt_timer_t timer;
    bool compute_strategy;
} spg_attr_options;

typedef void (*spg_attractor_t)(const int player, const parity_game* g,
                                recursive_result* result, vset_t u,
                                spg_attr_options* options, int depth);

typedef struct
{
    char* strategy_filename;
    bool check_strategy;
    bool interactive_strategy_play;
    int player;
    spg_attractor_t attr;
    spg_attr_options* attr_options;
} spgsolver_options;


extern struct poptOption spg_solve_options[];


/**
 * \brief Creates a new spgsolver_options object.
 */
spgsolver_options* spg_get_solver_options();


/**
 * \brief Destroy options object
 */
void spg_destroy_solver_options(spgsolver_options* options);



#endif /* SPG_OPTIONS_H_ */
