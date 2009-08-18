/*
 * $Log: aut-io.c,v $
 * Revision 1.5  2003/02/26 13:09:44  sccblom
 * Added some work related to branching bisimulation and tau*a equivalence.
 * Stefan.
 *
 * Revision 1.4  2002/05/15 12:21:59  sccblom
 * Added tex subdirectory and MPI prototype.
 *
 * Revision 1.3  2002/02/12 13:33:36  sccblom
 * First test version.
 *
 * Revision 1.2  2002/02/08 17:42:15  sccblom
 * Just saving.
 *
 * Revision 1.1  2002/02/08 12:14:40  sccblom
 * Just saving.
 *
 */

#include "aut-io.h"
#include "label.h"
#include "config.h"
#include "messages.h"
#include <ctype.h>
#include <stdlib.h>

static char *cvs_id="$Id: aut-io.c,v 1.5 2003/02/26 13:09:44 sccblom Exp $";
static char *cvs_id_h=AUT_IO_H;

#define BUFFER_SIZE 2048

void readaut(FILE *file,lts_t lts){
	int root,transitions,states,t,from_idx,from_end,label_idx,to_idx,i,from,label,to;
	char buffer[BUFFER_SIZE];
	fscanf(file,"des%*[^(]s");
	fscanf(file,"(%d,%d,%d)\n",&root,&transitions,&states);
	lts_set_type(lts,LTS_LIST);
	lts->root=root;
	lts_set_size(lts,states,transitions);
	for(t=0;t<transitions;t++){
		if (fgets(buffer,BUFFER_SIZE,file)==NULL){
			Fatal(1,1,"fgets error (transition %d)",t+1);
		}

		for(i=strlen(buffer);!isdigit(buffer[i]);i--);
		buffer[i+1]=0;
		for(;isdigit(buffer[i]);i--);
		to_idx=i+1;
		for(;buffer[i]!=',';i--);
		for(i--;isblank(buffer[i]);i--);
		buffer[i+1]=0;

		for(i=0;!isdigit(buffer[i]);i++);
		from_idx=i;
		for(;isdigit(buffer[i]);i++);
		from_end=i;
		for(;buffer[i]!=',';i++);
		for(i++;isblank(buffer[i]);i++);
		buffer[from_end]='\00';
		label_idx=i;

		from=atoi(buffer+from_idx);
		label=getlabelindex(buffer+label_idx,1);
		to=atoi(buffer+to_idx);

		lts->src[t]=from;
		lts->label[t]=label;
		lts->dest[t]=to;
	}
}

/*
 * we map the states of the lts on-the-fly. currently two maps can be chosen:
 *
 * transparent map:
 * #define MAP(s) s
 *
 * forcing root to be state 0: 
 * #define MAP(s) ((s==lts->root)?0:((s==0)?lts->root:s))
 */

#define MAP(s) ((s==lts->root)?0:((s==0)?lts->root:s))

void writeaut(FILE *file,lts_t lts){
	int i,j;

	fprintf(file,"des(%d,%d,%d)\n",MAP(lts->root),lts->transitions,lts->states);
	switch(lts->type){
	case LTS_LIST:
		for(i=0;i<lts->transitions;i++){
			fprintf(file,"(%d,%s,%d)\n",MAP(lts->src[i]),lts->label_string[lts->label[i]],MAP(lts->dest[i]));
		}
		break;
	case LTS_BLOCK:
		for(i=0;i<lts->states;i++){
			for(j=lts->begin[i];j<lts->begin[i+1];j++){
				fprintf(file,"(%d,%s,%d)\n",MAP(i),lts->label_string[lts->label[j]],MAP(lts->dest[j]));
			}
		}
		break;
	case LTS_BLOCK_INV:
		for(i=0;i<lts->states;i++){
			for(j=lts->begin[i];j<lts->begin[i+1];j++){
				fprintf(file,"(%d,%s,%d)\n",MAP(lts->src[j]),lts->label_string[lts->label[j]],MAP(i));
			}
		}
		break;
	default:
		lts_set_type(lts,LTS_LIST);
		writeaut(file,lts);
		break;
	}
}


