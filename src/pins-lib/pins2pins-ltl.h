#ifndef PINS2PINS_LTL
#define PINS2PINS_LTL

#include <pins-lib/pins.h>


/**
\brief The behaviour of the ltl buchi product

PINS_LTL_TEXTBOOK adds an initial state to the model and labels
the incoming edges with the properties of in the buchi automaton
PINS_LTL_SPIN labels the outgoing edges with the properties of
the buchi automaton. Additionally, the SPIN semantics accounts
for deadlocks in the LTS by letting the buchi continues upon deadlock.
PINS_LTL_LTSMIN Like SPIN semantics, but without the deadlock provision.
This allows LTSmin to maintain an efficient dependency matrix as
deadlock detection is non-local (it depends on the conjunction of all
guards from all transition groups).
*/
typedef enum {
    PINS_LTL_AUTO,
    PINS_LTL_TEXTBOOK,
    PINS_LTL_SPIN,
    PINS_LTL_LTSMIN
} pins_ltl_type_t;

/**
 * \brief boolean indicating whether PINS uses LTL
 */
extern pins_ltl_type_t PINS_LTL;

/**
\brief The type of the Buchi automaton

PINS_BUCHI_TYPE_BA refers to the (state-based) Buchi Automaton obtained
from ltl2ba.
PINS_BUCHI_TYPE_TGBA refers to the Transition Based Generalized Buchi
Automaton obtained from Spot (via ltl2spot).
PINS_BUCHI_TYPE_SPOTBA refers to the (state-based) Buchi Automaton
obtained from Spot (via ltl2spot).
*/
typedef enum {
    PINS_BUCHI_TYPE_BA,
    PINS_BUCHI_TYPE_TGBA,
    PINS_BUCHI_TYPE_SPOTBA,
    PINS_BUCHI_TYPE_MONITOR,
} pins_buchi_type_t;

/**
 * \brief buchi type for the LTL automaton
 */
extern pins_buchi_type_t PINS_BUCHI_TYPE;

/**
\brief Add LTL layer on top all other pins layers
*/
extern model_t GBaddLTL(model_t model);

extern uint32_t GBgetAcceptingSet ();

extern struct poptOption ltl_options[];


#endif // PINS2PINS_LTL
