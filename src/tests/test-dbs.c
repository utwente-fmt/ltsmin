/*
 * test-dbs.c
 *
 *  Created on: May 12, 2009
 *      Author: laarman
 */

#include <hre/config.h>

#include <pthread.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/times.h>
#include <sys/types.h>
#include <unistd.h>

#include <hre/user.h>
#include <hre-io/user.h>
#include <mc-lib/atomics.h>
#include <mc-lib/dbs-ll.h>
#include <mc-lib/stats.h>
#include <mc-lib/treedbs-ll.h>


typedef void *DBS_T; 
typedef int(*dbs_lookup_f)(const DBS_T dbs, const int *v);
typedef int(*dbs_lookup_ret_f)(const DBS_T dbs, const int *v, int *ret);
typedef DBS_T(*dbs_create_f)(int v, int s);
static dbs_lookup_ret_f lookup_ret = (dbs_lookup_ret_f)DBSLLlookup_ret;
static dbs_create_f create = (dbs_create_f)DBSLLcreate;
static dbs_stats_f statistics = (dbs_stats_f)DBSLLstats;

typedef struct context {
    int id;
    DBS_T db;
} *context_t;

typedef struct res_s {
    int count;
    float time;
    stats_t *stats;
} *res_t;

static char *program;
static int SIZE = 24;
static size_t NUM = 100*10*1024;
static size_t ARRAY_SIZE = 10;
static size_t SCAN_LEN = 0;
static size_t NUM_THREADS = 7;
static int STRUCT = 3; //1: DBS, 2: DBSLL, 3: TreeDBSLL
static int SHARED_DB = 1; //0=NO, 1=SEGMENT_INPUT, 2=SAME_INPUT 4=FILE
static int *vt[64];
static size_t n[64];
static stream_t s[64];
static char *filename;

void readfile(const size_t i) {
    char buf[4096];
    sprintf(buf, "%s-%zu", filename, i);
    s[i] = file_input(buf);
    stream_read(s[i], &ARRAY_SIZE, 4);
    struct stat fstat;
    stat(buf, &fstat);
    size_t size = (fstat.st_size - 4);
    n[i]=size / ARRAY_SIZE / 4;
    vt[i] = RTmalloc(size);
    stream_read(s[i], vt[i], size);
    stream_close(&s[i]);
    NUM+=n[i];
    char *fname = strrchr(filename, '/');
    Warning(info, "Read %s into memory (%zu x %zu)", fname==NULL? filename : fname+1, ARRAY_SIZE, n[i]);
    return;
}

void test() {
	size_t x,y;

    DBS_T dbs = create(ARRAY_SIZE, SIZE);

    struct tms tmp1;
	clock_t real_time1=times(&tmp1);
	int idx;
	printf("Filling dbs\n");
	int *ar = RTmalloc(sizeof(int[ARRAY_SIZE]));
	int seen =0;
    for (x = 0; x<NUM; x++) {
        for (y = 0; y<ARRAY_SIZE; y++) {
		    ar[y] = x+y;
		}
        seen = lookup_ret(dbs, ar, &idx);
        if (seen==1) printf("+%zu",x);
	}
	printf("filling: DONE\n");
    printf("Looking up in dbs\n");
    for (x = 0; x<NUM+10; x++) {
        for (y = 0; y<ARRAY_SIZE; y++) {
            ar[y] = x+y;
        }
        seen = lookup_ret(dbs, ar, &idx);
        if (seen==0) printf("!%zu", x);
    }
	struct tms tmp2;
#ifdef _WIN32
        float tick = CLK_TCK;
#else
	float tick=(float)sysconf(_SC_CLK_TCK);
#endif
	float real=(times(&tmp2)-real_time1)/tick;
    stats_t *stat = statistics(dbs);
    int elements = stat->elts;
    int tests = stat->tests;
    int misses = stat->misses;
    Warning(info, "{{{%.2f}}}, elts: %d, misses: %d, tests: %d", real, elements, misses, tests);
	printf("DONE\n");
    exit(0);
}

pthread_key_t *thread_id_key;

static size_t dones = 0;

