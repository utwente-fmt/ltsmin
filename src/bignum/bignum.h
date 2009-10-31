#ifndef BIGNUM_H
#define BIGNUM_H

/**
\file bignum.h
\brief Wrapper for bignum libraries
*/

#define BIGNUM_GNUMP

#ifdef BIGNUM_LONGLONG
typedef long long bn_int;
#endif

#ifdef BIGNUM_TOMMATH
#include "tommath.h"

typedef mp_int bn_int;
#endif

#ifdef BIGNUM_GNUMP
#include <gmp.h>

typedef mpz_t bn_int;
#endif

/**
\brief Initialize a bignum.

\param a Pointer to an uninitialized bignum.
 */
void bn_init(bn_int *a);

/**
\brief Initialize a bignum with the value of another bignum.

\param a Pointer to an uninitialized bignum.
\param b Pointer to an initialized bignum whose value should be used.
*/
void bn_init_copy(bn_int *a, bn_int *b);

/**
\brief Clear bignum (undoing any initialization).

\param a Pointer to an initialized bignum.
*/
void bn_clear(bn_int *a);

/**
\brief Convert floating point value to bignum.

\param a Non-negative floating point value.
\param b Pointer to an uninitialized bignum to contain the converted value.
 */
void bn_double2int(double a, bn_int *b);

/**
\brief Convert bignum to string.

\param string Pointer to buffer to contain the string.
\param size Size of the buffer pointer to by the string argument.
\param a Pointer to initialized bignum to be converted.

This function converts bignums to strings. Its behavior is similar
to that of snprintf: If the string argument is non-NULL, the buffer
pointed to by this argument will contain a NULL-terminated string
representing at most size-1 digits of the bignum. The return value
of the function is equal to the number of digits that would have been
in the buffer in case its size would have been infinite.
*/
int bn_int2string(char *string, size_t size, bn_int *a);

/**
\brief Add two bignums

\param a Pointer to initialized bignum that is the to fist operand of the
addition.
\param b Pointer to initialized bignum that is the to second operand of the
addition.
\param c Pointer to initialized bignum that will contain the result.

The function adds two bignums. The bignums pointed to as arguments to the
function may be equal.
 */
void bn_add(bn_int *a,bn_int *b,bn_int *c);

/**
\brief Set the value of a bignum to 0.

\param a Pointer to initialized bignum whose value should be set to 0.
 */
void bn_set_zero(bn_int *a);

/**
\brief Set the value of a bignum to 1.

\param a Pointer to initialized bignum whose value should be set to 1.
 */
void bn_set_one(bn_int *a);

#endif
