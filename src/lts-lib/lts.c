// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <hre/user.h>
#include <lts-io/provider.h>
#include <lts-lib/lts.h>
#include <util-lib/tables.h>

lts_t lts_create(){
    lts_t lts=(lts_t)RTmallocZero(sizeof(struct lts));
    lts->type=LTS_LIST;
    lts->tau=-1;
    return lts;
}

void lts_free(lts_t lts){
    RTfree(lts->begin);
    RTfree(lts->src);
    RTfree(lts->label);
    RTfree(lts->dest);
    RTfree(lts);
}

static void build_block(uint32_t states,uint32_t transitions,uint32_t *begin,uint32_t *block,uint32_t *label,uint32_t *other){
    int has_label=(label!=NULL);
    uint32_t i;
    uint32_t loc1,loc2;
    uint32_t tmp_label1=0,tmp_label2=0;
    uint32_t tmp_other1,tmp_other2;

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
        if(has_label) tmp_label1=label[i];
        tmp_other1=other[i];
        for(;;){
            if (loc1==i) {
                block[i]=i;
                if(has_label) label[i]=tmp_label1;
                other[i]=tmp_other1;
                break;
            }
            loc2=block[loc1];
            if(has_label) tmp_label2=label[loc1];
            tmp_other2=other[loc1];
            block[loc1]=loc1;
            if(has_label) label[loc1]=tmp_label1;
            other[loc1]=tmp_other1;
            if (loc2==i) {
                block[i]=i;
                if(has_label) label[i]=tmp_label2;
                other[i]=tmp_other2;
                break;
            }
            loc1=block[loc2];
            if(has_label) tmp_label1=label[loc2];
            tmp_other1=other[loc2];
            block[loc2]=loc2;
            if(has_label) label[loc2]=tmp_label2;
            other[loc2]=tmp_other2;
        }
    }
}

void lts_set_type(lts_t lts,LTS_TYPE type){
    uint32_t i,j;

    if (lts->type==type) return; /* no type change */
    Debug("first change to LTS_LIST");
    switch(lts->type){
        case LTS_LIST:
            lts->begin=(uint32_t*)RTmalloc(sizeof(uint32_t)*(lts->states+1));
            break;
        case LTS_BLOCK:
            lts->src=(uint32_t*)RTmalloc(sizeof(uint32_t)*(lts->transitions));
            for(i=0;i<lts->states;i++){
                for(j=lts->begin[i];j<lts->begin[i+1];j++){
                    lts->src[j]=i;
                }
            }
            break;
        case LTS_BLOCK_INV:
            lts->dest=(uint32_t*)RTmalloc(sizeof(uint32_t)*(lts->transitions));
            for(i=0;i<lts->states;i++){
                for(j=lts->begin[i];j<lts->begin[i+1];j++){
                    lts->dest[j]=i;
                }
            }
            break;
    }
    Debug("then change to required type");
    lts->type=type;
    switch(type){
        case LTS_LIST:
            RTfree(lts->begin);
            lts->begin=NULL;
            return;
        case LTS_BLOCK:
            build_block(lts->states,lts->transitions,lts->begin,lts->src,lts->label,lts->dest);
            RTfree(lts->src);
            lts->src=NULL;
            return;
        case LTS_BLOCK_INV:
            build_block(lts->states,lts->transitions,lts->begin,lts->dest,lts->label,lts->src);
            RTfree(lts->dest);
            lts->dest=NULL;
            return;
    }
}

