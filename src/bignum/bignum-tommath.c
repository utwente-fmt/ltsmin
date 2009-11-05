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
    int                 ret = mp_init_copy (a, b);
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
    for (int i = 0; i < 52; i++) {
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

int
bn_int2string (char *string, size_t size, mp_int *a)
{
    int                 ret,
                        needed_size;

    assert (string != NULL && size > 0);
    ret = mp_radix_size (a, 10, &needed_size);
    if (ret != MP_OKAY)
        Fatal (1, error, "Error getting radix size");
    if ((size_t) needed_size <= size) { // needed_size is always positive
        ret = mp_toradix (a, string, 10);
        if (ret != MP_OKAY)
            Fatal (1, error, "Error converting number to string");
    } else {
        string[0] = '\0';
    }
    return needed_size - 1;
}

void
bn_add (mp_int *a, mp_int *b, mp_int *c)
{
    int                 ret = mp_add (a, b, c);
    if (ret != MP_OKAY)
        Fatal (1, error, "Error adding numbers");
};

void
bn_set_digit (mp_int *a, unsigned int digit)
{
    mp_set (a, digit);
}
