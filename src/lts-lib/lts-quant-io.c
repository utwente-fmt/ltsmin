// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>

#include <stdlib.h>
#include <unistd.h>

#include <hre/user.h>
#include <util-lib/rationals.h>
#include <lts-lib/lts.h>
#include <hre/stringindex.h>

#define STATE_FMT "S%06u"

static char* tra_get_lab(const char *name){
    char *lab_name=strdup(name);
    char *tra=strstr(lab_name,".tra");
    char *tmp;
    while((tmp=strstr(tra+1,".tra"))){
        tra=tmp;
    }
    tra[1]='l';
    tra[2]='a';
    tra[3]='b';
    return lab_name;
}

static char* tra_get_sta(const char *name){
    char *sta_name=strdup(name);
    char *tra=strstr(sta_name,".tra");
    char *tmp;
    while((tmp=strstr(tra+1,".tra"))){
        tra=tmp;
    }
    tra[1]='s';
    tra[2]='t';
    tra[3]='a';
    return sta_name;
}

static void write_imca_trans(FILE* imca,const char* state_fmt,lts_t lts,uint32_t first){
    int reward_pos=lts_type_find_edge_label(lts->ltstype,"reward_numerator");
    int action_pos=lts_type_find_edge_label(lts->ltstype,"action");
    if (action_pos<0) Abort("imca format requires actions");
    int group_pos=lts_type_find_edge_label(lts->ltstype,"group");
    int param_pos=lts_type_find_edge_label(lts->ltstype,"numerator");
    int NE=lts_type_get_edge_label_count(lts->ltstype);

    int action_type=lts_type_get_edge_label_typeno(lts->ltstype,action_pos);
    uint32_t tau=(uint32_t)VTputChunk(lts->values[action_type],chunk_str("tau"));
    Warning(info,"tau = %u",tau);
    uint32_t rate=(uint32_t)VTputChunk(lts->values[action_type],chunk_str("rate"));
    Warning(info,"rate = %u",rate);

    for(uint32_t i=0;i<lts->states;i++){
        for(uint32_t j=lts->begin[i];j<lts->begin[i+1];){
            uint32_t label[NE];
            TreeUnfold(lts->edge_idx,lts->label[j],(int*)label);
            uint32_t group=label[group_pos];
            if (label[action_pos]==tau){
                fprintf(imca,state_fmt,first+i);
                fprintf(imca," tau");
            } else if (label[action_pos]==rate) {
                if(j==lts->begin[i]){
                    fprintf(imca,state_fmt,first+i);
                    fprintf(imca," !");
                }
            } else {
                chunk label_c=VTgetChunk(lts->values[action_type],label[action_pos]);
                char label_s[label_c.len*2+6];
                chunk2string(label_c,sizeof label_s,label_s);
                fprintf(imca,state_fmt,first+i);
                fprintf(imca," %s",label_s);
            }
            if (reward_pos>=0 && label[reward_pos]!=0){
                fprintf(imca," %.15e\n",((float)label[reward_pos])/(float)label[reward_pos+1]);
            } else if((label[action_pos]==rate && j==lts->begin[i]) || (label[action_pos]!=rate)) {
                fprintf(imca," \n");
            }
            do {
                fprintf(imca,"* ");
                fprintf(imca,state_fmt,first+lts->dest[j]);
                fprintf(imca," %.15e\n",((float)label[param_pos])/(float)label[param_pos+1]);
                j++;
                if (j<lts->begin[i+1])
                    TreeUnfold(lts->edge_idx,lts->label[j],(int*)label);
            } while (j<lts->begin[i+1]&&group==label[group_pos]);
        }
        //fprintf(imca,"\n");
    }

}


#define BUFFER_SIZE 100000

