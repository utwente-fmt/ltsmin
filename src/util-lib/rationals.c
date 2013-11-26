// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>

#include <math.h>

#include <hre/user.h>
#include <util-lib/rationals.h>

uint32_t gcd32(uint32_t a,uint32_t b){
    uint32_t tmp;
    for(;;){
        if(a>b) {
            tmp=a;
            a=b;
            b=tmp;
        }
        tmp=b%a;
        if (tmp==0) return a;
        b=a;
        a=tmp;
    }
}

uint32_t lcm32(uint32_t a,uint32_t b){
    return (a/gcd32(a,b))*b;
}

uint64_t gcd64(uint64_t a,uint64_t b){
    uint64_t tmp;
    for(;;){
        if(a>b) {
            tmp=a;
            a=b;
            b=tmp;
        }
        tmp=b%a;
        if (tmp==0) return a;
        b=a;
        a=tmp;
    }
}

uint64_t lcm64(uint64_t a,uint64_t b){
    return (a/gcd64(a,b))*b;
}

static float two_epsilon=0.000001;
static int fequal(float f1,float f2){
    float f=(f1+f2);
    float t1=f1/f;
    float t2=f2/f;
    f=fabsf(t2-t1);
    return (f<two_epsilon);
}

void
rationalize32 (float f,uint32_t *numerator,uint32_t *denominator)
{
    uint32_t i,j;
    // try x for 32 bit x
    j=1;
    i=nearbyintf(((float)j)*f);
    if (i==0){
        i=nearbyintf(1000000000*f);
        if (i==0){
            *numerator=0;
            *denominator=1;
            return;           
        }
    } else if (fequal(f,(float)i)){
        *numerator=i;
        *denominator=1;
        return;
    }
    // try x/y for 8 bit x,y
    for(j=2;j<256;j++){
        i=nearbyintf(((float)j)*f);
        if (i<=0 || i>=256) continue;
        if (fequal(f,((float)i)/((float)j))){
            *numerator=i;
            *denominator=j;
            return;
        }
    }
    // try 3(4??) digit decimal.
    for(j=1000;j<1000000000;j=j*10){
        i=nearbyintf(((float)j)*f);
        if (i<=100 || i>10000) continue;
        if (fequal(f,((float)i)/((float)j))){
            uint32_t c=gcd32(i,j);
            *numerator=i/c;
            *denominator=j/c;
            return;
        }
    }
    Abort("Attempt to reverse engineer %f failed",f);
}
