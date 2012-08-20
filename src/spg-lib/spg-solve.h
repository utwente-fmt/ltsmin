/*
 * \file pg-solve.h
 *
 *  Created on: 23 Jan 2012
 *      Author: kant
 */
#ifndef SPG_SOLVE_H_
#define SPG_SOLVE_H_

#include <stdbool.h>

#include <hre/runtime.h>
#include <vset-lib/vector_set.h>

typedef struct
{
    vset_t win[2];
} recursive_result;

typedef struct tmp_file_s {
    int                 fd;
    char                buffer[256];
} tmp_file_t;

typedef struct
{
    vdom_t domain;
    int state_length;
    int *src;
    vset_t v;
    vset_t v_player[2];
    int min_priority;
    int max_priority;
    vset_t *v_priority;
    tmp_file_t** v_priority_swapfile;
    int num_groups;
    vrel_t *e;
} parity_game;


typedef struct
{
    bool swap;
    bool dot;
    bool chaining;
    bool saturation;
    rt_timer_t spg_solve_timer;
} spgsolver_options;


extern struct poptOption spg_solve_options[];


/**
 * \brief Creates an empty game.
 */
parity_game* spg_create(const vdom_t domain, int state_length, int num_groups, int min_priority, int max_priority);


/**
 * \brief Destroys vsets in the parity game and frees dynamically allocated arrays.
 */
void spg_destroy(parity_game* g);


/**
 * \brief Writes the symbolic parity game to a file.
 */
void spg_save(FILE* f, parity_game* g);


/**
 * \brief Reads a symbolic parity game from file.
 */
parity_game* spg_load(FILE* f, vset_implementation_t impl);

/**
 * \brief Creates a deep copy of g.
 */
parity_game* spg_copy(const parity_game* g);


/**
 * \brief Creates a new spgsolver_options object.
 */
spgsolver_options* spg_get_solver_options();


/**
 * \brief Destroy options object
 */
void spg_destroy_solver_options(spgsolver_options* options);


/**
 * \brief
 */
bool spg_solve(parity_game* g, spgsolver_options* options);


/**
 * \brief Solves the parity game encoded in node set V and the partitioned edge set
 * E.
 * \param v
 * \return true iff ...
 */
recursive_result spg_solve_recursive(parity_game* g, const spgsolver_options* options);


/**
 * \brief
 */
void spg_game_restrict(parity_game *g, vset_t a, const spgsolver_options* options);


/**
 * \brief Computes attractor set
 */
void spg_attractor(int player, const parity_game* g, vset_t u, const spgsolver_options* options);


/**
 * \brief Computes attractor set
 */
void spg_attractor_chaining(int player, const parity_game* g, vset_t u, const spgsolver_options* options);


#endif /* SPG_SOLVE_H_ */
