/*
 * \file spg-attr.h
 */
#ifndef SPG_ATTR_H_
#define SPG_ATTR_H_

#include <hre/runtime.h>
#include <spg-lib/spg.h>
#include <spg-lib/spg-options.h>
#ifdef HAVE_SYLVAN
#include <sylvan.h>
#endif

/**
 * \brief Computes attractor set
 */
void spg_attractor(const int player, const parity_game* g, recursive_result* result,
                   vset_t u, spg_attr_options* options, int depth);


#ifdef HAVE_SYLVAN

/**
 * \brief Computes attractor set
 */
void spg_attractor_par(const int player, const parity_game* g, recursive_result* result,
                       vset_t u, spg_attr_options* options, int depth);


/**
 * \brief Computes attractor set
 */
void spg_attractor_par2(const int player, const parity_game* g, recursive_result* result,
                        vset_t u, spg_attr_options* options, int depth);

#endif


/**
 * \brief Computes attractor set
 */
void spg_attractor_chaining(const int player, const parity_game* g, recursive_result* result,
                            vset_t u, spg_attr_options* options, int depth);

#define SPG_ATTR_REPORT_LEVEL(INDENT,OPT,PLAYER,U,V_LEVEL,LEVEL) {                              \
    if (log_active(infoLong))                                                                   \
    {                                                                                           \
        long   u_count;                                                                         \
        bn_int_t u_elem_count;                                                                  \
        long   level_count;                                                                     \
        bn_int_t level_elem_count;                                                              \
        vset_count(U, &u_count, &u_elem_count);                                                 \
        vset_count(V_LEVEL, &level_count, &level_elem_count);                                   \
        RTstopTimer(OPT->timer);                                                                \
        Print(infoLong, "[%7.3f] " "%*s" "attr_%d^%d: u has %ld nodes, v_level has %ld nodes.",   \
        		RTrealTime(OPT->timer), INDENT, "", PLAYER, LEVEL, u_count, level_count);       \
        RTstartTimer(OPT->timer);                                                               \
        bn_clear(&u_elem_count);                                                                \
        bn_clear(&level_elem_count);                                                            \
    }                                                                                           \
    else                                                                                        \
    {                                                                                           \
        Print(info, "[%7.3f] " "%*s" "attr_%d: level %d.",                                    \
                RTrealTime(OPT->timer), INDENT, "", PLAYER, LEVEL);                             \
    }                                                                                           \
}

#define SPG_ATTR_REPORT_SET(INDENT,OPT,PLAYER,SET,LEVEL) {                                      \
    if (log_active(infoLong))                                                                   \
    {                                                                                           \
        RTstopTimer(OPT->timer);                                                                \
        Print(infoLong, "[%7.3f] " "%*s" "attr_%d^%d: counting " #SET ".",                   \
        		RTrealTime(OPT->timer), INDENT, "", PLAYER, LEVEL);                             \
        long   s_count;                                                                         \
        bn_int_t s_elem_count;                                                                  \
        vset_count(SET, &s_count, &s_elem_count);                                               \
        Print(infoLong, "[%7.3f] " "%*s" "attr_%d^%d: " #SET " has %ld nodes.",              \
        		RTrealTime(OPT->timer), INDENT, "", PLAYER, LEVEL, s_count);                    \
        RTstartTimer(OPT->timer);                                                               \
        bn_clear(&s_elem_count);                                                                \
    }                                                                                           \
}

#define SPG_ATTR_REPORT_ELEMENTS(INDENT,OPT,PLAYER,SET,LEVEL) {                                 \
    if (log_active(infoLong))                                                                   \
    {                                                                                           \
        long   s_count;                                                                         \
        bn_int_t s_elem_count;                                                                  \
        vset_count(SET, &s_count, &s_elem_count);                                               \
        char s[1024];                                                                           \
        bn_int2string(s, sizeof(s), &s_elem_count);                                             \
        RTstopTimer(OPT->timer);                                                                \
        Print(infoLong, "[%7.3f] " "%*s" "attr_%d^%d: " #SET " has %s elements (%ld nodes).",  \
                RTrealTime(OPT->timer), INDENT, "", PLAYER, LEVEL, s, s_count);                 \
        RTstartTimer(OPT->timer);                                                               \
        bn_clear(&s_elem_count);                                                                \
    }                                                                                           \
}

#define SPG_ATTR_REPORT_REL(INDENT,OPT,PLAYER,REL,LEVEL) {                                      \
    if (log_active(infoLong))                                                                   \
    {                                                                                           \
        RTstopTimer(OPT->timer);                                                                \
        Print(infoLong, "[%7.3f] " "%*s" "attr_%d^%d: counting rel " #REL ".",               \
        		RTrealTime(OPT->timer), INDENT, "", PLAYER, LEVEL);                             \
        long   s_count;                                                                         \
        bn_int_t s_elem_count;                                                                  \
        vrel_count(REL, &s_count, &s_elem_count);                                               \
        Print(infoLong, "[%7.3f] " "%*s" "attr_%d^%d: " #REL " has %ld nodes.",              \
        		RTrealTime(OPT->timer), INDENT, "", PLAYER, LEVEL, s_count);                    \
        RTstartTimer(OPT->timer);                                                               \
        bn_clear(&s_elem_count);                                                                \
    }                                                                                           \
}

#endif /* SPG_ATTR_H_ */
