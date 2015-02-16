#include <hre/config.h>
#include <stdio.h>
#include <math.h>
#include <assert.h>
#include "bignum.h"
#include "runtime.h"

void
bn_init (mp_int *a)
{
    int                 ret = mp_init (a);
    if (ret != MP_OKAY)
        Fatal (1, error, "Error initializing number");
}

void
bn_init_copy (mp_int *a, mp_int *b)
{
    int                 ret;
    if (a == b)
        return;
    ret = mp_init_copy (a, b);
    if (ret != MP_OKAY)
        Fatal (1, error, "Error initializing number");
}

void
bn_clear (mp_int *a)
{
    mp_clear (a);
}

void
bn_double2int (double a, mp_int *b)
{
    const unsigned int  FRAC_BITS = 52;
    int                 ret,
                        exp;
    long int            val;
    double              frac;
    mp_int              upper,
                        number;

    assert (a >= 0);
    ret = mp_init_multi (b, &number, &upper, NULL);
    if (ret != MP_OKAY)
        Fatal (1, error, "Error initializing number");
    mp_set (&upper, 1);
    frac = frexp (a, &exp);
    ret = mp_mul_2d (&upper, exp, &upper);
    if (ret != MP_OKAY)
        Fatal (1, error, "Error initializing number");
    for (unsigned int i = 0; i < FRAC_BITS; i++) {
        frac = frac * 2;
        val = lround (floor (frac));
        assert (val == 0 || val == 1);
        frac = frac - lround (floor (frac));
        if (val == 1) {
            mp_div_2d (&upper, i + 1, &number, NULL);
            if (ret != MP_OKAY)
                Fatal (1, error, "Error dividing numbers");
            mp_add (b, &number, b);
            if (ret != MP_OKAY)
                Fatal (1, error, "Error adding numbers");
        }
    }
    mp_clear_multi (&number, &upper, NULL);
}

double
bn_int2double (mp_int *a)
{
    double              value = 0,
                        multiplier = 1;
    int                 ret;
    mp_int              dividend,
                        remainder;

    ret = mp_init_copy (&dividend, a);
    if (ret != MP_OKAY)
        Fatal (1, error, "Error initializing number");
    ret = mp_init (&remainder);
    if (ret != MP_OKAY)
        Fatal (1, error, "Error initializing number");
    while (!mp_iszero (&dividend)) {
        mp_div_2d (&dividend, 1, &dividend, &remainder);
        if (mp_isodd (&remainder))
            value = value + multiplier;
        multiplier = multiplier * 2;
    }
    mp_clear_multi (&dividend, &remainder, NULL);
    return value;
}

void
bn_int2string (char *string, size_t size, mp_int *a)
{
    (void) size;
    int ret;
    ret = mp_toradix (a, string, 10);
    if (ret != MP_OKAY)
        Fatal (1, error, "Error converting number to string");
}

void
bn_add (mp_int *a, mp_int *b, mp_int *c)
{
    int                 ret = mp_add (a, b, c);
    if (ret != MP_OKAY)
        Fatal (1, error, "Error adding numbers");
};

void
bn_set_digit (mp_int *a, int digit)
{
    mp_set (a, digit);
}

size_t
bn_strlen (mp_int *a)
{
    int needed_size;
    int ret = mp_radix_size (a, 10, &needed_size);
    if (ret != MP_OKAY)
            Fatal (1, error, "Error getting radix size");

    return needed_size - 1;
}
