#ifndef PINS2PINS_GROUP
#define PINS2PINS_GROUP

extern int      PINS_USE_GUARDS;

/**
\brief Reorder and regroup transitions and state vectors
*/
extern model_t GBregroup(model_t model);

extern struct poptOption group_options[];


#endif // PINS2PINS_GROUP
