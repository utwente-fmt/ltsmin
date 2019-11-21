/*
 * pins2pins-mucalc.h
 *
 *  Created on: 23 Nov 2012
 *      Author: kant
 */

#ifndef PINS2PINS_MUCALC_H_
#define PINS2PINS_MUCALC_H_


extern struct poptOption mucalc_options[];

/**
\brief Add mu-calculus layer
*/
extern model_t GBaddMucalc (model_t model);

/**
 * \brief Returns 1 if the mucalc wrapper is active; 0 otherwise.
 */
extern int GBhaveMucalc();

/**
 * \brief Gets the number of subformulae of the mu-calculus property that is being checked.
 * Needed for the parity game solver.
 */
extern int GBgetMucalcNodeCount();

#endif /* PINS2PINS_MUCALC_H_ */
