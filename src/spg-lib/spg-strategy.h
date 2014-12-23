/*
 * \file spg-strategy.h
 *
 *  Created on: 23 Jan 2012
 *      Author: kant
 */
#ifndef SPG_STRATEGY_H_
#define SPG_STRATEGY_H_

#include <stdbool.h>

#include <hre/runtime.h>
#include <spg-lib/spg.h>
#include <spg-lib/spg-attr.h>
#include <spg-lib/spg-options.h>


/**
 * \brief Creates an empty result.
 */
recursive_result recursive_result_create(vdom_t dom);


/**
 * \brief Adds level for player to strategy.
 */
void update_strategy_levels(recursive_result* result, int player, vset_t level);


/**
 * \brief Combines strategy levels for player.
 */
void concat_strategy_levels(vdom_t domain, vset_t** dst, int* dst_count,
                            int** dst_boundaries, int* dst_boundary_count,
                            vset_t** src, int* src_count, int** src_boundaries,
                            int* src_boundary_count);

/**
 * \brief TODO
 */
extern void
concat_strategy_levels_player (int player, vdom_t domain, vset_t** dst,
                               int* dst_count, int** dst_boundaries,
                               int* dst_boundary_count, vset_t** src,
                               int* src_count, int** src_boundaries,
                               int* src_boundary_count);

/**
 * \brief Destroys vrels in the result and frees dynamically allocated arrays.
 */
void recursive_result_destroy(recursive_result result);


/**
 * \brief Writes the symbolic result to a file.
 */
void result_save(FILE* f, const recursive_result r);


/**
 * \brief Reads a symbolic result from file.
 */
recursive_result result_load(FILE* f, vset_implementation_t impl, vdom_t dom);


/**
 * \brief Randomly play on game g according to the strategy in result.
 */
bool random_strategy_play(const parity_game* g, const recursive_result* result, const int player);


void play_strategy_interactive(const parity_game* g, const recursive_result* strategy, const int player);


void check_strategy(const parity_game* g, const recursive_result* strategy, const int player, const bool result, const int n);


#endif /* SPG_STRATEGY_H_ */