void *fill(void *c) {
    set_label(program, "");
    context_t ctx = c;
    size_t id = ctx->id;
    DBS_T dbs = ctx->db;
    size_t start, end;
    int str;
    if (SHARED_DB==1) { //SEGMENT_INPUT
        int part_size = NUM/((float)NUM_THREADS);   
        start = part_size*id;
        end = start + part_size;
	    if (vt==NULL) {
            if (id==NUM_THREADS-1) end+=NUM%NUM_THREADS;
        } else {
            if (id<NUM%NUM_THREADS) end++;
        }
        str = 0;
    } else if (SHARED_DB==4) {
        readfile(id);
        fetch_add(&dones, 1);
        while (atomic_read(&dones) != NUM_THREADS) {}
        start = 0;
        end = n[id];
        str = id;    
    } else {
        start = 0;
        end = NUM;
        str = 0;
    }
    n[id]=end-start; 
	Warning(info, "Filling [%zu] dbs with %zu - %zu = %zu vectors", id, end, start, end-start) ;
	res_t res = RTmalloc(sizeof( *res));
    res->count = 0;
    int idx;
    struct tms tmp1;
	clock_t real_time1=times(&tmp1);
    if (vt==NULL) {
	    int ar[ARRAY_SIZE];
        for (size_t x = start; x<end; x++) {
	    	for (size_t y = 0; y<ARRAY_SIZE; y++) ar[y]=x+y;
            int seen = lookup_ret(dbs, ar, &idx);
            if (!seen)
                res->count++;
	    }
    } else {
        for (size_t x = start; x<end; x++) {
            int *ar = &vt[str][x*ARRAY_SIZE];
            int seen = lookup_ret(dbs, ar, &idx);
            if (!seen)
                res->count++;
        }
    }

	struct tms tmp2;
#ifdef _WIN32
	float tick=CLK_TCK;
#else
	float tick=(float)sysconf(_SC_CLK_TCK);
#endif
	float real=(times(&tmp2)-real_time1)/tick;

	float sys=(tmp2.tms_stime-tmp1.tms_stime)/tick;
	float usr=(tmp2.tms_utime-tmp1.tms_utime)/tick;
	Warning(info, "filling [%zu]: DONE real=%5.3f, sys=%5.3f, user=%5.3f", id, real,sys,usr);
    res->time = real;
    res->stats = statistics(dbs);
    return res;
}

void set_struct(const char *name){
    STRUCT = strcmp(name, "tree") ? (strcmp(name, "dbsll") ? 
                 (strcmp(name, "dbs") ? 0: 1) : 2) : 3;
    if (!STRUCT)
        Abort("Not a valid structure: %s", name);
    switch(STRUCT) {
        case 1:
            lookup_ret = (dbs_lookup_ret_f)DBSLLlookup_ret;
            create = (dbs_create_f)DBSLLcreate_sized;
            statistics = (dbs_stats_f)DBSLLstats;
            break;
        case 2:
            lookup_ret = (dbs_lookup_ret_f)TreeDBSLLlookup;
            create = (dbs_create_f)TreeDBSLLcreate_sized;
            statistics = (dbs_stats_f)TreeDBSLLstats;
            break;
    }
}

DBS_T do_readfile(int *count) {
    NUM = 0;
    SHARED_DB = 4;
    readfile(NUM_THREADS);
    DBS_T dbs = SHARED_DB ? create(ARRAY_SIZE, SIZE) : NULL;
    size_t start = 0;
    size_t end = n[NUM_THREADS];
    int str = NUM_THREADS;    
    int idx; 
    Warning(info, "Filling [%s] dbs with %zu - %zu = %zu vectors", "init", 
                    end, start, end-start);
    for (size_t x = start; x<end; x++) {
        if (!lookup_ret(dbs, &vt[str][x*ARRAY_SIZE], &idx))
            *count += 1;
    }
    return dbs;
}

