#ifndef PINS2PINS_CACHE
#define PINS2PINS_CACHE

extern struct poptOption cache_options[];

/**
\brief Add caching of grey box short calls.
*/
extern model_t GBaddCache(model_t model);

// default implementation of get_label_group_cached
extern void get_label_group_uncached_default (model_t model, sl_group_enum_t group, int *src, int *label, int *uncached);

#endif // PINS2PINS_CACHE
