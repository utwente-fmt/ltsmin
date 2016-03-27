#ifndef PINS2PINS_LTL
#define PINS2PINS_LTL

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
    PINS_LTL_NONE,
    PINS_LTL_TEXTBOOK,
    PINS_LTL_SPIN,
    PINS_LTL_LTSMIN
} pins_ltl_type_t;

/**
 * \brief boolean indicating whether PINS uses LTL
 */
extern pins_ltl_type_t PINS_LTL;

/**
\brief Add LTL layer on top all other pins layers
*/
extern model_t GBaddLTL(model_t model);

extern struct poptOption ltl_options[];


#endif // PINS2PINS_LTL
