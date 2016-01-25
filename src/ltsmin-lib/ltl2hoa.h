/**
 * C wrapper for the C++ file ltl2hoa.cpp
 */
#ifndef LTL2HOA_H
#define LTL2HOA_H

#ifdef __cplusplus
extern "C" {
#endif

#include <ltsmin-lib/ltsmin-buchi.h>

void ltsmin_ltl2hoa(ltsmin_expr_t e, int to_tgba);
ltsmin_buchi_t *ltsmin_hoa_buchi();

#ifdef __cplusplus
}
#endif

#endif // LTL2HOA_H