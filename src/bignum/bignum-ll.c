#include <stdio.h>
#include <math.h>
#include "bignum.h"

void
bn_init (long long *a)
{
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

void
bn_add (long long *a, long long *b, long long *c)
{
    *c = *a + *b;
}

void
bn_set_zero (long long *a)
{
    *a = 0;
}

void
bn_set_one (long long *a)
{
    *a = 1;
}
