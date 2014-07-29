/*
 * spg.h
 *
 *  Created on: 30 Oct 2012
 *      Author: kant
 */

#ifndef SPG_H_
#define SPG_H_

#include <stdbool.h>

#include <vset-lib/vector_set.h>

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
    int num_groups;
    vrel_t *e;
} parity_game;


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


#endif /* SPG_H_ */