void lts_read_tra(const char*tra_name,lts_t lts){
    char *lab_name=tra_get_lab(tra_name);
    char *sta_name=tra_get_sta(tra_name);
    int lab_test=access(lab_name,R_OK)==0;
    int sta_test=access(sta_name,R_OK)==0;

    if (lab_test && sta_test){
        Abort("Both %s and %s exist. Please remove one of them",lab_name,sta_name);
    }
    if (!lab_test && !sta_test){
        Abort("Neither %s nor %s exist.",lab_name,sta_name);
    }

    lts_set_type(lts,LTS_LIST);

    Print(infoShort,"reading %s/%s",tra_name,lab_test?lab_name:sta_name);
    FILE* tra=fopen(tra_name,"r");
    if (tra == NULL) {
        AbortCall("Could not open %s for reading",tra_name);
    }
    FILE* lab=fopen(lab_test?lab_name:sta_name,"r");
    if (lab == NULL) {
        AbortCall("Could not open %s for reading",lab_name);
    }
    char buffer[BUFFER_SIZE];
    string_index_t props=SIcreate();
    if (fgets(buffer,BUFFER_SIZE, lab)==NULL) AbortCall("read error");
    int no;
    char *entry,*next;
    char name[4096];
    uint32_t first_state;
    if (lab_test) {
        Print(infoShort,"read label header");
        if(sscanf(buffer,"%d",&no)){
            Print(infoShort,"assuming prism format");
            first_state=0;
            for(entry=buffer;entry;entry=next){
                next=strchr(entry,' ');
                if(next) { *next=0; next++; }
                sscanf(entry,"%d=%s",&no,(char*)&name);
                if (no<0) Abort("negative label");
                name[strlen(name)-1]=0;
                Print(infoShort,"label %d is %s",no,name+1);
                SIputAt(props,name+1,no);
                if (no==0 && strcmp(name+1,"init")) {
                    Abort ("label 0 is reserved for init");
                }
            }
            no=SIgetCount(props);
        } else {
            Print(infoShort,"assuming mrmc format");
            first_state=1;
            if (fgets(buffer,BUFFER_SIZE, lab)==NULL) AbortCall("read error");
            no=0;
            for(entry=buffer;entry;entry=next){
                next=strchr(entry,' ');
                if(next) { *next=0; next++; }
                sscanf(entry,"%s",(char*)&name);
                Print(infoShort,"label %d is %s",no,name);
                if (no==0 && strcmp(name,"init")) {
                    Abort("label 0 is reserved for init");
                }
                SIputAt(props,name,no);
                no++;
            }
            if (fgets(buffer,BUFFER_SIZE, lab)==NULL) AbortCall("read error");
        }
    } else {
        Print(infoShort,"read state header");
        first_state=0;
        no=0;
        for(entry=buffer+1;entry;entry=next){
            next=strchr(entry,',');
            if(next) {
                *next=0;
                next++;
            } else {
                next=strchr(entry,')');
                *next=0;
                next=NULL;
            }
            Print(infoShort,"state var %d is %s",no,entry);
            SIputAt(props,entry,no);
            no++;
        }
        no=SIgetCount(props);
    }
    Print(infoShort,"read transition header");
    uint32_t states;
    uint32_t transitions;
    uint32_t edges;
    int items;
    if (fgets(buffer,BUFFER_SIZE, tra)==NULL) Abort("read error");
    if (sscanf(buffer,"STATES%u",&states)){
        if (first_state==0) Abort("tra and lab in different formats");
        if (fgets(buffer,BUFFER_SIZE, tra)==NULL) Abort("read error");
        sscanf(buffer,"TRANSITIONS%u",&transitions);
        items=2;
    } else {
        if (first_state==1) Abort("tra and lab in different formats");
        items=sscanf(buffer,"%u %u %u",&states,&transitions,&edges);
        if (items==3){
            Print(infoShort,"%u transition groups",transitions);
            transitions=edges;
        }
    }

    lts_type_t ltstype=lts_type_create();
    if (lab_test){
        int booltype=lts_type_put_type(ltstype,"bool",LTStypeRange,NULL);
        lts_type_set_range(ltstype,booltype,0,1);
    } else {
        lts_type_put_type(ltstype,"int",LTStypeDirect,NULL);
    }
    lts_type_put_type(ltstype,"nat",LTStypeDirect,NULL);
    lts_type_put_type(ltstype,"pos",LTStypeDirect,NULL);
    lts_type_set_state_label_count(ltstype,no);
    for(int i=0;i<no;i++){
        lts_type_set_state_label_name(ltstype,i,SIget(props,i));
        lts_type_set_state_label_type(ltstype,i,lab_test?"bool":"int");
    }
    lts_type_set_edge_label_count(ltstype,items);
    if (items==2){
        Print(infoShort,"input is xTMC");
        lts_type_set_edge_label_name(ltstype,0,"numerator");
        lts_type_set_edge_label_type(ltstype,0,"nat");
        lts_type_set_edge_label_name(ltstype,1,"denominator");
        lts_type_set_edge_label_type(ltstype,1,"pos");
    } else {
        Print(infoShort,"input is MDP");
        lts_type_set_edge_label_name(ltstype,0,"group");
        lts_type_set_edge_label_type(ltstype,0,"nat");
        lts_type_set_edge_label_name(ltstype,1,"numerator");
        lts_type_set_edge_label_type(ltstype,1,"nat");
        lts_type_set_edge_label_name(ltstype,2,"denominator");
        lts_type_set_edge_label_type(ltstype,2,"pos");
    }
    lts_set_sig(lts,ltstype);
    lts_set_size(lts,1,states,transitions);
    Print(infoShort,"%u states %u transitions",states,transitions);


    Print(infoShort,"reading transitions");
    for(uint32_t i=0;i<transitions;i++){
        if (fgets(buffer,BUFFER_SIZE, tra)==NULL) Abort("read error");
        uint32_t src,dst,labels[3];
        float rate;
        if (items==2){
            sscanf(buffer,"%u %u %f",&src,&dst,&rate);
            rationalize32(rate,labels,labels+1);
        } else {
            sscanf(buffer,"%u %u %u %f",&src,labels,&dst,&rate);
            rationalize32(rate,labels+1,labels+2);
        }
        src-=first_state;
        dst-=first_state;
        lts->src[i]=src;
        lts->dest[i]=dst;
        lts->label[i]=TreeFold(lts->edge_idx,(int32_t*)labels);
    }


    uint32_t labels[no];
    if (lab_test){
        Print(infoShort,"reading state labels");
        for(int j=0;j<no;j++) labels[j]=0;
        if (TreeFold(lts->prop_idx,(int*)labels)!=0) Abort("no labels not 0");
        for(uint32_t i=0;i<states;i++){
            lts->properties[i]=0;
        }
        while(fgets(buffer,BUFFER_SIZE, lab)){
            for(int j=0;j<no;j++) labels[j]=0;
            uint32_t state;
            if (first_state) {
                sscanf(buffer,"%d",&state);
                state--;
                for(entry=strchr(buffer,' ')+1;entry;entry=next){
                    next=strchr(entry,' ');
                    if(next) { *next=0; next++; }
                    sscanf(entry,"%s",(char*)&name);
                    int tmp=SIlookup(props,name);
                    if (tmp<0) Abort("bad label %s",name);
                    labels[tmp]=1;
                    if (tmp==0) lts->root_list[0]=state;
                }
            } else {
                sscanf(buffer,"%d:",&state);
                for(entry=strchr(buffer,' ')+1;entry;entry=next){
                    next=strchr(entry,' ');
                    if(next) { *next=0; next++; }
                    int tmp;
                    sscanf(entry,"%d ",&tmp);
                    if (tmp<0 || tmp>=no) Abort("bad label %d",tmp);
                    labels[tmp]=1;
                    if (tmp==0) lts->root_list[0]=state;
                }
            }
            lts->properties[state]=TreeFold(lts->prop_idx,(int*)labels);
        }
        if (!feof(lab)) Abort("read error");
    } else {
        Print(infoShort,"reading state vectors");
        for(uint32_t i=0;i<states;i++){
            if(NULL==fgets(buffer,BUFFER_SIZE, lab)) AbortCall("read error on state %d",i)
            uint32_t state;
            sscanf(buffer,"%d:",&state);
            if (state!=i) Abort("state mismatch: %u != %u",state,i);
            for (int j=0;j<no;j++){
                if (j==0){
                    entry=strchr(buffer,'(')+1;
                } else {
                    entry=strchr(entry,',')+1;
                }
                sscanf(entry,"%d",&labels[j]);
            }
            lts->properties[i]=TreeFold(lts->prop_idx,(int*)labels);
        }
    }

    fclose(tra);
    fclose(lab);
    free(lab_name); //strstr
}

