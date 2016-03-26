/*
 * pins2pins-mucalc.h
 *
 *  Created on: 23 Nov 2012
 *      Author: kant
 */

#ifndef PINS2PINS_MUCALC_H_
#define PINS2PINS_MUCALC_H_

/**
\brief Add mu-calculus layer
*/
extern model_t GBaddMucalc (model_t model, const char *mucalc_file);

/**
 * \brief Returns 1 if the mucalc wrapper is active; 0 otherwise.
 */
extern int GBhaveMucalc();

/**
 * \brief Gets the number of subformulae of the mu-calculus property that is being checked.
 * Needed for the parity game solver.
 */
extern int GBgetMucalcNodeCount(model_t model);

/**
 * \brief Sets the number of subformulae of the mu-calculus property that is being checked.
 * Needed for the parity game solver.
 */
extern void GBsetMucalcNodeCount(model_t model, int node_count);


#endif /* PINS2PINS_MUCALC_H_ */
