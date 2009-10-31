#include <stdio.h>
#include <math.h>
#include <assert.h>
#include "bignum.h"
#include "runtime.h"

void bn_init(mpz_t *a)
{
  mpz_init(*a);
}

void bn_init_copy(mpz_t *a, mpz_t *b)
{
  mpz_init(*a);
  mpz_set(*a,*b);
}

void bn_clear(mpz_t *a)
{
  mpz_clear(*a);
}

void bn_double2int(double a, mpz_t *b)
{
  assert(a >= 0);
  mpz_init(*b);
  mpz_set_d(*b,a);
}

int bn_int2string(char *string, size_t size, mpz_t *a)
{
  char *string_needed;
  unsigned int needed_size;

  string_needed = mpz_get_str(NULL,10,*a);
  if(string_needed == NULL) Fatal(1,error,"Error converting string");
  needed_size = strlen(string_needed);
  if (needed_size < size) strcpy(string,string_needed);
  else if (string != NULL) string[0] = '\0';
  free(string_needed);
  return needed_size;
}

void bn_add(mpz_t *a,mpz_t *b,mpz_t *c)
{
  mpz_add(*c,*a,*b);
}

void bn_set_zero(mpz_t *a)
{
  mpz_set_ui(*a,0);
}

void bn_set_one(mpz_t *a)
{
  mpz_set_ui(*a,1);
}