static int storm_edge_gte(lts_t lts,int k1,int k2){
    // lexicographic order.
    // first compare labels.
    int l1=TreeDBSGet(lts->edge_idx,lts->label[k1],0);
    int l2=TreeDBSGet(lts->edge_idx,lts->label[k2],0);
    if (l1<l2) return 0;
    // then compare hyper edge group.
    int g1=TreeDBSGet(lts->edge_idx,lts->label[k1],1);
    int g2=TreeDBSGet(lts->edge_idx,lts->label[k2],1);
    if (g1<g2) return 0;
    // finally compare destination state number.
    return lts->dest[k1]>=lts->dest[k2];
}

void lts_write_tra(const char*tra_name,lts_t lts){
    char *lab_name=tra_get_lab(tra_name);
    Print(infoShort,"writing %s/%s",tra_name,lab_name);
    FILE* tra=fopen(tra_name,"w");
    if (tra == NULL) {
        AbortCall("Could not open %s for writing",tra_name);
    }
    FILE* lab=fopen(lab_name,"w");
    if (lab == NULL) {
        AbortCall("Could not open %s for writing",lab_name);
    }
    lts_set_type(lts,LTS_BLOCK);
    if (lts_type_get_edge_label_count(lts->ltstype)==2){
        // The funny thing with TRA is that it needs both src and dst sorted.
        // sort transitions (bubble sort)
        for(uint32_t i=0;i<lts->states;i++){
            for(uint32_t m=lts->begin[i];m<lts->begin[i+1];m++){
                for(uint32_t k=m;k>lts->begin[i];k--){
                    if(lts->dest[k]>=lts->dest[k-1]) break;
                    uint32_t tmp=lts->label[k];
                    lts->label[k]=lts->label[k-1];
                    lts->label[k-1]=tmp;
                    tmp=lts->dest[k];
                    lts->dest[k]=lts->dest[k-1];
                    lts->dest[k-1]=tmp;
                }
            }
        }
        fprintf(tra,"STATES %d\n",lts->states);
        fprintf(tra,"TRANSITIONS %d\n",lts->transitions);
        for(uint32_t i=0;i<lts->states;i++){
            for(uint32_t m=lts->begin[i];m<lts->begin[i+1];m++){
                uint32_t rate[2];
                TreeUnfold(lts->edge_idx,lts->label[m],(int*)rate);
                fprintf(tra,"%d %d %g\n",i+1,lts->dest[m]+1,((double)rate[0])/((double)rate[1]));
            }
        }
    } else {
        // storm format sort transitions (bubble sort)
        for(uint32_t i=0;i<lts->states;i++){
            for(uint32_t m=lts->begin[i];m<lts->begin[i+1];m++){
                for(uint32_t k=m;k>lts->begin[i];k--){
                    if(storm_edge_gte(lts,k,k-1)) break;
                    uint32_t tmp=lts->label[k];
                    lts->label[k]=lts->label[k-1];
                    lts->label[k-1]=tmp;
                    tmp=lts->dest[k];
                    lts->dest[k]=lts->dest[k-1];
                    lts->dest[k-1]=tmp;
                }
            }
        }
        fprintf(tra,"STATES %d\n",lts->states);
        fprintf(tra,"TRANSITIONS %d\n",lts->transitions);
        write_imca_trans(tra,"%u",lts,1);
    }
    fclose(tra);
    int N=lts_type_get_state_label_count(lts->ltstype);
    fprintf(lab,"#DECLARATION\n");
    if (lts_type_get_edge_label_count(lts->ltstype)!=2){
        fprintf(lab,"init ");
    }
    for(int i=0;i<N;i++){
        fprintf(lab,i?" %s":"%s",lts_type_get_state_label_name(lts->ltstype,i));
    }
    fprintf(lab,"\n#END\n");
    int label[N];
    for(uint32_t i=0;i<lts->states;i++){
        switch(N){
            case 0:
                break;
            case 1:
                label[0]=lts->properties[i];
                break;
            default:
                TreeUnfold(lts->prop_idx,lts->properties[i],label);
        }
        int first=1;
        if (lts_type_get_edge_label_count(lts->ltstype)!=2){
            for(uint32_t j=0;j<lts->root_count;j++){
                if (lts->root_list[j]==i){
                    fprintf(lab,"%u init",i+1);
                    first=0;
                    break;
                }
            }
        }
        for(int j=0;j<N;j++){
            if(label[j]){
                if(first){
                    fprintf(lab,"%u",i+1);
                    first=0;
                }
                fprintf(lab," %s",lts_type_get_state_label_name(lts->ltstype,j));
            }
        }
        if(!first) fprintf(lab,"\n");
    }
    fclose(lab);
    free(lab_name); // strstr
}