static void lts_realloc(lts_t lts){
    int N,K;
    if (lts->ltstype){
        N=lts_type_get_state_label_count(lts->ltstype);
        K=lts_type_get_edge_label_count(lts->ltstype);
    } else {
        N=0;
        K=1;
    }
    uint32_t size;
    // realloc root_list
    size=sizeof(uint32_t)*lts->root_count;
    lts->root_list=(uint32_t*)RTrealloc(lts->root_list,size);
    if (size && lts->root_list==NULL) Abort("out of memory");
    // realloc properties
    if (N>0) {
        size=sizeof(uint32_t)*lts->states;
        lts->properties=(uint32_t*)RTrealloc(lts->properties,size);
        if (size && lts->properties==NULL) Abort("out of memory");
    } else {
        lts->properties=NULL;
    }
    switch(lts->type){
        case LTS_BLOCK:
            break;
        case LTS_BLOCK_INV:
            break;
        case LTS_LIST:
            break;
        default:
            Abort("undefined ");
    }
    // realloc begin
    switch(lts->type){
        case LTS_BLOCK:
        case LTS_BLOCK_INV:
            size=sizeof(uint32_t)*(lts->states+1);
            break;
        case LTS_LIST:
            size=0;
            break;
    }
    lts->begin=(uint32_t*)RTrealloc(lts->begin,size);
    if (size && lts->begin==NULL) Abort("out of memory");
    // realloc src
    switch(lts->type){
        case LTS_BLOCK:
            size=0;
            break;
        case LTS_BLOCK_INV:
        case LTS_LIST:
            size=sizeof(uint32_t)*(lts->transitions);
            break;
    }
    lts->src=(uint32_t*)RTrealloc(lts->src,size);
    if (size && lts->src==NULL) Abort("out of memory");
    // realloc dest
    switch(lts->type){
        case LTS_BLOCK_INV:
            size=0;
            break;
        case LTS_BLOCK:
        case LTS_LIST:
            size=sizeof(uint32_t)*(lts->transitions);
            break;
    }
    lts->dest=(uint32_t*)RTrealloc(lts->dest,size);
    if (size && lts->dest==NULL) Abort("out of memory");
    // realloc label
    size=(K>0)?(sizeof(uint32_t)*(lts->transitions)):0;
    lts->label=(uint32_t*)RTrealloc(lts->label,size);
    if (size && lts->label==NULL) Abort("out of memory");
}

void lts_set_sig_given(lts_t lts,lts_type_t type,value_table_t *values){
    if (lts->ltstype) Abort("type change unimplemented");
    lts->ltstype=type;
    int V=lts_type_get_state_length(type);
    if (V>0) {
        lts->state_db=TreeDBScreate(V);
    }
    int N=lts_type_get_state_label_count(type);
    if (N>1) {
        lts->prop_idx=TreeDBScreate(N);
    }
    int K=lts_type_get_edge_label_count(type);
    if (K>1) {
        lts->edge_idx=TreeDBScreate(K);
    }
    int T=lts_type_get_type_count(type);
    lts->values=(value_table_t*)RTmalloc(T*sizeof(value_table_t));
    for(int i=0;i<T;i++){
        lts->values[i]=values[i];
    }
    Print(infoShort,"realloc");
    lts_realloc(lts);
}

void lts_set_sig(lts_t lts,lts_type_t type){
    int T=lts_type_get_type_count(type);
    value_table_t values[T];
    for(int i=0;i<T;i++){
        char *sort=lts_type_get_type(type,i);
        switch(lts_type_get_format(type,i)){
        case LTStypeDirect:
        case LTStypeRange:
        case LTStypeBool:
        case LTStypeTrilean:
        case LTStypeSInt32:
            values[i]=NULL;
            break;
        case LTStypeChunk:
        case LTStypeEnum:
            values[i] = simple_chunk_table_create(NULL,sort);
            break;
        }
    }
    lts_set_sig_given(lts,type,values);
}

