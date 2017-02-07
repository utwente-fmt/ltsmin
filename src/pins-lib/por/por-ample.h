#ifndef AMPLE_POR
#define AMPLE_POR


#include <pins-lib/por/pins2pins-por.h>


typedef struct ample_s ample_t;

typedef struct process_s {
    int                 id;
    char               *name;
    int                 pc_slot;
    ci_list            *groups;

    ci_list            *en;
    ci_list            *succs;
    bool                visible;
    size_t              conflicts;
} process_t;

extern ample_t     *ample_create_context (por_context *ctx, bool all);

extern bool         ample_is_stubborn (por_context *ctx, int group);

extern int          ample_search_all (model_t self, int *src, TransitionCB cb,
                                      void *user_context);

extern int          ample_search_one (model_t self, int *src, TransitionCB cb,
                                      void *user_context);

extern process_t   *identify_procs (por_context *por, size_t *num_procs,
                                    int *group2proc);

#endif
