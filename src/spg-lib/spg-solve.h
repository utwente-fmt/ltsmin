/*
 * \file spg-solve.h
 *
 *  Created on: 23 Jan 2012
 *      Author: kant
 */
#ifndef SPG_SOLVE_H_
#define SPG_SOLVE_H_

#include <float.h>
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
    double elem_count;                      \
    vset_count(SET, &N_COUNT, &elem_count); \
}

#define SPG_REPORT_RECURSIVE_RESULT(OPT,INDENT,RESULT) {                 \
    if (log_active(infoLong))                                            \
    {                                                                    \
        long   n_count;                                                  \
        vset_count(RESULT.win[0], &n_count, NULL);                       \
        RTstopTimer(OPT->timer);                                         \
        Print(infoLong, "[%7.3f] " "%*s" "solve_recursive: result.win[0]: %ld nodes.", \
        		RTrealTime(OPT->timer), INDENT, "", n_count);            \
        RTstartTimer(OPT->timer);                                        \
        vset_count(RESULT.win[1], &n_count, NULL);                       \
        RTstopTimer(OPT->timer);                                         \
        Print(infoLong, "[%7.3f] " "%*s" "solve_recursive: result.win[1]: %ld nodes.", \
                RTrealTime(OPT->timer), INDENT, "", n_count);            \
        RTstartTimer(OPT->timer);                                        \
    }                                                                    \
}

#define SPG_REPORT_MIN_PRIORITY(OPT,INDENT,M, U) {                  \
    if (log_active(infoLong))                                       \
    {                                                               \
        long   n_count;                                             \
        vset_count(U, &n_count, NULL);                              \
        RTstopTimer(OPT->timer);                                    \
        Print(infoLong, "[%7.3f] " "%*s" "solve_recursive: m=%d, u has %ld nodes.", \
        		RTrealTime(OPT->timer), INDENT, "", M, n_count);    \
        RTstartTimer(OPT->timer);                                   \
    }                                                               \
}

#define SPG_REPORT_DEADLOCK_STATES(OPT,INDENT,PLAYER) {                                     \
    if(log_active(infoLong))                                                                \
    {                                                                                       \
        long   n_count;                                                                     \
        double elem_count;                                                                  \
        vset_count(deadlock_states[PLAYER], &n_count, &elem_count);                         \
        RTstopTimer(OPT->timer);                                                            \
        Print(infoLong, "[%7.3f] " "%*s" "solve_recursive: %.*g deadlock states (%ld nodes) with result '%s' (player=%d).", \
        		RTrealTime(OPT->timer), INDENT, "", DBL_DIG, elem_count, n_count, ((PLAYER==0)?"false":"true"), PLAYER);  \
        RTstartTimer(OPT->timer);                                                           \
    }                                                                                       \
}

#define SPG_REPORT_SET(OPT,INDENT,SET) {                                                        \
    if (log_active(infoLong))                                                                   \
    {                                                                                           \
        RTstopTimer(OPT->timer);                                                                \
        Print(infoLong, "[%7.3f] " "%*s" "counting " #SET ".",                                  \
                RTrealTime(OPT->timer), indent, "");                                            \
        RTstartTimer(OPT->timer);                                                               \
        long   s_count;                                                                         \
        vset_count(SET, &s_count, NULL);                                                        \
        RTstopTimer(OPT->timer);                                                                \
        Print(infoLong, "[%7.3f] " "%*s" #SET " has %ld nodes.",                                \
                RTrealTime(OPT->timer), indent, "", s_count);                                   \
        RTstartTimer(OPT->timer);                                                               \
    }                                                                                           \
}

#define SPG_REPORT_ELEMENTS(OPT,INDENT,SET) {                                                   \
    if (log_active(infoLong))                                                                   \
    {                                                                                           \
        RTstopTimer(OPT->timer);                                                                \
        Print(infoLong, "[%7.3f] " "%*s" "counting " #SET ".",                                  \
                RTrealTime(OPT->timer), indent, "");                                            \
        RTstartTimer(OPT->timer);                                                               \
        long   s_count;                                                                         \
        double s_elem_count;                                                                    \
        vset_count(SET, &s_count, &s_elem_count);                                               \
        RTstopTimer(OPT->timer);                                                                \
        Print(infoLong, "[%7.3f] " "%*s" #SET " has %.*g states (%ld nodes).",                  \
                RTrealTime(OPT->timer), indent, "", DBL_DIG, s_elem_count, s_count);            \
        RTstartTimer(OPT->timer);                                                               \
    }                                                                                           \
}


#endif /* SPG_SOLVE_H_ */
