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
        long   level_count;                                                                     \
        vset_count(U, &u_count, NULL);                                                          \
        vset_count(V_LEVEL, &level_count, NULL);                                                \
        RTstopTimer(OPT->timer);                                                                \
        Print(infoLong, "[%7.3f] " "%*s" "attr_%d^%d: u has %ld nodes, v_level has %ld nodes.", \
        		RTrealTime(OPT->timer), INDENT, "", PLAYER, LEVEL, u_count, level_count);       \
        RTstartTimer(OPT->timer);                                                               \
    }                                                                                           \
    else                                                                                        \
    {                                                                                           \
        Print(info, "[%7.3f] " "%*s" "attr_%d: level %d.",                                      \
                RTrealTime(OPT->timer), INDENT, "", PLAYER, LEVEL);                             \
    }                                                                                           \
}

#define SPG_ATTR_REPORT_SET(INDENT,OPT,PLAYER,SET,LEVEL) {                                      \
    if (log_active(infoLong))                                                                   \
    {                                                                                           \
        RTstopTimer(OPT->timer);                                                                \
        Print(infoLong, "[%7.3f] " "%*s" "attr_%d^%d: counting " #SET ".",                      \
        		RTrealTime(OPT->timer), INDENT, "", PLAYER, LEVEL);                             \
        long   s_count;                                                                         \
        vset_count(SET, &s_count, NULL);                                                        \
        Print(infoLong, "[%7.3f] " "%*s" "attr_%d^%d: " #SET " has %ld nodes.",                 \
        		RTrealTime(OPT->timer), INDENT, "", PLAYER, LEVEL, s_count);                    \
        RTstartTimer(OPT->timer);                                                               \
    }                                                                                           \
}

#define SPG_ATTR_REPORT_ELEMENTS(INDENT,OPT,PLAYER,SET,LEVEL) {                                 \
    if (log_active(infoLong))                                                                   \
    {                                                                                           \
        long   s_count;                                                                         \
        double s_elem_count;                                                                    \
        vset_count(SET, &s_count, &s_elem_count);                                               \
        RTstopTimer(OPT->timer);                                                                \
        Print(infoLong, "[%7.3f] " "%*s" "attr_%d^%d: " #SET " has %.*g elements (%ld nodes).", \
                RTrealTime(OPT->timer), INDENT, "", PLAYER, LEVEL, DBL_DIG, s_elem_count, s_count);      \
        RTstartTimer(OPT->timer);                                                               \
    }                                                                                           \
}

#define SPG_ATTR_REPORT_REL(INDENT,OPT,PLAYER,REL,LEVEL) {                                      \
    if (log_active(infoLong))                                                                   \
    {                                                                                           \
        RTstopTimer(OPT->timer);                                                                \
        Print(infoLong, "[%7.3f] " "%*s" "attr_%d^%d: counting rel " #REL ".",                  \
        		RTrealTime(OPT->timer), INDENT, "", PLAYER, LEVEL);                             \
        long   s_count;                                                                         \
        vrel_count(REL, &s_count, NULL);                                                        \
        Print(infoLong, "[%7.3f] " "%*s" "attr_%d^%d: " #REL " has %ld nodes.",                 \
        		RTrealTime(OPT->timer), INDENT, "", PLAYER, LEVEL, s_count);                    \
        RTstartTimer(OPT->timer);                                                               \
    }                                                                                           \
}

#endif /* SPG_ATTR_H_ */
