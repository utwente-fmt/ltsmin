#include <unistd.h>
#include "messages.h"
#include "lts.h"
#include <string.h>
#include <malloc.h>
#include "dirops.h"
#include "seglts.h"
#include "data_io.h"

int lts_write_segments=0;

lts_t lts_create(){
	lts_t lts=(lts_t)malloc(sizeof(struct lts));
	if (lts==NULL) {
		Fatal(1,1,"out of memory in new_lts");
	}
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
	realloc(lts->begin,0);
	realloc(lts->src,0);
	realloc(lts->label,0);
	realloc(lts->dest,0);
	free(lts);
}

static void build_block(int states,int transitions,u_int32_t *begin,u_int32_t *block,u_int32_t *label,u_int32_t *other){
	int i;
	int loc1,loc2;
	u_int32_t tmp_label1,tmp_label2;
	u_int32_t tmp_other1,tmp_other2;

	for(i=0;i<states;i++) begin[i]=0;
	for(i=0;i<transitions;i++) begin[block[i]]++;
	for(i=1;i<states;i++) begin[i]=begin[i]+begin[i-1];
	for(i=transitions-1;i>=0;i--){
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
	int i,j;

	if (lts->type==type) return; /* no type change */

	/* first change to LTS_LIST */
	switch(lts->type){
		case LTS_LIST:
			lts->begin=(u_int32_t*)malloc(sizeof(u_int32_t)*(lts->states+1));
			if (lts->begin==NULL) Fatal(1,1,"out of memory in lts_set_type");
			break;
		case LTS_BLOCK:
			lts->src=(u_int32_t*)malloc(sizeof(u_int32_t)*(lts->transitions));
			if (lts->src==NULL) Fatal(1,1,"out of memory in lts_set_type");
			for(i=0;i<lts->states;i++){
				for(j=lts->begin[i];j<lts->begin[i+1];j++){
					lts->src[j]=i;
				}
			}
			break;
		case LTS_BLOCK_INV:
			lts->dest=(u_int32_t*)malloc(sizeof(u_int32_t)*(lts->transitions));
			if (lts->dest==NULL) Fatal(1,1,"out of memory in lts_set_type");
			for(i=0;i<lts->states;i++){
				for(j=lts->begin[i];j<lts->begin[i+1];j++){
					lts->dest[j]=i;
				}
			}
			break;
	}
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
			if (lts->begin==NULL) Fatal(1,1,"out of memory in lts_set_size");
	}
	switch(lts->type){
		case LTS_LIST:
		case LTS_BLOCK_INV:
			lts->src=(u_int32_t*)realloc(lts->src,sizeof(u_int32_t)*(transitions+1));
			if (lts->src==NULL) Fatal(1,1,"out of memory in lts_set_size");
	}
	switch(lts->type){
		case LTS_LIST:
		case LTS_BLOCK:
		case LTS_BLOCK_INV:
			lts->label=(u_int32_t*)realloc(lts->label,sizeof(u_int32_t)*(transitions+1));
			if (lts->label==NULL) Fatal(1,1,"out of memory in lts_set_size");
	}
	switch(lts->type){
		case LTS_LIST:
		case LTS_BLOCK:
			lts->dest=(u_int32_t*)realloc(lts->dest,sizeof(u_int32_t)*(transitions+1));
			if (lts->dest==NULL) Fatal(1,1,"out of memory in lts_set_size");
	}
}

