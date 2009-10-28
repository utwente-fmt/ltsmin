#include <stdio.h>
#include <math.h>
#include <assert.h>
#include "bignum.h"
#include "runtime.h"

#ifdef BIGNUM_LONGLONG
void bn_init(long long *a){
  return;
}

void bn_init_copy(long long *a, long long *b){
  *a = *b;
}

void bn_clear(long long *a){
  return;
}

void bn_double2int(double a, long long *b){
  assert(a >= 0);
  *b = llround(a);
}

int bn_int2string(char *string, size_t size, long long *a){
  return snprintf(string, size, "%lld", *a);
}

void bn_add(long long *a,long long *b, long long *c){
  *c = *a + *b;
}

void bn_set_zero(long long *a){
  *a = 0;
}

void bn_set_one(long long *a){
  *a = 1;
}
#endif

#ifdef BIGNUM_TOMMATH
void bn_init(mp_int *a){
  int ret = mp_init(a);
  if(ret != MP_OKAY) Fatal(1,error,"Error initializing number");
}

void bn_init_copy(mp_int *a, mp_int *b){
  int ret = mp_init_copy(a,b);
  if(ret != MP_OKAY) Fatal(1,error,"Error initializing number");
}

void bn_clear(mp_int *a){
  mp_clear(a);
}

void bn_double2int(double a, mp_int *b){
  int ret, exp;
  long int val;
  double frac;
  mp_int upper, number;

  assert(a >= 0);
  ret = mp_init_multi(b,&number,&upper,NULL);
  if(ret != MP_OKAY) Fatal(1,error,"Error initializing number");
  mp_set(&upper,1);
  frac = frexp(a,&exp);
  ret = mp_mul_2d(&upper,exp,&upper);
  if(ret != MP_OKAY) Fatal(1,error,"Error initializing number");
  for(int i=0;i<52;i++) {
    frac = frac * 2;
    val = lround(floor(frac));
    assert(val == 0 || val == 1);
    frac = frac - lround(floor(frac));
    if (val == 1) {
      mp_div_2d(&upper,i+1,&number,NULL);
      if(ret != MP_OKAY) Fatal(1,error,"Error dividing numbers");
      mp_add(b,&number,b);
      if(ret != MP_OKAY) Fatal(1,error,"Error adding numbers");
    }
  }
  mp_clear_multi(&number,&upper,NULL);
}

int bn_int2string(char* string, size_t size, mp_int *a){
  int ret, needed_size;

  ret = mp_radix_size(a,10,&needed_size);
  if(ret != MP_OKAY) Fatal(1,error,"Error getting radix size");
  if((size_t)needed_size <= size) { //needed_size is always positive
    ret = mp_toradix(a,string,10);
    if(ret != MP_OKAY) Fatal(1,error,"Error converting number to string");    
  } else if(string != NULL) {
    string[0]='\0';
  }
  return needed_size-1;
}

void bn_add(mp_int *a,mp_int *b, mp_int *c){
  int ret = mp_add(a,b,c);
  if(ret != MP_OKAY) Fatal(1,error,"Error adding numbers");
};

void bn_set_zero(mp_int *a){
  mp_set(a,0);
}

void bn_set_one(mp_int *a){
  mp_set(a,1);
}
#endif

#ifdef BIGNUM_GNUMP
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
#endif
