#ifndef LTS_COUNT_H
#define LTS_COUNT_H

#include <stdint.h>

typedef uint64_t lts_count_int_t;

#define LTS_COUNT_UNDEF ((uint64_t)0xFFFFFFFFFFFFFFFF)

typedef struct {
	int segments;
	lts_count_int_t *state;
	lts_count_int_t *in;
	lts_count_int_t *out;
	lts_count_int_t **cross;
} lts_count_t;

#define LTS_COUNT_STATE     0x01
#define LTS_COUNT_IN        0x02
#define LTS_COUNT_OUT       0x04
#define LTS_COUNT_CROSS_IN  0x08
#define LTS_COUNT_CROSS_OUT 0x10
#define LTS_COUNT_ALL       0x1F

#define LTS_CHECK_STATE(count,seg,ofs) {if((ofs)>=count.state[seg]) count.state[seg]=(ofs)+1;}
#define LTS_INCR_CROSS(count,from,to) {if(count.cross[from][to]<LTS_COUNT_UNDEF) count.cross[from][to]++;}
#define LTS_INCR_IN(count,seg) {if(count.in[seg]<LTS_COUNT_UNDEF) {count.in[seg]++;}}
#define LTS_INCR_OUT(count,seg) {if(count.out[seg]<LTS_COUNT_UNDEF) {count.out[seg]++;}}

/**
\brief Initialize a counter struct.

\param flags The counters that must be zeroed.
\param counted If less than segments then just the counters for the mentioned segment will be zeroed,
otherwise all counters are zeroed.
\param segments The number of segments.
 */
extern void lts_count_init(lts_count_t *count,int flags,int counted,int segments);

/**
\brief Clean up.
 */
extern void lts_count_fini(lts_count_t *count);

#endif