#define IMCA_STATE_FMT "S%06u"
void lts_write_imca(const char*imca_name,lts_t lts){
    FILE* imca=fopen(imca_name,"w");
    if (imca == NULL) {
        AbortCall("Could not open %s for writing",imca_name);
    }
    lts_set_type(lts,LTS_BLOCK);
    int action_type=lts_type_get_edge_label_typeno(lts->ltstype,0);
    uint32_t tau=(uint32_t)VTputChunk(lts->values[action_type],chunk_str("tau"));
    Warning(info,"tau = %u",tau);
    uint32_t rate=(uint32_t)VTputChunk(lts->values[action_type],chunk_str("rate"));
    Warning(info,"rate = %u",rate);

    fprintf(imca,"#INITIALS\n");
    for(uint32_t i=0;i<lts->root_count;i++){
      fprintf(imca,IMCA_STATE_FMT "\n",lts->root_list[i]);
    }
    fprintf(imca,"#GOALS\n");
    if (lts->properties!=NULL){
        for(uint32_t i=0;i<lts->states;i++){
            if (lts->properties[i]!=0){
                fprintf(imca,IMCA_STATE_FMT "\n",i);
            }
        }
    }
    fprintf(imca,"#TRANSITIONS\n");
    write_imca_trans(imca,IMCA_STATE_FMT,lts,0);
    fclose(imca);
}
