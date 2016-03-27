#ifndef PINS2PINS_MUTEX
#define PINS2PINS_MUTEX

extern int PINS_REQUIRE_MUTEX_WRAPPER;

/**
\brief Add mutex wrapper (for non thread-safe PINS models)
*/
extern model_t GBaddMutex(model_t model);

#endif // PINS2PINS_MUTEX
