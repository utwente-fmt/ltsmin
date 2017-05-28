#ifndef ALG_REACH_H
#define ALG_REACH_H

#include <pins-lib/pins.h>
#include <vset-lib/vector_set.h>

typedef struct expand_info {
    int group;
    vset_t group_explored;
    long *eg_count;
} expand_info_t;

extern void save_level(vset_t visited);

extern void expand_group_next(int group, vset_t set);

extern void expand_group_next_projected(vrel_t rel, vset_t set, void *context);

extern void learn_guards_reduce(vset_t true_states, int t, long *guard_count,
                                vset_t *guard_maybe, vset_t false_states,
                                vset_t maybe_states, vset_t tmp);

extern void reduce(int group, vset_t set);

extern void eval_label(int label, vset_t set);

extern void learn_guards(vset_t states, long *guard_count);

extern void learn_guards_par(vset_t states, long *guard_count);

extern void learn_labels(vset_t states);

extern void learn_labels_par(vset_t states);

extern void add_step (bool backward, vset_t addto, vset_t from, vset_t universe);

/**
 * Tree structure to evaluate the condition of a transition group.
 * If we disable the soundness check of guard-splitting then if we
 * have MAYBE_AND_FALSE_IS_FALSE (like mCRL(2) and SCOOP) then
 * (maybe && false == false) or (false && maybe == false) is not checked.
 * If we have !MAYBE_AND_FALSE_IS_FALSE (like Java, Promela and DVE) then only
 * (maybe && false == false) is not checked.
 * For guard-splitting ternary logic is used; i.e. (false,true,maybe) = (0,1,2) = (0,1,?).
 * Truth table for MAYBE_AND_FALSE_IS_FALSE:
 *      0 1 ?
 *      -----
 *  0 | 0 0 0
 *  1 | 0 1 ?
 *  ? | 0 ? ?
 * Truth table for !MAYBE_AND_FALSE_IS_FALSE:
 *      0 1 ?
 *      -----
 *  0 | 0 0 0
 *  1 | 0 1 ?
 *  ? | ? ? ?
 *
 *  Note that if a guard evaluates to maybe then we add it to both guard_false and guard_true, i.e. F \cap T != \emptyset.
 *  Soundness check: vset_is_empty(root(reach_red_s)->true_container \cap root(reach_red_s)->false_container) holds.
 *  Algorithm to carry all maybe states to the root:
 *  \bigcap X = Y \cap Z = (Fy,Ty) \cap (Fz,Tz):
 *   - T = (Ty \cap Tz) U M
 *   - F = Fy U Fz U M
 *   - M = MAYBE_AND_FALSE_IS_FALSE  => ((Fy \cap Ty) \ Fz) U ((Fz \cap Tz) \ Fy) &&
 *         !MAYBE_AND_FALSE_IS_FALSE => (Fy \cap Ty) U ((Fz \cap Tz) \ Fy)
 */
typedef struct reach_red_s
{
    vset_t true_container;
    vset_t false_container;
    vset_t left_maybe; // temporary vset so that we don't have to create/destroy at each level
    vset_t right_maybe; // temporary vset so that we don't have to create/destroy at each level
    struct reach_red_s *left;
    struct reach_red_s *right;
    int index; // which guard
    int group; // which transition group
} reach_red_t;

extern struct reach_red_s *reach_red_prepare(size_t left, size_t right, int group);

extern void reach_red_destroy(struct reach_red_s *s);

typedef struct reach_s
{
    vset_t container;
    vset_t deadlocks; // only used if dlk_detect
    vset_t ancestors;
    struct reach_s *left;
    struct reach_s *right;
    int index;
    int class;
    int next_count;
    int eg_count;
    struct reach_red_s *red;
    int unsound_group;
} reach_t;

extern struct reach_s *reach_prepare(size_t left, size_t right);

extern void reach_destroy(struct reach_s *s);


#endif //ALG_REACH_H