void lts_set_size(lts_t lts,uint32_t roots,uint32_t states,uint32_t transitions){
    lts->root_count=roots;
    lts->states=states;
    lts->transitions=transitions;
    lts_realloc(lts);
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

int tau_step(void*context,lts_t lts,uint32_t src,uint32_t edge,uint32_t dest){
    (void)src; (void)dest;
    if (lts->label[edge]!=(uint32_t)(lts->tau)) return 0;
    bitset_t diverging=(bitset_t)context;
    if (diverging==NULL) return 1;
    if (bitset_set(diverging,dest)) return 1;
    return !bitset_set(diverging,src);
}

int stutter_step(void*context,lts_t lts,uint32_t src,uint32_t edge,uint32_t dest){
    (void)context; (void)edge;
    return lts->properties[src]==lts->properties[dest];
}

struct cycle_elim_context {
    silent_predicate silent;
    void* silent_ctx;
};

void lts_silent_cycle_elim(lts_t lts,silent_predicate silent,void*silent_ctx,bitset_t diverging){
    if (lts->state_db!=NULL){
        Warning(lerror,"illegally wiping out state vectors");
        lts->state_db=NULL;
    }
    int has_props=lts->properties!=NULL;
    int has_labels=lts->label!=NULL;

    uint32_t *queue=(uint32_t*)RTmalloc(sizeof(uint32_t)*lts->states);
    uint32_t *stack=(uint32_t*)RTmalloc(sizeof(uint32_t)*lts->states);
    uint32_t *next=(uint32_t*)RTmalloc(sizeof(uint32_t)*lts->states);
    bitset_t todo=bitset_create(64,64);
    uint32_t *map=(uint32_t*)RTmalloc(sizeof(uint32_t)*lts->states);

    uint32_t stack_ptr;
    uint32_t queue_ptr;
    
    Debug("queue on exit time");
    lts_set_type(lts,LTS_BLOCK);
    stack_ptr=0;
    queue_ptr=0;
    bitset_clear_all(todo);
    bitset_set_range(todo,0,lts->states-1);
    stack[stack_ptr]=0;
    while(bitset_next_set(todo,&stack[stack_ptr])){
        Debug("enter state %u",stack[stack_ptr]);
        bitset_clear(todo,stack[stack_ptr]);
        next[stack_ptr]=lts->begin[stack[stack_ptr]];
        for(;;){
            if (next[stack_ptr]<lts->begin[stack[stack_ptr]+1]){
                uint32_t dest=lts->dest[next[stack_ptr]];
                Debug("edge %u: %u -> %u",next[stack_ptr],stack[stack_ptr],dest);
                if (silent(silent_ctx,lts,stack[stack_ptr],next[stack_ptr],dest)){
                    next[stack_ptr]++;
                    if (bitset_test(todo,dest)){
                        bitset_clear(todo,dest);
                        stack_ptr++;
                        stack[stack_ptr]=dest;
                        next[stack_ptr]=lts->begin[stack[stack_ptr]];
                        Debug("enter state %u",stack[stack_ptr]);
                    }
                } else {
                    next[stack_ptr]++;
                }
            } else {
                Debug("leave state %u",stack[stack_ptr]);
                queue[queue_ptr]=stack[stack_ptr];
                queue_ptr++;
                if (stack_ptr>0){
                    // back track;
                    stack_ptr--;
                } else {
                    // DFS run complete;
                    break;
                }
            }
        }
    }
    Debug("mark components");
    lts_set_type(lts,LTS_BLOCK_INV);
    stack_ptr=0;
    bitset_clear_all(todo);
    bitset_set_range(todo,0,lts->states-1);
    uint32_t component=0;
    while(queue_ptr>0){
        queue_ptr--;
        if (!bitset_test(todo,queue[queue_ptr])){
            continue;
        }
        stack[stack_ptr]=queue[queue_ptr];
        Debug("enter state %u (%u)",stack[stack_ptr],component);
        bitset_clear(todo,stack[stack_ptr]);
        map[stack[stack_ptr]]=component;
        next[stack_ptr]=lts->begin[stack[stack_ptr]];
        for(;;){
            if (next[stack_ptr]<lts->begin[stack[stack_ptr]+1]){
                uint32_t src=lts->src[next[stack_ptr]];
                Debug("edge %u: %u <- %u",next[stack_ptr],src,stack[stack_ptr]);
                if (silent(silent_ctx,lts,src,next[stack_ptr],stack[stack_ptr])){
                    Debug("silent");
                    next[stack_ptr]++;
                    if (bitset_test(todo,src)){
                        bitset_clear(todo,src);
                        stack_ptr++;
                        stack[stack_ptr]=src;
                        next[stack_ptr]=lts->begin[stack[stack_ptr]];
                        Debug("enter state %u (%u)",stack[stack_ptr],component);
                        map[stack[stack_ptr]]=component;
                    }
                } else {
                    next[stack_ptr]++;
                }
            } else {
                Debug("leave state %u",stack[stack_ptr]);
                if (stack_ptr>0){
                    // back track;
                    stack_ptr--;
                } else {
                    // DFS run complete;
                    break;
                }
            }
        }
        component++;
    }
    Debug("showing map");
    for(uint32_t i=0;i<lts->states;i++){
        Debug("map[%4u]=%4u",i,map[i]);
    }
    Debug("divide out equivalence classes");
    lts_set_type(lts,LTS_LIST);
    Debug("initial states");
    for(uint32_t i=0;i<lts->root_count;i++){
        lts->root_list[i]=map[lts->root_list[i]];
    }
    uint32_t count=0;
    Debug("transitions");
    uint32_t s,d,l=0;
    for(uint32_t i=0;i<lts->transitions;i++){
        s=map[lts->src[i]];
        d=map[lts->dest[i]];
        if (has_labels) l=lts->label[i];
        if (silent(silent_ctx,lts,lts->src[i],i,lts->dest[i])&&(s==d)) {
            if (diverging==NULL || !bitset_test(diverging,lts->src[i])) {
                continue;
            }
        }
        lts->src[count]=s;
        if (has_labels) lts->label[count]=l;
        lts->dest[count]=d;
        count++;
    }
    if(has_props){
        uint32_t *temp_props=queue;
        Debug("dividing properties");
        for(uint32_t i=0;i<lts->states;i++){
            Debug("copy prop[%u]=%u",map[i],lts->properties[i]);
            temp_props[map[i]]=lts->properties[i];
        }
        for(uint32_t i=0;i<component;i++){
            lts->properties[i]=temp_props[i];
            Debug("prop[%u]=%u",i,lts->properties[i]);
        }
    }
    lts_set_size(lts,lts->root_count,component,count);
    RTfree(map);
    RTfree(queue);
    RTfree(stack);
    RTfree(next);
    Debug("uniq");
    lts_uniq(lts);
    Debug("cycle elim done");
}

void lts_merge(lts_t lts1,lts_t lts2){
    Print(info,"** warning ** omitting signature check");
    if (lts1->state_db) Abort("lts_merge cannot deal with state vectors");
    if (lts1->edge_idx) Abort("lts_merge cannot deal with multiple edge labels");
    uint32_t init_count=lts1->root_count;
    uint32_t state_count=lts1->states;
    uint32_t trans_count=lts1->transitions;
    lts_set_type(lts1,LTS_LIST);
    lts_set_type(lts2,LTS_LIST);
    lts_set_size(lts1,lts1->root_count+lts2->root_count,
        lts1->states+lts2->states,lts1->transitions+lts2->transitions);
    for(uint32_t i=0;i<lts2->root_count;i++){
        lts1->root_list[init_count]=state_count+lts2->root_list[i];
    }
    for(uint32_t i=0;i<lts2->transitions;i++){
        lts1->src[trans_count+i]=state_count+lts2->src[i];
        lts1->dest[trans_count+i]=state_count+lts2->dest[i];
    }
    if (lts1->label) {
        int T1=lts_type_get_edge_label_typeno(lts1->ltstype,0);
        int T2=lts_type_get_edge_label_typeno(lts2->ltstype,0);
        if (lts2->values[T2]!=NULL){
            int C = VTgetCount (lts2->values[T2]);
            int map[C];
            table_iterator_t it = VTiterator (lts2->values[T2]);
            for (int i = 0; IThasNext(it); i++) {
                chunk c = ITnext (it);
                map[i] = VTputChunk (lts1->values[T1], c);
            }
            for(uint32_t i=0;i<lts2->transitions;i++){
                lts1->label[trans_count+i]=map[lts2->label[i]];
            }
        } else {
            for(uint32_t i=0;i<lts2->transitions;i++){
                lts1->label[trans_count+i]=lts2->label[i];
            }
        }
    }
    if (lts1->properties) {
        if (lts1->prop_idx==NULL){
            Print(info,"copying one property");
            int T1=lts_type_get_state_label_typeno(lts1->ltstype,0);
            int T2=lts_type_get_state_label_typeno(lts2->ltstype,0);
            if (lts2->values[T2]!=NULL){
                Print(info,"copying chunks");
                int C = VTgetCount (lts2->values[T2]);
                int map[C];
                table_iterator_t it = VTiterator (lts2->values[T2]);
                for (int i = 0; IThasNext(it); i++) {
                    chunk c = ITnext (it);
                    map[i] = VTputChunk (lts1->values[T1], c);
                }
                Print(info,"copying labels");
                for (uint32_t i=0;i<lts2->states;i++){
                    lts1->properties[state_count+i] = map[lts2->properties[i]];
                }
            } else {
                for (uint32_t i=0;i<lts2->states;i++){
                    lts1->properties[state_count+i] = lts2->properties[i];
                }
            }
        } else {
            int K=lts_type_get_state_label_count(lts1->ltstype);
            int vec[K];
            for(uint32_t i=0;i<lts2->states;i++){
                TreeUnfold(lts2->prop_idx,lts2->properties[i],vec);
                for(int j=0;j<K;j++){
                    int T1=lts_type_get_state_label_typeno(lts1->ltstype,j);
                    int T2=lts_type_get_state_label_typeno(lts2->ltstype,j);
                    if (lts2->values[T2]!=NULL){
                        chunk c=VTgetChunk(lts2->values[T2],vec[j]);
                        vec[j]=VTputChunk(lts1->values[T1],c);
                    }
                }
                lts1->properties[state_count+i]=TreeFold(lts1->prop_idx,vec);
            }
        }
    }
    lts_free(lts2);
}

static const size_t BUFLEN = 65536;

lts_t lts_encode_edge(lts_t lts){
    lts_t res=lts_create();
    lts_set_type(res,LTS_LIST);
    lts_set_sig(res,single_action_type());
    lts_set_type(lts,LTS_LIST);
    uint32_t temp=lts->transitions+lts->root_count;
    int V=lts_type_get_state_length(lts->ltstype);
    if (V) temp+=lts->states;
    int N=lts_type_get_state_label_count(lts->ltstype);
    if (N) temp+=lts->states;
    int K=lts_type_get_edge_label_count(lts->ltstype);
    lts_set_size(res,1,lts->states + 1,temp);
    res->root_list[0]=0;
    Print(infoShort,"init");
    int init=VTputChunk(res->values[0],chunk_str("init"));
    uint32_t edge=0;
    for(uint32_t i=0;i<lts->root_count;i++){
        res->src[edge]=0;
        res->dest[edge]=lts->root_list[i]+1;
        res->label[edge]=init;
        edge++;
    }
    if (V) {
        int typeno[V];
        data_format_t format[V];
        for(int i=0;i<V;i++){
            typeno[i]=lts_type_get_state_typeno(lts->ltstype,i);
            format[i]=lts_type_get_format(lts->ltstype,typeno[i]);
        }
        int vector[V];
        for(uint32_t i=0;i<lts->states;i++){
            char label[BUFLEN];
            char *current=label;
            current+=snprintf(current,BUFLEN,"state");
            TreeUnfold(lts->state_db,i,vector);
            for(int j=0;j<V;j++){
                switch(format[j]){
                    case LTStypeDirect:
                    case LTStypeRange:
                    case LTStypeSInt32:
                        current+=snprintf(current,BUFLEN,"|%d",vector[j]);
                        break;
                    case LTStypeChunk:
                    case LTStypeEnum:
                        {
                        chunk label_c=VTgetChunk(lts->values[typeno[j]],vector[j]);
                        char label_s[label_c.len*2+6];
                        chunk2string(label_c,sizeof label_s,label_s);
                        current+=snprintf(current,BUFLEN,"|%s",label_s);
                        break;
                        }
                    case LTStypeBool: // fall through
                    case LTStypeTrilean: {
                        char* value = NULL;
                        switch (vector[j]) {
                            case 0: {
                                value = "false";
                                break;
                            }
                            case 1: {
                                value = "true";
                                break;
                            }
                            case 2: {
                                value = "maybe";
                                break;
                            }
                            default: {
                                Abort("Invalid value: %d", vector[j]);
                            }
                        }
                        current+=snprintf(current,BUFLEN,"|%s",value);
                        break;
                    }
                }
            }
            res->src[edge]=i+1;
            res->dest[edge]=i+1;
            res->label[edge]=VTputChunk(res->values[0],chunk_str(label));;
            edge++;
        }
    }
    if (N) {
        int typeno[N];
        data_format_t format[N];
        for(int i=0;i<N;i++){
            typeno[i]=lts_type_get_state_label_typeno(lts->ltstype,i);
            format[i]=lts_type_get_format(lts->ltstype,typeno[i]);
        }
        int vector[N];
        for(uint32_t i=0;i<lts->states;i++){

            char label[BUFLEN];
            char *current=label;
            current+=snprintf(current,BUFLEN,"prop");
            if (N==1) vector[0]=lts->properties[i];
            if (N>1) {
                TreeUnfold(lts->prop_idx,lts->properties[i],vector);
            }
            for(int j=0;j<N;j++){
                switch(format[j]){
                    case LTStypeDirect:
                    case LTStypeRange:
                    case LTStypeSInt32:
                        current+=snprintf(current,BUFLEN,"|%d",vector[j]);
                        break;
                    case LTStypeChunk:
                    case LTStypeEnum:
                        {
                        chunk label_c=VTgetChunk(lts->values[typeno[j]],vector[j]);
                        char label_s[label_c.len*2+6];
                        chunk2string(label_c,sizeof label_s,label_s);
                        current+=snprintf(current,BUFLEN,"|%s",label_s);
                        break;
                        }
                    case LTStypeBool:
                    case LTStypeTrilean: {
                        char* value = NULL;
                        switch (vector[j]) {
                            case 0: {
                                value = "false";
                                break;
                            }
                            case 1: {
                                value = "true";
                                break;
                            }
                            case 2: {
                                value = "maybe";
                                break;
                            }
                            default: {
                                Abort("Invalid value: %d", vector[j]);
                            }
                        }
                        current+=snprintf(current,BUFLEN,"|%s",value);
                        break;
                    }
                }
            }
            res->src[edge]=i+1;
            res->dest[edge]=i+1;
            res->label[edge]=VTputChunk(res->values[0],chunk_str(label));
            edge++;
        }
    }

    {
        int typeno[K];
        data_format_t format[K];
        for(int i=0;i<K;i++){
            typeno[i]=lts_type_get_edge_label_typeno(lts->ltstype,i);
            format[i]=lts_type_get_format(lts->ltstype,typeno[i]);
        }
        int vector[K];
        for(uint32_t i=0;i<lts->transitions;i++){
            char label[BUFLEN];
            char *current=label;
            if (K==1) vector[0]=lts->label[i];
            current+=snprintf(current,BUFLEN,"edge");
            if (K>1) {
                TreeUnfold(lts->edge_idx,lts->label[i],vector);
            }
            for(int j=0;j<K;j++){
                switch(format[j]){
                    case LTStypeDirect:
                    case LTStypeRange:
                    case LTStypeSInt32:
                        current+=snprintf(current,BUFLEN,"|%d",vector[j]);
                        break;
                    case LTStypeChunk:
                    case LTStypeEnum:
                        {
                        chunk label_c=VTgetChunk(lts->values[typeno[j]],vector[j]);
                        char label_s[label_c.len*2+6];
                        chunk2string(label_c,sizeof label_s,label_s);
                        current+=snprintf(current,BUFLEN,"|%s",label_s);
                        break;
                        }
                    case LTStypeBool:
                    case LTStypeTrilean: {
                        char* value = NULL;
                        switch (vector[j]) {
                            case 0: {
                                value = "false";
                                break;
                            }
                            case 1: {
                                value = "true";
                                break;
                            }
                            case 2: {
                                value = "maybe";
                                break;
                            }
                            default: {
                                Abort("Invalid value: %d", vector[j]);
                            }
                        }
                        current+=snprintf(current,BUFLEN,"|%s",value);
                        break;
                    }
                }
            }
            res->src[edge]=lts->src[i]+1;
            res->dest[edge]=lts->dest[i]+1;
            res->label[edge]=VTputChunk(res->values[0],chunk_str(label));
            edge++;
        }
    }
    return res;
}

