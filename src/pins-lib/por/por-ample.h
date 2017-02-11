#ifndef AMPLE_POR
#define AMPLE_POR

#include <stdbool.h>

#include <pins-lib/por/pins2pins-por.h>
#include <util-lib/fast_set.h>

typedef struct ample_s ample_t;

typedef struct __attribute__((packed)) process_s {
    char               *name;
    int                 id;
    int                 pc_slot;
    ci_list            *groups;

    ci_list            *en;
    ci_list            *succs;
    size_t              conflicts;
    size_t              depth;
    fset_t             *fset;           // for detecting non-progress
    bms_t              *transaction;
    bool                visible;
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
