// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>

#include <hre/user.h>
#include <util-lib/treedbs.h>


static void test_length(int N){
    Print(infoShort,"Testing TreeDBS for length %d",N);
    treedbs_t dbs=TreeDBScreate(N);
    int tmp[N];
    for(int i=0;i<N;i++){
        tmp[i]=0;
    }
    TreeFold(dbs,tmp);
    for(int i=0;i<N;i++){
        tmp[i]=1;
        TreeFold(dbs,tmp);
    }
    for(int i=0;i<N;i++){
        tmp[i]=2;
        TreeFold(dbs,tmp);
    }
    Print(infoShort,"insertion is done");
    for(int i=0;i<=2*N;i++){
        printf("%4d:",i);
        for(int j=0;j<N;j++){
            printf(" %d",TreeDBSGet(dbs,i,j));
        }
        printf("\n");
    }
}

int main(int argc, char *argv[]){
    HREinitBegin(argv[0]);
    HREinitStart(&argc,&argv,0,0,NULL,"");
    test_length(1);
    test_length(2);
    test_length(11);
    HREexit(HRE_EXIT_SUCCESS);
}