int main2(const int argc, const char **args) {
    HREinitBegin (args[0]);
    HREinitStart ((int*)&argc,(char***)&args,0,0,NULL,NULL);
    program = get_label();
	DBS_T dbs;
    int elements = 0;
    int count = 0;
    
    if (argc>2) {
		NUM_THREADS = atoi(args[1]);
		SIZE = atoi(args[2]);
    } else {
        test();
    }
    if (NUM_THREADS==0) {
        set_struct(args[1]);
        test();
    }
    if (NUM_THREADS==0) {
        set_struct(args[1]);
        test();
    }
    
    if (argc>3)
		SHARED_DB = atoi(args[3]);
    if (argc>4)
		NUM = atoi(args[4]);
    if (argc>5)
		ARRAY_SIZE = atoi(args[5]);
    if (argc>6 && SHARED_DB)
        set_struct(args[6]); 
    
    if (argc>7 || SHARED_DB==0) {
        filename = SHARED_DB==0 ? (char*)args[3] : (char*)args[7];
        SCAN_LEN = atoi(args[6]);
        if (NUM==0)
            set_struct(args[4]);
        dbs = do_readfile(&count);
        elements += statistics(dbs)->elts;
    } else {
        dbs = SHARED_DB ? create(ARRAY_SIZE, SIZE) : NULL;
    }
    Warning(info, "THREADS: %zu, SIZE: %zu", NUM_THREADS, ARRAY_SIZE);

	thread_id_key = RTmalloc(sizeof *thread_id_key);
    pthread_key_create(thread_id_key, NULL);
    
    pthread_t threads[NUM_THREADS];
    context_t ctxs[NUM_THREADS];
    for(size_t i = 0; i < NUM_THREADS; i++) {
    	ctxs[i] = RTmalloc(sizeof *ctxs[i]);
        ctxs[i]->id=i;
        ctxs[i]->db=SHARED_DB ? dbs : create(ARRAY_SIZE, SIZE);
    }

    for(size_t i = 0; i < NUM_THREADS; i++) {
        pthread_create(&threads[i], NULL, fill, ctxs[i]);
    } 
    float tot = 0;
    int tests = 0;
    int misses = 0;
    int rehashes = 0;
    for(size_t i = 0; i < NUM_THREADS; i++) {
        float **ret=RTmalloc(sizeof *ret);
        pthread_join(threads[i], (void**)ret);
        res_t res = (res_t)*ret;
        tot+=res->time;
        count += res->count;
        elements+=res->stats->elts;
        tests += res->stats->tests;
        misses+=res->stats->misses;
        rehashes+=res->stats->rehashes;
    }
    float tot_norm = SHARED_DB==1||SHARED_DB==4 ? tot : tot/((float)NUM_THREADS); 
    Warning(info, "time:{{{%.2f}}}, elts:{{{%d}}}, misses:{{{%d}}}, tests:{{{%d}}}, rehashes:{{{%d}}}, %d", tot_norm, count, misses, tests, rehashes, elements);
    return 0;
}

