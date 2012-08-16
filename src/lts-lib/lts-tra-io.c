// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>

#include <stdlib.h>

#include <hre/user.h>
#include <lts-lib/rationals.h>
#include <lts-lib/lts.h>
#include <hre/stringindex.h>

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

#define BUFFER_SIZE 100000

void lts_read_tra(const char*tra_name,lts_t lts){
    lts_set_type(lts,LTS_LIST);
      char *lab_name=tra_get_lab(tra_name);
    Print(infoShort,"reading %s/%s",tra_name,lab_name);
    FILE* tra=fopen(tra_name,"r");
    if (tra == NULL) {
        AbortCall("Could not open %s for reading",tra_name);
    }
    FILE* lab=fopen(lab_name,"r");
    if (lab == NULL) {
        AbortCall("Could not open %s for reading",lab_name);
    }
    char buffer[BUFFER_SIZE];
    Print(infoShort,"read label header");
    string_index_t props=SIcreate();
    if (fgets(buffer,BUFFER_SIZE, lab)==NULL) AbortCall("read error");
    int no;
    char *entry,*next;
    char name[4096];
    uint32_t first_state;
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
    lts_type_t ltstype=lts_type_create();
    int booltype=lts_type_put_type(ltstype,"bool",LTStypeRange,NULL);
    lts_type_set_range(ltstype,booltype,0,1);
    lts_type_put_type(ltstype,"nat",LTStypeDirect,NULL);
    lts_type_put_type(ltstype,"pos",LTStypeDirect,NULL);
    lts_type_set_state_label_count(ltstype,no);
    for(int i=0;i<no;i++){
        lts_type_set_state_label_name(ltstype,i,SIget(props,i));
        lts_type_set_state_label_type(ltstype,i,"bool");
    }
    lts_type_set_edge_label_count(ltstype,2);
    lts_type_set_edge_label_name(ltstype,0,"numerator");
    lts_type_set_edge_label_type(ltstype,0,"nat");
    lts_type_set_edge_label_name(ltstype,1,"denominator");
    lts_type_set_edge_label_type(ltstype,1,"pos");
    lts_set_sig(lts,ltstype);

    Print(infoShort,"read transition header");
    uint32_t states;
    uint32_t transitions;
    if (fgets(buffer,BUFFER_SIZE, tra)==NULL) Abort("read error");
    if (sscanf(buffer,"STATES%u",&states)){
        if (first_state==0) Abort("tra and lab in different formats");
        if (fgets(buffer,BUFFER_SIZE, tra)==NULL) Abort("read error");
        sscanf(buffer,"TRANSITIONS%u",&transitions);
    } else {
        if (first_state==1) Abort("tra and lab in different formats");
        sscanf(buffer,"%u %u",&states,&transitions);
    }
    lts_set_size(lts,1,states,transitions);
    Print(infoShort,"%u states %u transitions",states,transitions);


    Print(infoShort,"reading transitions");
    for(uint32_t i=0;i<transitions;i++){
        if (fgets(buffer,BUFFER_SIZE, tra)==NULL) Abort("read error");
        uint32_t src,dst,labels[2];
        float rate;
        sscanf(buffer,"%u %u %f",&src,&dst,&rate);
        src-=first_state;
        dst-=first_state;
        lts->src[i]=src;
        lts->dest[i]=dst;
        rationalize32(rate,labels,labels+1);
        lts->label[i]=TreeFold(lts->edge_idx,(int32_t*)labels);
    }

    Print(infoShort,"reading state labels");
    uint32_t labels[no];
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

    fclose(tra);
    fclose(lab);
    free(lab_name);
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
    int N=lts_type_get_state_label_count(lts->ltstype);
    fprintf(lab,"#DECLARATION\n");
    for(int i=0;i<N;i++){
        fprintf(lab,i?" %s":"%s",lts_type_get_state_label_name(lts->ltstype,i));
    }
    fprintf(lab,"\n#END\n");
    int label[N];
    for(uint32_t i=0;i<lts->states;i++){
        TreeUnfold(lts->prop_idx,lts->properties[i],label);
        int first=1;
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
    fclose(tra);
    fclose(lab);
    free(lab_name);
}
