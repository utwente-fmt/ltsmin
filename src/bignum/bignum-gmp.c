#include <hre/config.h>

#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "bignum.h"

void
bn_init (mpz_t *a)
{
    mpz_init (*a);
}

void
bn_init_copy (mpz_t *a, mpz_t *b)
{
    if (a == b)
        return;
    mpz_init (*a);
    mpz_set (*a, *b);
}

void
bn_clear (mpz_t *a)
{
    mpz_clear (*a);
}

void
bn_double2int (double a, mpz_t *b)
{
    assert (a >= 0);
    mpz_init (*b);
    mpz_set_d (*b, a);
}

int
bn_int2string (char *string, size_t size, mpz_t *a)
{
    const unsigned int  BASE = 10;
    unsigned int        needed_size = mpz_sizeinbase (*a, BASE) + 2;
    assert (string != NULL && size > 0);
    if (size >= needed_size)
        mpz_get_str (string, BASE, *a);
    else
        string[0] = '\0';
    return needed_size - 1;
}

double
bn_int2double (mpz_t *a)
{
    return mpz_get_d(*a);
}

void
bn_add (mpz_t *a, mpz_t *b, mpz_t *c)
{
    mpz_add (*c, *a, *b);
}

void
bn_set_digit (mpz_t *a, unsigned int digit)
{
    mpz_set_ui (*a, digit);
}
