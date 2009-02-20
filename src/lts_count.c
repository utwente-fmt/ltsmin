#include <stdlib.h>
#include <lts_count.h>
#include <runtime.h>

void lts_count_init(lts_count_t *count,int flags,int counted,int segments){
	count->segments=segments;
	count->state=RTmalloc(segments*sizeof(lts_count_int_t));
	count->in=RTmalloc(segments*sizeof(lts_count_int_t));
	count->out=RTmalloc(segments*sizeof(lts_count_int_t));
	count->cross=RTmalloc(segments*sizeof(int*));
	for(int i=0;i<segments;i++){
		count->state[i]=LTS_COUNT_UNDEF;
		count->in[i]=LTS_COUNT_UNDEF;
		count->out[i]=LTS_COUNT_UNDEF;
		count->cross[i]=RTmalloc(segments*sizeof(lts_count_int_t));
		for(int j=0;j<segments;j++){
			count->cross[i][j]=LTS_COUNT_UNDEF;
		}
	}
	int from=(counted==segments)?0:counted;
	int to=(counted==segments)?segments:(counted+1);
	if (flags|LTS_COUNT_STATE) for(int i=from;i<to;i++) count->state[i]=0;
	if (flags|LTS_COUNT_IN) for(int i=from;i<to;i++) count->in[i]=0;
	if (flags|LTS_COUNT_OUT) for(int i=from;i<to;i++) count->out[i]=0;
	if (flags|LTS_COUNT_CROSS_IN) for(int i=0;i<segments;i++) for(int j=from;j<to;j++) count->cross[i][j]=0;
	if (flags|LTS_COUNT_CROSS_OUT) for(int i=from;i<to;i++) for(int j=0;j<segments;j++) count->cross[i][j]=0;
}

void lts_count_fini(lts_count_t *count){
	for(int i=0;i<count->segments;i++){
		free(count->cross[i]);
	}
	free(count->state);
	free(count->in);
	free(count->out);
	free(count->cross);
	bzero(count,sizeof(lts_count_t));
}

