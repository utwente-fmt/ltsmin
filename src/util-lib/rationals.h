// -*- tab-width:4 ; indent-tabs-mode:nil -*-

/**
\file rationals.h
\brief Library for working with rational numbers.
*/

#ifndef RATIONALS_H
#define RATIONALS_H

#include <stdint.h>

/// Greatest common divisor.
extern uint32_t gcd32(uint32_t a,uint32_t b);

/// Least common multiple.
extern uint32_t lcm32(uint32_t a,uint32_t b);

/// Greatest common divisor.
extern uint64_t gcd64(uint64_t a,uint64_t b);

/// Least common multiple.
extern uint64_t lcm64(uint64_t a,uint64_t b);

/// Reverse engineer a rational number from a float.
extern void rationalize32(float f,uint32_t *numerator,uint32_t *denominator);

#endif

