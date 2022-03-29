/**
 * C wrapper for the C++ file ltl2spot.cpp
 */
#ifndef LTL2SPOT_H
#define LTL2SPOT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <ltsmin-lib/ltsmin-buchi.h>

// use spot to build an automaton from a parsed LTL expression
void ltsmin_ltl2spot(ltsmin_expr_t e, int to_tgba, ltsmin_parse_env_t env);
// transform the spot::twa automaton built by ltl2spot to an ltsmin_buchi_t
ltsmin_buchi_t *ltsmin_hoa_buchi(ltsmin_parse_env_t env);
// directly parse the given HOA file and build an ltsmin_buchi_t
ltsmin_buchi_t *ltsmin_parse_hoa_buchi(const char * hoa_file, int to_tgba, ltsmin_parse_env_t env);
void ltsmin_hoa_destroy();

#ifdef __cplusplus
}
#endif

#endif // LTL2SPOT_H
