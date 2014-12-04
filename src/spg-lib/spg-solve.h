/*
 * \file spg-solve.h
 *
 *  Created on: 23 Jan 2012
 *      Author: kant
 */
#ifndef SPG_SOLVE_H_
#define SPG_SOLVE_H_

#include <stdbool.h>

#include <hre/runtime.h>
#include <spg-lib/spg.h>
#include <spg-lib/spg-attr.h>
#include <spg-lib/spg-options.h>
#include <spg-lib/spg-strategy.h>


/**
 * \brief
 */
bool spg_solve(parity_game* g, recursive_result* result, spgsolver_options* options);


/**
 * \brief Solves the parity game encoded in node set V and the partitioned edge set
 * E.
 * \param v
 * \return true iff ...
 */
recursive_result spg_solve_recursive(parity_game* g, const spgsolver_options* options, int depth);


/**
 * \brief
 */
void spg_game_restrict(parity_game *g, vset_t a, const spgsolver_options* options);


#ifdef LTSMIN_DEBUG
#define SPG_OUTPUT_DOT(DOT,SET,NAME,...)  {                    \
    char dotfilename[255];                                          \
    FILE* dotfile;                                                  \
    if (DOT)                                                        \
    {                                                               \
        sprintf(dotfilename, NAME, __VA_ARGS__);                    \
        dotfile = fopen(dotfilename,"w");                           \
        vset_dot(dotfile, SET);                                       \
        fclose(dotfile);                                            \
    }                                                               \
}
#else
#define SPG_OUTPUT_DOT(DOT,SET,NAME,...)
#endif

#define VSET_COUNT_NODES(SET, N_COUNT) {    \
    bn_int_t elem_count;                    \
    vset_count(SET, &N_COUNT, &elem_count); \
    bn_clear(&elem_count);                  \
}

#define SPG_REPORT_RECURSIVE_RESULT(OPT,INDENT,RESULT) {                 \
    if (log_active(infoLong))                                            \
    {                                                                    \
        long   n_count;                                                  \
        bn_int_t elem_count;                                             \
        vset_count(RESULT.win[0], &n_count, &elem_count);                \
        RTstopTimer(OPT->timer);                                         \
        Print(infoLong, "[%7.3f] " "%*s" "solve_recursive: result.win[0]: %ld nodes.", \
        		RTrealTime(OPT->timer), INDENT, "", n_count);            \
        RTstartTimer(OPT->timer);                                        \
        vset_count(RESULT.win[1], &n_count, &elem_count);                \
        RTstopTimer(OPT->timer);                                         \
        Print(infoLong, "[%7.3f] " "%*s" "solve_recursive: result.win[1]: %ld nodes.", \
                RTrealTime(OPT->timer), INDENT, "", n_count);            \
        RTstartTimer(OPT->timer);                                        \
        bn_clear(&elem_count);                                           \
    }                                                                    \
}

#define SPG_REPORT_MIN_PRIORITY(OPT,INDENT,M, U) {                  \
    if (log_active(infoLong))                                       \
    {                                                               \
        long   n_count;                                             \
        bn_int_t elem_count;                                        \
        vset_count(U, &n_count, &elem_count);                       \
        RTstopTimer(OPT->timer);                                    \
        Print(infoLong, "[%7.3f] " "%*s" "solve_recursive: m=%d, u has %ld nodes.", \
        		RTrealTime(OPT->timer), INDENT, "", M, n_count);    \
        RTstartTimer(OPT->timer);                                   \
        bn_clear(&elem_count);                                      \
    }                                                               \
}

#define SPG_REPORT_DEADLOCK_STATES(OPT,INDENT,PLAYER) {                                     \
    if(log_active(infoLong))                                                                \
    {                                                                                       \
        long   n_count;                                                                     \
        bn_int_t elem_count;                                                                \
        vset_count(deadlock_states[PLAYER], &n_count, &elem_count);                         \
        char s[1024];                                                                       \
        bn_int2string(s, sizeof(s), &elem_count);                                           \
        RTstopTimer(OPT->timer);                                                            \
        Print(infoLong, "[%7.3f] " "%*s" "solve_recursive: %s deadlock states (%ld nodes) with result '%s' (player=%d).", \
        		RTrealTime(OPT->timer), INDENT, "", s, n_count, ((PLAYER==0)?"false":"true"), PLAYER);  \
        RTstartTimer(OPT->timer);                                                           \
        bn_clear(&elem_count);                                                              \
    }                                                                                       \
}

#define SPG_REPORT_SET(OPT,INDENT,SET) {                                                        \
    if (log_active(infoLong))                                                                   \
    {                                                                                           \
        RTstopTimer(OPT->timer);                                                                \
        Print(infoLong, "[%7.3f] " "%*s" "counting " #SET ".",                                \
                RTrealTime(OPT->timer), indent, "");                                            \
        RTstartTimer(OPT->timer);                                                               \
        long   s_count;                                                                         \
        bn_int_t s_elem_count;                                                                  \
        vset_count(SET, &s_count, &s_elem_count);                                               \
        RTstopTimer(OPT->timer);                                                                \
        Print(infoLong, "[%7.3f] " "%*s" #SET " has %ld nodes.",                              \
                RTrealTime(OPT->timer), indent, "", s_count);                                   \
        RTstartTimer(OPT->timer);                                                               \
        bn_clear(&s_elem_count);                                                                \
    }                                                                                           \
}

#define SPG_REPORT_ELEMENTS(OPT,INDENT,SET) {                                                   \
    if (log_active(infoLong))                                                                   \
    {                                                                                           \
        RTstopTimer(OPT->timer);                                                                \
        Print(infoLong, "[%7.3f] " "%*s" "counting " #SET ".",                                \
                RTrealTime(OPT->timer), indent, "");                                            \
        RTstartTimer(OPT->timer);                                                               \
        long   s_count;                                                                         \
        bn_int_t s_elem_count;                                                                  \
        vset_count(SET, &s_count, &s_elem_count);                                               \
        char s[1024];                                                                           \
        bn_int2string(s, sizeof(s), &s_elem_count);                                             \
        RTstopTimer(OPT->timer);                                                                \
        Print(infoLong, "[%7.3f] " "%*s" #SET " has %s states (%ld nodes).",                 \
                RTrealTime(OPT->timer), indent, "", s, s_count);                                \
        RTstartTimer(OPT->timer);                                                               \
        bn_clear(&s_elem_count);                                                                \
    }                                                                                           \
}


#endif /* SPG_SOLVE_H_ */