int
main (int c, char **v)
{
    HREinitBegin(v[0]);
    HREinitStart(&c,&v,0,0,NULL,NULL);
    treedbs_ll_t dbs = TreeDBSLLcreate_sized (4, 10, 2, 5, 0, 1);
    int s[4] = {1,2,3,4};
    int t[8];
    tree_t next = (tree_t)t;
    TreeDBSLLlookup_incr (dbs, (int*)s, NULL, next);
    tree_ref_t ref = TreeDBSLLindex (dbs, next);
    uint32_t val = 0;
    printf("%u ", val); val = TreeDBSLLinc_sat_bits (dbs, ref);
    printf("%u ", val); val = TreeDBSLLinc_sat_bits (dbs, ref);
    printf("%u ", val); val = TreeDBSLLinc_sat_bits (dbs, ref);
    printf("%u ", val); val = TreeDBSLLinc_sat_bits (dbs, ref);
    printf("%u ", val); val = TreeDBSLLinc_sat_bits (dbs, ref);
    printf("%u ", val); val = TreeDBSLLinc_sat_bits (dbs, ref);
    printf("%u ", val); val = TreeDBSLLinc_sat_bits (dbs, ref);
    printf("%u ", val); val = TreeDBSLLinc_sat_bits (dbs, ref);
    printf("%u ", val); val = TreeDBSLLinc_sat_bits (dbs, ref);
    printf("%u ", val); val = TreeDBSLLinc_sat_bits (dbs, ref);
    printf("%u ", val); val = TreeDBSLLinc_sat_bits (dbs, ref);
    printf("%u ", val); val = TreeDBSLLinc_sat_bits (dbs, ref);
    printf("%u ", val); val = TreeDBSLLinc_sat_bits (dbs, ref);
    printf("%u ", val); val = TreeDBSLLinc_sat_bits (dbs, ref);
    printf("%u ", val); val = TreeDBSLLinc_sat_bits (dbs, ref);
    printf("%u ", val); val = TreeDBSLLinc_sat_bits (dbs, ref);
    printf("%u ", val); val = TreeDBSLLinc_sat_bits (dbs, ref);
    printf("%u ", val); val = TreeDBSLLinc_sat_bits (dbs, ref);
    printf("%u ", val); val = TreeDBSLLinc_sat_bits (dbs, ref);
    printf("%u ", val); val = TreeDBSLLinc_sat_bits (dbs, ref);
    printf("%u ", val); val = TreeDBSLLinc_sat_bits (dbs, ref);
    printf("%u ", val); val = TreeDBSLLinc_sat_bits (dbs, ref);
    printf("%u ", val); val = TreeDBSLLinc_sat_bits (dbs, ref);
    printf("%u ", val); val = TreeDBSLLinc_sat_bits (dbs, ref);
    printf("%u ", val); val = TreeDBSLLinc_sat_bits (dbs, ref);
    printf("%u ", val); val = TreeDBSLLinc_sat_bits (dbs, ref);
    printf("%u ", val); val = TreeDBSLLinc_sat_bits (dbs, ref);
    printf("%u ", val); val = TreeDBSLLinc_sat_bits (dbs, ref);
    printf("%u ", val); val = TreeDBSLLinc_sat_bits (dbs, ref);
    printf("%u ", val); val = TreeDBSLLinc_sat_bits (dbs, ref);
    printf("%u ", val); val = TreeDBSLLinc_sat_bits (dbs, ref);
    HREassert( val == 31, "Error in test");
    printf("%u ", val); val = TreeDBSLLdec_sat_bits (dbs, ref);
    printf("%u ", val); val = TreeDBSLLdec_sat_bits (dbs, ref);
    printf("%u ", val); val = TreeDBSLLdec_sat_bits (dbs, ref);
    printf("%u ", val); val = TreeDBSLLdec_sat_bits (dbs, ref);
    printf("%u ", val); val = TreeDBSLLdec_sat_bits (dbs, ref);
    printf("%u ", val); val = TreeDBSLLdec_sat_bits (dbs, ref);
    printf("%u ", val); val = TreeDBSLLdec_sat_bits (dbs, ref);
    printf("%u ", val); val = TreeDBSLLdec_sat_bits (dbs, ref);
    printf("%u ", val); val = TreeDBSLLdec_sat_bits (dbs, ref);
    printf("%u ", val); val = TreeDBSLLdec_sat_bits (dbs, ref);
    printf("%u ", val); val = TreeDBSLLdec_sat_bits (dbs, ref);
    printf("%u ", val); val = TreeDBSLLdec_sat_bits (dbs, ref);
    printf("%u ", val); val = TreeDBSLLdec_sat_bits (dbs, ref);
    printf("%u ", val); val = TreeDBSLLdec_sat_bits (dbs, ref);
    printf("%u ", val); val = TreeDBSLLdec_sat_bits (dbs, ref);
    printf("%u ", val); val = TreeDBSLLdec_sat_bits (dbs, ref);
    printf("%u ", val); val = TreeDBSLLdec_sat_bits (dbs, ref);
    printf("%u ", val); val = TreeDBSLLdec_sat_bits (dbs, ref);
    printf("%u ", val); val = TreeDBSLLdec_sat_bits (dbs, ref);
    printf("%u ", val); val = TreeDBSLLdec_sat_bits (dbs, ref);
    printf("%u ", val); val = TreeDBSLLdec_sat_bits (dbs, ref);
    printf("%u ", val); val = TreeDBSLLdec_sat_bits (dbs, ref);
    printf("%u ", val); val = TreeDBSLLdec_sat_bits (dbs, ref);
    printf("%u ", val); val = TreeDBSLLdec_sat_bits (dbs, ref);
    printf("%u ", val); val = TreeDBSLLdec_sat_bits (dbs, ref);
    printf("%u ", val); val = TreeDBSLLdec_sat_bits (dbs, ref);
    printf("%u ", val); val = TreeDBSLLdec_sat_bits (dbs, ref);
    printf("%u ", val); val = TreeDBSLLdec_sat_bits (dbs, ref);
    printf("%u ", val); val = TreeDBSLLdec_sat_bits (dbs, ref);
    printf("%u ", val); val = TreeDBSLLdec_sat_bits (dbs, ref);
    printf("%u ", val); val = TreeDBSLLdec_sat_bits (dbs, ref);
    HREassert ( val == 0, "no empty");
    //printf("%u ", val); val = TreeDBSLLdec_sat_bits (dbs, ref);
    printf("\n");
    //TreeDBSLLinc_sat_bits (dbs, ref);
    HREexit(0);
    (void)c;(void)v;
}

