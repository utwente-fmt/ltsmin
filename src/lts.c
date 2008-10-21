
#include "lts.h"
#include <unistd.h>
#include "runtime.h"
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <stdlib.h>


lts_t lts_create(){
	lts_t lts=(lts_t)RTmalloc(sizeof(struct lts));
	lts->begin=NULL;
	lts->src=NULL;
	lts->label=NULL;
	lts->dest=NULL;
	lts->type=LTS_LIST;
	lts->transitions=0;
	lts->states=0;
	lts->tau=-1;
	lts->label_string=NULL;
	return lts;
}

void lts_free(lts_t lts){
	free(lts->begin);
	free(lts->src);
	free(lts->label);
	free(lts->dest);
	free(lts);
}

static void build_block(uint32_t states,uint32_t transitions,u_int32_t *begin,u_int32_t *block,u_int32_t *label,u_int32_t *other){
	uint32_t i;
	uint32_t loc1,loc2;
	u_int32_t tmp_label1,tmp_label2;
	u_int32_t tmp_other1,tmp_other2;

	for(i=0;i<states;i++) begin[i]=0;
	for(i=0;i<transitions;i++) begin[block[i]]++;
	for(i=1;i<states;i++) begin[i]=begin[i]+begin[i-1];
	for(i=transitions;i>0;){
		i--;
		block[i]=--begin[block[i]];
	}
	begin[states]=transitions;
	for(i=0;i<transitions;i++){
		if (block[i]==i) {
			continue;
		}
		loc1=block[i];
		tmp_label1=label[i];
		tmp_other1=other[i];
		for(;;){
			if (loc1==i) {
				block[i]=i;
				label[i]=tmp_label1;
				other[i]=tmp_other1;
				break;
			}
			loc2=block[loc1];
			tmp_label2=label[loc1];
			tmp_other2=other[loc1];
			block[loc1]=loc1;
			label[loc1]=tmp_label1;
			other[loc1]=tmp_other1;
			if (loc2==i) {
				block[i]=i;
				label[i]=tmp_label2;
				other[i]=tmp_other2;
				break;
			}
			loc1=block[loc2];
			tmp_label1=label[loc2];
			tmp_other1=other[loc2];
			block[loc2]=loc2;
			label[loc2]=tmp_label2;
			other[loc2]=tmp_other2;
		}
	}
}

void lts_set_type(lts_t lts,LTS_TYPE type){
	uint32_t i,j;

	if (lts->type==type) return; /* no type change */

	/* first change to LTS_LIST */
	switch(lts->type){
		case LTS_LIST:
			lts->begin=(u_int32_t*)RTmalloc(sizeof(u_int32_t)*(lts->states+1));
			break;
		case LTS_BLOCK:
			lts->src=(u_int32_t*)RTmalloc(sizeof(u_int32_t)*(lts->transitions));
			for(i=0;i<lts->states;i++){
				for(j=lts->begin[i];j<lts->begin[i+1];j++){
					lts->src[j]=i;
				}
			}
			break;
		case LTS_BLOCK_INV:
			lts->dest=(u_int32_t*)RTmalloc(sizeof(u_int32_t)*(lts->transitions));
			for(i=0;i<lts->states;i++){
				for(j=lts->begin[i];j<lts->begin[i+1];j++){
					lts->dest[j]=i;
				}
			}
			break;
	}
//	MEMSTAT_CHECK;
	/* then change to requried type */
	lts->type=type;
	switch(type){
		case LTS_LIST:
			free(lts->begin);
			lts->begin=NULL;
			return;
		case LTS_BLOCK:
			build_block(lts->states,lts->transitions,lts->begin,lts->src,lts->label,lts->dest);
			free(lts->src);
			lts->src=NULL;
			return;
		case LTS_BLOCK_INV:
			build_block(lts->states,lts->transitions,lts->begin,lts->dest,lts->label,lts->src);
			free(lts->dest);
			lts->dest=NULL;
			return;
	}
}



void lts_set_size(lts_t lts,u_int32_t states,u_int32_t transitions){
	lts->states=states;
	lts->transitions=transitions;
	switch(lts->type){
		case LTS_BLOCK:
		case LTS_BLOCK_INV:
			lts->begin=(u_int32_t*)realloc(lts->begin,sizeof(u_int32_t)*(states+1));
			if (lts->begin==NULL) Fatal(1,error,"out of memory in lts_set_size");
			break;
		case LTS_LIST:
			break;
	}
	switch(lts->type){
		case LTS_LIST:
		case LTS_BLOCK_INV:
			lts->src=(u_int32_t*)realloc(lts->src,sizeof(u_int32_t)*transitions);
			if (lts->src==NULL&&transitions!=0) Fatal(1,error,"out of memory in lts_set_size");
			break;
		case LTS_BLOCK:
			break;
	}
	switch(lts->type){
		case LTS_LIST:
		case LTS_BLOCK:
		case LTS_BLOCK_INV:
			lts->label=(u_int32_t*)realloc(lts->label,sizeof(u_int32_t)*transitions);
			if (lts->label==NULL&&transitions!=0) Fatal(1,error,"out of memory in lts_set_size");
			break;
	}
	switch(lts->type){
		case LTS_LIST:
		case LTS_BLOCK:
			lts->dest=(u_int32_t*)realloc(lts->dest,sizeof(u_int32_t)*transitions);
			if (lts->dest==NULL&&transitions!=0) Fatal(1,error,"out of memory in lts_set_size");
			break;
		case LTS_BLOCK_INV:
			break;
	}
}

