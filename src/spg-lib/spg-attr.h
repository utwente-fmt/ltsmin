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
void spg_attractor(int player, const parity_game* g, vset_t u, const spg_attr_options* options);


#if defined(HAVE_SYLVAN)

/**
 * \brief Computes attractor set
 */
void spg_attractor_par(int player, const parity_game* g, vset_t u, const spg_attr_options* options);


/**
 * \brief Computes attractor set
 */
void spg_attractor_par2(int player, const parity_game* g, vset_t u, const spg_attr_options* options);

#endif


/**
 * \brief Computes attractor set
 */
void spg_attractor_chaining(int player, const parity_game* g, vset_t u, const spg_attr_options* options);

#define SPG_ATTR_REPORT_LEVEL(OPT,PLAYER,U,V_LEVEL,LEVEL) {                                     \
    if (log_active(infoLong))                                                                   \
    {                                                                                           \
        long   u_count;                                                                         \
        bn_int_t u_elem_count;                                                                  \
        long   level_count;                                                                     \
        bn_int_t level_elem_count;                                                              \
        vset_count(U, &u_count, &u_elem_count);                                                 \
        vset_count(V_LEVEL, &level_count, &level_elem_count);                                   \
        RTstopTimer(OPT->timer);                                                                \
        Print(infoLong, "attr_%d^%d [%5.3f]: u has %ld nodes, v_level has %ld nodes.",      \
                PLAYER, LEVEL, RTrealTime(OPT->timer), u_count, level_count);                   \
        RTstartTimer(OPT->timer);                                                               \
        bn_clear(&u_elem_count);                                                                \
        bn_clear(&level_elem_count);                                                            \
    }                                                                                           \
}

#define SPG_ATTR_REPORT_SET(OPT,PLAYER,SET,LEVEL) {                                             \
    if (log_active(infoLong))                                                                   \
    {                                                                                           \
        RTstopTimer(OPT->timer);                                                                \
        Print(infoLong, "attr_%d^%d [%5.3f]: counting " #SET ".",                             \
        		PLAYER, LEVEL, RTrealTime(OPT->timer));                                         \
        long   s_count;                                                                         \
        bn_int_t s_elem_count;                                                                  \
        vset_count(SET, &s_count, &s_elem_count);                                               \
        Print(infoLong, "attr_%d^%d [%5.3f]: " #SET " has %ld nodes.",                       \
                PLAYER, LEVEL, RTrealTime(OPT->timer), s_count);                                \
        RTstartTimer(OPT->timer);                                                               \
        bn_clear(&s_elem_count);                                                                \
    }                                                                                           \
}

#define SPG_ATTR_REPORT_REL(OPT,PLAYER,REL,LEVEL) {                                             \
    if (log_active(infoLong))                                                                   \
    {                                                                                           \
        RTstopTimer(OPT->timer);                                                                \
        Print(infoLong, "attr_%d^%d [%5.3f]: counting rel " #REL ".",                        \
                PLAYER, LEVEL, RTrealTime(OPT->timer));                                         \
        long   s_count;                                                                         \
        bn_int_t s_elem_count;                                                                  \
        vrel_count(REL, &s_count, &s_elem_count);                                               \
        Print(infoLong, "attr_%d^%d [%5.3f]: " #REL " has %ld nodes.",                       \
                PLAYER, LEVEL, RTrealTime(OPT->timer), s_count);                                \
        RTstartTimer(OPT->timer);                                                               \
        bn_clear(&s_elem_count);                                                                \
    }                                                                                           \
}

#endif /* SPG_ATTR_H_ */
