#include <hre/config.h>
#include <stdio.h>
#include <math.h>
#include <assert.h>
#include "bignum.h"

void
bn_init (long long *a)
{
    (void)a;
    return;
}

void
bn_init_copy (long long *a, long long *b)
{
    *a = *b;
}

void
bn_clear (long long *a)
{
    (void)a;
    return;
}

void
bn_double2int (double a, long long *b)
{
    assert (a >= 0);
    *b = llround (a);
}

int
bn_int2string (char *string, size_t size, long long *a)
{
    return snprintf (string, size, "%lld", *a);
}

double
bn_int2double (long long *a)
{
    return (double)(*a);
}

void
bn_add (long long *a, long long *b, long long *c)
{
    *c = *a + *b;
}

void
bn_set_digit (long long *a, unsigned int digit)
{
    *a = digit;
}