void lts_uniq_sort(lts_t lts){
	uint32_t i,j,k,q,l,d,count,found;
	lts_set_type(lts,LTS_BLOCK);
	count=0;
	for(i=0;i<lts->states;i++){
		j=lts->begin[i];
		lts->begin[i]=count;
		for(;j<lts->begin[i+1];j++){
			l=lts->label[j];
			d=lts->dest[j];
			found=0;
			for(k=count;k>lts->begin[i];k--){
				if (l<lts->label[k-1]) continue;
				if (l==lts->label[k-1]) {
					if (d<lts->dest[k-1]) continue;
					if (d==lts->dest[k-1]) {
						found=1;
						break;
					}
				}
				break;
			}
			if(found) continue;
			for(q=count;q>k;q--){
				lts->label[q]=lts->label[q-1];
				lts->dest[q]=lts->dest[q-1];
			}
			lts->label[k]=l;
			lts->dest[k]=d;
			count++;
		}
	}
	//Warning(1,"count is %d",count);
	lts->begin[lts->states]=count;
	lts_set_size(lts,lts->states,count);
}

void lts_uniq(lts_t lts){
	uint32_t i,j,k,count,oldbegin,found;
	lts_set_type(lts,LTS_BLOCK);
	count=0;
	for(i=0;i<lts->states;i++){
		oldbegin=lts->begin[i];
		lts->begin[i]=count;
		for(j=oldbegin;j<lts->begin[i+1];j++){
			found=0;
			for(k=lts->begin[i];k<count;k++){
				if((lts->label[j]==lts->label[k])&&(lts->dest[j]==lts->dest[k])){
					found=1;
					break;
				}
			}
			if (!found){
				lts->label[count]=lts->label[j];
				lts->dest[count]=lts->dest[j];
				count++;
			}
		}
	}
	lts->begin[lts->states]=count;
	lts_set_size(lts,lts->states,count);
}

void lts_sort_alt(lts_t lts){
	uint32_t i,j,k,l,d;
	int *lbl_index;

	lbl_index=(int*)malloc(lts->label_count*sizeof(int));
	for(i=0;i<lts->label_count;i++){
		lbl_index[i]=-1;
	}
	lts_set_type(lts,LTS_BLOCK);
	k=0;
	for(i=0;i<lts->transitions;i++){
		if (lbl_index[lts->label[i]]==-1){
			lbl_index[lts->label[i]]=k;
			k++;
		}
	}
	for(i=0;i<lts->states;i++){
		for(j=lts->begin[i];j<lts->begin[i+1];j++){
			l=lts->label[j];
			d=lts->dest[j];
			for(k=j;k>lts->begin[i];k--){
				if (lbl_index[lts->label[k-1]]<lbl_index[l]) break;
				if ((lts->label[k-1]==l)&&(lts->dest[k-1]<=d)) break;
				lts->label[k]=lts->label[k-1];
				lts->dest[k]=lts->dest[k-1];
			}
			lts->label[k]=l;
			lts->dest[k]=d;
		}
	}
}

void lts_sort(lts_t lts){
	uint32_t i,j,k,l,d;
	lts_set_type(lts,LTS_BLOCK);
	for(i=0;i<lts->states;i++){
		for(j=lts->begin[i];j<lts->begin[i+1];j++){
			l=lts->label[j];
			d=lts->dest[j];
			for(k=j;k>lts->begin[i];k--){
				if (lts->label[k-1]<l) break;
				if ((lts->label[k-1]==l)&&(lts->dest[k-1]<=d)) break;
				lts->label[k]=lts->label[k-1];
				lts->dest[k]=lts->dest[k-1];
			}
			lts->label[k]=l;
			lts->dest[k]=d;
		}
	}
}


void lts_sort_dest(lts_t lts){
	uint32_t i,j,k,l,d;
	lts_set_type(lts,LTS_BLOCK);
	for(i=0;i<lts->states;i++){
		for(j=lts->begin[i];j<lts->begin[i+1];j++){
			l=lts->label[j];
			d=lts->dest[j];
			for(k=j;k>lts->begin[i];k--){
				if (lts->dest[k-1]<d) break;
				if ((lts->dest[k-1]==d)&&(lts->label[k-1]<=l)) break;
				lts->label[k]=lts->label[k-1];
				lts->dest[k]=lts->dest[k-1];
			}
			lts->label[k]=l;
			lts->dest[k]=d;
		}
	}
}


