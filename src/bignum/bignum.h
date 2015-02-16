#ifndef LTSMIN_BIGNUM_H
#define LTSMIN_BIGNUM_H

/**
\file bignum.h
\brief Wrapper for bignum libraries
*/

#if defined(HAVE_GMP_H)
#include <gmp.h>
typedef mpz_t bn_int_t;

#elif defined(HAVE_TOMMATH_H)
#include <tommath.h>
typedef mp_int bn_int_t;

#else
typedef long long bn_int_t;
#endif


/**
\brief Initialize a bignum.

\param a Pointer to an uninitialized bignum.
 */
extern void         bn_init (bn_int_t *a);

/**
\brief Initialize a bignum with the value of another bignum.

\param a Pointer to an uninitialized bignum.
\param b Pointer to an initialized bignum whose value should be used.
*/
extern void         bn_init_copy (bn_int_t *a, bn_int_t *b);

/**
\brief Clear bignum (undoing any initialization).

\param a Pointer to an initialized bignum.
*/
extern void         bn_clear (bn_int_t *a);

/**
\brief Convert floating point value to bignum.

\param a Non-negative floating point value.
\param b Pointer to an uninitialized bignum to contain the converted value.
 */
extern void         bn_double2int (double a, bn_int_t *b);

/**
\brief Convert bignum to string.

\param string Pointer to buffer to contain the string.
\param a Pointer to initialized bignum to be converted.

This function converts bignums to strings. Its behavior is similar
to that of snprintf: If the string argument is non-NULL, the buffer
pointed to by this argument will contain a NULL-terminated string
representing at most size-1 digits of the bignum.
*/
void                bn_int2string (char *string, size_t size, bn_int_t *a);

/**
\brief Convert bignum to double.

\param a Pointer to initialized bignum to be converted.
*/
double              bn_int2double (bn_int_t *a);

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
extern void         bn_add (bn_int_t *a, bn_int_t *b, bn_int_t *c);

/**
\brief Set the value of a bignum to a small positive integer.

\param a Pointer to initialized bignum whose value should be set.

\param digit A small positive integer.
 */
extern void         bn_set_digit (bn_int_t *a, int digit);

/**
\brief Returns the length of the string representation of bignum a.

\param a Pointer to initialized bignum whos string length should be given.
 */
extern size_t       bn_strlen (bn_int_t *a);

#endif
