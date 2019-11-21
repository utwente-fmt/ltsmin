#include <hre/config.h>
#include <hre/user.h>
#include <hre/feedback.h>
#include <stdio.h>
#include <math.h>
#include <assert.h>
#include "bignum.h"

void
bn_init (long long *a)
{
    *a = 0;
}

void
bn_init_copy (long long *a, long long *b)
{
    *a = *b;
}

void
bn_clear (long long *a)
{
    *a = 0;
}

void
bn_double2int (double a, long long *b)
{
    assert (a >= 0);
    *b = llround (a);
}

void
bn_int2string (char *string, size_t size, long long *a)
{
    snprintf (string, size, "%lld", *a);
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
    if (*c < *a || *c < *b) Abort("long long overflow");
}

void
bn_set_digit (long long *a, int digit)
{
    *a = digit;
}

size_t
bn_strlen (long long *a)
{
    return snprintf(NULL, 0, "%lld", *a) + 1;
}