void lts_uniq(lts_t lts){
	int i,j,k,count,oldbegin,found;
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
	int i,j,k,l,d;
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
	int i,j,k,l,d;
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

void lts_bfs_reorder(lts_t lts) {
	int i,j,k;
	int *map,*repr;

	Warning(1,"starting BFS reordering");
	lts_set_type(lts,LTS_BLOCK);
	Warning(2,"sorted lts into blocked format");
	map=(int*)malloc(lts->states*sizeof(int));
	repr=(int*)malloc(lts->states*sizeof(int));
	for(i=0;i<lts->states;i++){
		map[i]=-1;
	}
	repr[0]=lts->root;
	map[lts->root]=0;
	i=0;
	j=1;
	while(i<j){
		for(k=lts->begin[repr[i]];k<lts->begin[repr[i]+1];k++){
			if (map[lts->dest[k]]==-1) {
				map[lts->dest[k]]=j;
				repr[j]=lts->dest[k];
				j++;
			}
		}
		i++;
	}
	free(repr);
	Warning(2,"created map");
	lts_set_type(lts,LTS_LIST);
	Warning(2,"transformed into list representation");
	for(i=0;i<lts->transitions;i++){
		lts->src[i]=map[lts->src[i]];
		lts->dest[i]=map[lts->dest[i]];
	}
	lts->root=0;
	free(map);
	Warning(2,"applied map");
	lts_set_type(lts,LTS_BLOCK);
	Warning(2,"sorted lts into blocked format");
}

static int pass1_dfs_count;

static void pass1_dfs(lts_t lts,int tau,u_int32_t *e_time,int *time,int state){
	int i;
	if (e_time[state]>0) return;
	pass1_dfs_count++;
	e_time[state]=1;
	for(i=lts->begin[state];i<lts->begin[state+1];i++){
		if (lts->label[i]==tau) pass1_dfs(lts,tau,e_time,time,lts->dest[i]);
	}
	(*time)++;
	e_time[state]=(*time);
}

static void pass2_dfs(lts_t lts,int tau,u_int32_t *map,int component,int state){
	int i;
	if(map[state]>0) return;
	map[state]=component;
	for(i=lts->begin[state];i<lts->begin[state+1];i++){
		if (lts->label[i]==tau) pass2_dfs(lts,tau,map,component,lts->dest[i]);
	}
}

void lts_tau_cycle_elim(lts_t lts){
	int i,j,k,time,tmp,component,count,s,l,d,max,tau;
	u_int32_t *map;

	tau=lts->tau;
	/* mark with exit times */
	lts_set_type(lts,LTS_BLOCK);
	map=(u_int32_t*)malloc(sizeof(u_int32_t)*lts->states);
	for(i=0;i<lts->states;i++) {
		map[i]=0;
	}
	time=1;
	max=0;
	for(i=0;i<lts->states;i++){
		pass1_dfs_count=0;
		pass1_dfs(lts,tau,map,&time,i);
		if (pass1_dfs_count) Warning(3,"tau compenent has size %d",pass1_dfs_count);
		if (pass1_dfs_count>max) max=pass1_dfs_count;
	}
	Warning(2,"worst tau component has size %d",max);
	/* renumber: highest exit time means lowest number */
	/* at the same time reverse direction of edges */
	lts_set_type(lts,LTS_LIST);
	lts->root=time-map[lts->root];
	for(i=0;i<lts->transitions;i++){
		tmp=lts->src[i];
		lts->src[i]=time-map[lts->dest[i]];
		lts->dest[i]=time-map[tmp];
	}
	/* mark components */
	lts_set_type(lts,LTS_BLOCK);
	for(i=0;i<lts->states;i++){
		map[i]=0;
	}
	component=0;
	for(i=0;i<lts->states;i++){
		if(map[i]==0){
			component++;
			pass2_dfs(lts,tau,map,component,i);
		}
	}
	/* divide out equivalence classes reverse direction of edges again */
	lts_set_type(lts,LTS_LIST);
	lts->root=map[lts->root]-1;
	count=0;
	for(i=0;i<lts->transitions;i++){
		d=map[lts->src[i]]-1;
		s=map[lts->dest[i]]-1;
		l=lts->label[i];
		if ((l==tau)&&(s==d)) {
			continue;
		}
		lts->src[count]=s;
		lts->label[count]=l;
		lts->dest[count]=d;
		count++;
		if ((l==tau)&&(s>d)){
			Fatal(1,0,"tau from high to low");
		}
	}
	Warning(2,"all tau steps from low to high");
	lts_set_size(lts,component,count);
	free(map);
	lts_uniq(lts);
}

static int normalize(int *map,int i){
	int old;
	if (map[i]==i) return i;

	old=map[i];
	map[i]=i;
	map[i]=normalize(map,old);
	return map[i];
}

void lts_tau_indir_elim(lts_t lts){
	int i,j,count,*map,tau,scount,tcount;

	tau=lts->tau;
	lts_set_type(lts,LTS_BLOCK);
	map=(int*)malloc(lts->states*sizeof(int));
	for(i=0;i<lts->states;i++){
		if((lts->begin[i+1]-lts->begin[i])==1 && lts->label[lts->begin[i]]==tau){
			lts->label[lts->begin[i]]=-1;
			map[i]=lts->dest[lts->begin[i]];
		} else {
			map[i]=i;
		}
	}
	for(i=0;i<lts->states;i++){
		map[i]=normalize(map,i);
	}
	lts_set_type(lts,LTS_LIST);
	lts->root=map[lts->root];
	for(i=0;i<lts->transitions;i++){
		lts->dest[i]=map[lts->dest[i]];
		lts->src[i]=map[lts->src[i]];
		if ((lts->label[i]==tau) && (lts->src[i]==lts->dest[i])) lts->label[i]=-1;
	}
	for(i=0;i<lts->states;i++){
		map[i]=0;
	}
	map[lts->root]=1;
	for(i=0;i<lts->transitions;i++){
		if (lts->label[i]!=-1) {
			map[lts->dest[i]]=1;
		}
	}
	scount=0;
	for(i=0;i<lts->states;i++){
		if (map[i]==1){
			map[i]=scount;
			scount++;
		}
	}
	lts->root=map[lts->root];
	tcount=0;
	for(i=0;i<lts->transitions;i++){
		if (lts->label[i]!=-1){
			lts->dest[tcount]=map[lts->dest[i]];
			lts->label[tcount]=lts->label[i];
			lts->src[tcount]=map[lts->src[i]];
			tcount++;
		}
	}
	lts_set_size(lts,scount,tcount);
	free(map);
}


/* AUT IO */

#include "aut-io.h"
#include "label.h"

static void lts_read_aut(char *name,lts_t lts){
	FILE* file;
	int i,count;

	file=fopen(name,"r");
	if (file == NULL) {
		FatalCall(1,1,"failed to open %s for reading",name);
	}
	readaut(file,lts);
	count=getlabelcount();
	lts->label_string=malloc(count*sizeof(char*));
	for(i=0;i<count;i++){
		lts->label_string[i]=getlabelstring(i);
	}
	if (lts->tau<0) lts->tau=getlabelindex("\"tau\"",0);
	if (lts->tau<0) lts->tau=getlabelindex("tau",0);
	if (lts->tau<0) lts->tau=getlabelindex("i",0);
	lts->label_count=count;
	fclose(file);
}

static void lts_write_aut(char *name,lts_t lts){
	FILE* file=fopen(name,"w");
	if (file == NULL) {
		FatalCall(1,1,"could not open %s for output",name);
	}
	writeaut(file,lts);
	fclose(file);
}

static int lts_guess_format(char *name){
	char *lastdot=strrchr(name,'.');
	if(!lastdot) Fatal(1,1,"filename %s has no extension",name);
	lastdot++;
	if (!strcmp(lastdot,"aut")) return LTS_AUT;
#ifdef USE_BCG
	if (!strcmp(lastdot,"bcg")) return LTS_BCG;
#endif
#ifdef USE_SVC
	if (!strcmp(lastdot,"svc")) return LTS_SVC;
#endif
	if (!strcmp(lastdot,"dir")) return LTS_DIR;
	Fatal(1,1,"unknown extension %s",lastdot);
	return 6987623; // This statement should not be reachable!
}


/* BCG IO */
#ifdef USE_BCG

#include "bcg_user.h"

static void lts_read_bcg(char *name,lts_t lts){
	BCG_TYPE_OBJECT_TRANSITION bcg_graph;
	bcg_type_state_number bcg_s1;
	BCG_TYPE_LABEL_NUMBER bcg_label_number;
	bcg_type_state_number bcg_s2;
	int i;

	BCG_OT_READ_BCG_BEGIN (name, &bcg_graph, 0);
	lts_set_type(lts,LTS_LIST);
	Warning(2," %d states %d transitions",BCG_OT_NB_STATES (bcg_graph),BCG_OT_NB_EDGES (bcg_graph));
	lts_set_size(lts,BCG_OT_NB_STATES (bcg_graph),BCG_OT_NB_EDGES (bcg_graph));
	lts->root=BCG_OT_INITIAL_STATE (bcg_graph);
	lts->label_count=BCG_OT_NB_LABELS (bcg_graph);
	lts->label_string=malloc(lts->label_count*sizeof(char*));
	for(i=0;i<lts->label_count;i++){
		lts->label_string[i]=BCG_OT_LABEL_STRING (bcg_graph,i);
		if (!BCG_OT_LABEL_VISIBLE (bcg_graph,i)){
			if (lts->tau==-1) {
				lts->tau=i;
			} else {
				Fatal(1,1,"more than one invisible label");
			}
		}
	}
	i=0;
	BCG_OT_ITERATE_PLN (bcg_graph,bcg_s1,bcg_label_number,bcg_s2){
		lts->src[i]=bcg_s1;
		lts->label[i]=bcg_label_number;
		lts->dest[i]=bcg_s2;
		i++;
	} BCG_OT_END_ITERATE;
	BCG_OT_READ_BCG_END (&bcg_graph);
}

static void lts_write_bcg(char *name,lts_t lts){
	int i,j,l;

	lts_set_type(lts,LTS_BLOCK);
	BCG_IO_WRITE_BCG_BEGIN (name,lts->root,2,"bsim2",0);
	for(i=0;i<lts->states;i++) for(j=lts->begin[i];j<lts->begin[i+1];j++){
		l=lts->label[j];
		BCG_IO_WRITE_BCG_EDGE (i,(l==lts->tau)?"i":(lts->label_string[l]),lts->dest[j]);
	}
	BCG_IO_WRITE_BCG_END ();
}

#endif
/* BCG IO END */

static void lts_write_dir(char *name,lts_t lts){
	seginfo_t info;
	char filename[1024];
	int i,j,k;
	FILE *output;
	FILE **src_out;
	FILE **lbl_out;
	FILE **dst_out;

	if (lts_write_segments==0) lts_write_segments=1;
	Warning(1,"writing %s with %d segment(s)",name,lts_write_segments);
	lts_set_type(lts,LTS_BLOCK);
	SLTSCreateInfo(&info,lts_write_segments);
	info->label_tau=lts->tau;
	info->label_count=lts->label_count;
	info->initial_seg=lts->root%lts_write_segments;
	info->initial_ofs=lts->root/lts_write_segments;
	CreateEmptyDir(name,DELETE_ALL);
	sprintf(filename,"%s/TermDB",name);
	output=fopen(filename,"w");
	for(i=0;i<lts->label_count;i++){
		fprintf(output,"%s\n",lts->label_string[i]);
	}
	fclose(output);
	src_out=(FILE**)malloc(lts_write_segments*sizeof(FILE*));
	lbl_out=(FILE**)malloc(lts_write_segments*sizeof(FILE*));
	dst_out=(FILE**)malloc(lts_write_segments*sizeof(FILE*));
	for(i=0;i<lts_write_segments;i++) {
		for(j=0;j<lts_write_segments;j++) {
			sprintf(filename,"%s/src-%d-%d",name,i,j);
			src_out[j]=fopen(filename,"w");
			sprintf(filename,"%s/label-%d-%d",name,i,j);
			lbl_out[j]=fopen(filename,"w");
			sprintf(filename,"%s/dest-%d-%d",name,i,j);
			dst_out[j]=fopen(filename,"w");
		}
		for(j=i;j<lts->states;j+=lts_write_segments){
			for(k=lts->begin[j];k<lts->begin[j+1];k++){
				int dseg=(lts->dest[k])%lts_write_segments;
				info->transition_count[i][dseg]++;
				fwrite32(src_out[dseg],info->state_count[i]);
				fwrite32(lbl_out[dseg],lts->label[k]);
				fwrite32(dst_out[dseg],(lts->dest[k])/lts_write_segments);
			}
			info->state_count[i]++;
		}
		for(j=0;j<lts_write_segments;j++) {
			fclose(src_out[j]);
			fclose(lbl_out[j]);
			fclose(dst_out[j]);
		}
	}
	info->info="bsim2 output";
	SLTSWriteInfo(info,name);
}

#include "dlts.h"
static void lts_read_dir(char *name,lts_t lts) {
	dlts_t dlts;
	int i,j,k,s_count,t_count,*offset,t_offset;

	dlts=dlts_create();
	dlts->dirname=name;
	dlts_getinfo(dlts);
	if (lts_write_segments==0) {
		lts_write_segments=dlts->segment_count;
		Warning(1,"setting lts_write_segments to %d",dlts->segment_count);
	}
	s_count=0;
	t_count=0;
	offset=(int*)calloc(dlts->segment_count,sizeof(int));
	for(i=0;i<dlts->segment_count;i++){
		offset[i]=s_count;
		s_count+=dlts->state_count[i];
		for(j=0;j<dlts->segment_count;j++){
			t_count+=dlts->transition_count[i][j];
		}
	}
	lts_set_type(lts,LTS_LIST);
	lts_set_size(lts,s_count,t_count);
	lts->root=offset[dlts->root_seg]+dlts->root_ofs;
	lts->tau=dlts->tau;

	dlts_getTermDB(dlts);
	lts->label_count=dlts->label_count;
	lts->label_string=malloc(lts->label_count*sizeof(char*));
	for(i=0;i<lts->label_count;i++){
		lts->label_string[i]=strdup(dlts->label_string[i]);
	}

	t_offset=0;
	for(i=0;i<dlts->segment_count;i++){
		for(j=0;j<dlts->segment_count;j++){
			dlts_load_src(dlts,i,j);
			dlts_load_label(dlts,i,j);
			dlts_load_dest(dlts,i,j);
			for(k=0;k<dlts->transition_count[i][j];k++){
				lts->src[t_offset+k]=offset[i]+dlts->src[i][j][k];
				lts->label[t_offset+k]=dlts->label[i][j][k];
				lts->dest[t_offset+k]=offset[j]+dlts->dest[i][j][k];
			}
			t_offset+=dlts->transition_count[i][j];
			dlts_free_src(dlts,i,j);
			dlts_free_label(dlts,i,j);
			dlts_free_dest(dlts,i,j);
		}
	}

	dlts_free(dlts);
}

#ifdef USE_SVC
#include <svc.h>
#include <aterm2.h>

void* lts_stack_bottom=NULL;
int lts_aterm_init_done=0;

static void lts_read_svc(char *name,lts_t lts){
	SVCfile inFile;
	SVCbool indexed;
	SVCstateIndex fromState, toState;
	SVClabelIndex label;
	SVCparameterIndex parameter;
	int i;

	if(lts_aterm_init_done==0) ATinit(0,NULL,lts_stack_bottom);

	lts_set_type(lts,LTS_LIST);
	SVCopen(&inFile, name, SVCread, &indexed);
	lts_set_size(lts,SVCnumStates(&inFile),SVCnumTransitions(&inFile));
	lts->root=SVCgetInitialState(&inFile);
	lts->label_count=SVCnumLabels(&inFile);
	lts->label_string=(char**)malloc(SVCnumLabels(&inFile)*sizeof(char*));
	i=0;
	while (SVCgetNextTransition(&inFile, &fromState, &label, &toState, &parameter)){
		lts->src[i]=fromState;
		lts->label[i]=label;
		lts->dest[i]=toState;
		i++;
	}
	for(i=0;i<SVCnumLabels(&inFile);i++){
		lts->label_string[i]=strdup(ATwriteToString(SVClabel2ATerm(&inFile,i)));
		if(!strcmp("i",lts->label_string[i])){
			lts->tau=i;
		}
	}
	if (SVCclose(&inFile)<0){
		Fatal(1,1, "SVC file trailer corrupt");
	}
}

static void lts_write_svc(char *name,lts_t lts){	
	SVCbool indexed=SVCtrue;
	SVCfile outFile;
	SVCparameterIndex parameter;
	SVClabelIndex label;
	SVCstateIndex fromState, toState;
	SVCbool       _new;
	int i,j,lbl_i;
	char *lbl_s;

	if(lts_aterm_init_done==0) ATinit(0,NULL,lts_stack_bottom);

	lts_set_type(lts,LTS_BLOCK);
	SVCopen(&outFile,name,SVCwrite,&indexed);
	parameter=SVCnewParameter(&outFile, (ATerm)ATmakeList0(), &_new);
	SVCsetCreator(&outFile, "lts");
	SVCsetInitialState(&outFile, SVCnewState(&outFile, (ATerm)ATmakeInt(lts->root), &_new));
	for(i=0;i<lts->states;i++){
                fromState=SVCnewState(&outFile, (ATerm)ATmakeInt(i), &_new);
		for(j=lts->begin[i];j<lts->begin[i+1];j++){
			lbl_i=lts->label[j];
			if(lbl_i==lts->tau){
				lbl_s="i";
			} else {
				lbl_s=lts->label_string[lbl_i];
			}
			toState=SVCnewState(&outFile, (ATerm)ATmakeInt(lts->dest[j]), &_new);
			label=SVCnewLabel(&outFile, (ATerm)ATmakeAppl(ATmakeAFun(lbl_s,0,ATfalse)), &_new);
			SVCputTransition(&outFile, fromState, label, toState, parameter);
		}
	}
	SVCclose(&outFile);
}


#endif

void lts_read(int format,char *name,lts_t lts){
	if (format==LTS_AUTO) format=lts_guess_format(name);

	switch(format){
	case LTS_AUT:
		lts_read_aut(name,lts);
		break;
#ifdef USE_BCG
	case LTS_BCG:
		lts_read_bcg(name,lts);
		break;
#endif
	case LTS_DIR:
		lts_read_dir(name,lts);
		break;
#ifdef USE_SVC
	case LTS_SVC:
		lts_read_svc(name,lts);
		break;
#endif	default:
		Fatal(1,1,"illegal format");
	}
}

void lts_write(int format,char *name,lts_t lts){
	if (format==LTS_AUTO) format=lts_guess_format(name);
	switch(format){
	case LTS_AUT:
		lts_write_aut(name,lts);
		break;
#ifdef USE_BCG
	case LTS_BCG:
		lts_write_bcg(name,lts);
		break;
#endif
	case LTS_DIR:
		lts_write_dir(name,lts);
		break;
#ifdef USE_SVC
	case LTS_SVC:
		lts_write_svc(name,lts);
		break;
#endif
	default:
		Fatal(1,1,"illegal format");
	}
}


