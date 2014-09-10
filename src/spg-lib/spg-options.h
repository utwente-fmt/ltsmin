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
    bool dot;
#ifdef LTSMIN_DEBUG
    size_t dot_count;
#endif
    bool saturation;
    rt_timer_t timer;
} spg_attr_options;


typedef void (*spg_attractor_t)(int player, const parity_game* g, vset_t u, const spg_attr_options* options, int depth);


typedef struct
{
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
