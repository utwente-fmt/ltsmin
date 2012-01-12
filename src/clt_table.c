#include <config.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <time.h>

#include <clt_table.h>
#include <fast_hash.h>
#include <runtime.h>
#include <tls.h>

static const struct timespec BO = {0, 2500};

#define CLT_HASHFUNC(k, ksize) (k >> (ksize - T_BITS))
#define CLT_REMAINDER(k) (k & ((1<<R_BITS)-1))

#define NONE 0
#define EMPTY 1
#define LEFT 2
#define RIGHT 3

#define R_BITS 28
#define B_SPACE 1024

typedef struct __attribute__ ((__packed__)) clt_bucket {
    uint32_t rest      : R_BITS;
    uint32_t  occupied : 1;
    uint32_t  locked   : 1;
    uint32_t  virgin   : 1;
    uint32_t  change   : 1;
} clt_bucket_t;

struct clt_dbs_s {
    clt_bucket_t       *table;
    uint32_t            keysize; // in bits
    size_t              size;
    size_t              mask;
    size_t              log_size;
};

/**
 * return > 0 if table[pos].key >  k
 * return   0 if table[pos].key == k
 * return < 0 if table[pos].key <  k
 */
static inline
int clt_compare (const clt_dbs_t* dbs, size_t pos, uint32_t r)
{
	if (dbs->table[pos].rest > r) return 1;
    if (dbs->table[pos].rest < r) return -1;
    return 0;
}

static inline 
int clt_find_left_from (const clt_dbs_t* dbs, size_t pos)
{
	do {
		pos--;
		assert (pos < dbs->size+B_SPACE);
	}
	while (dbs->table[pos].occupied);
	return pos;
}

static inline 
int clt_find_right_from (const clt_dbs_t* dbs, size_t pos)
{
	do {
		pos++;
		assert (pos < dbs->size+B_SPACE+B_SPACE);
	} while (dbs->table[pos].occupied);
	return pos;
}

static inline 
bool clt_try_lock (const clt_dbs_t* dbs, size_t pos)
{
	if (dbs->table[pos].locked)
		return false;
	clt_bucket_t currval, newval;
	uint64_t p = atomic64_read(dbs->table+pos);
	memcpy(&currval, &p, sizeof(clt_bucket_t));
	memcpy(&newval, &p, sizeof(clt_bucket_t));
	currval.occupied = 0;
	currval.locked = 0;
	newval.locked = 1;
	return cas ((uint32_t*)&dbs->table[pos], *(uint32_t*)&currval, *(uint32_t*)&newval);
}

static inline 
void clt_unlock (const clt_dbs_t* dbs, uint32_t pos)
{
    clt_bucket_t b = *(clt_bucket_t*)&atomic32_read(&dbs->table[pos]);
    b.locked = 0;
    atomic32_write(&dbs->table[pos], *(uint32_t*)&b);
}

int clt_find_or_put (const clt_dbs_t* dbs, uint64_t k)
{
   	size_t idx = SuperFastHash (&k, 8, 0)+B_SPACE; // add the breathing space
   	idx &= dbs->mask;
    uint32_t r   = CLT_REMAINDER (k);
    assert (k < (1UL << dbs->keysize));
     
	size_t t_left, t_right;

	clt_bucket_t check, oldval, newval;
	check = dbs->table[idx];
	
    if (!check.occupied) {
	    newval.rest = r;
    	newval.occupied = newval.change = newval.virgin = 1;

        check.occupied = 0;
    	check.locked = 0;
    	long res = cas((uint32_t*)&dbs->table[idx], *(uint32_t*)&check, *(uint32_t*)&newval);
    	oldval = *(clt_bucket_t*)&res;
    	if (!oldval.occupied && !oldval.locked)	{
    		return 0;
    	} else if (!oldval.occupied && oldval.locked) {
    		// locked, start over
            nanosleep (&BO, NULL);
    		return clt_find_or_put (dbs, k);
    	} // oldval.occupied -> normal insert
    }
    t_left = clt_find_left_from (dbs, idx);
    t_right = clt_find_right_from (dbs, idx);

	if (!clt_try_lock(dbs, t_left)) {
        nanosleep (&BO, NULL);
		return clt_find_or_put (dbs, k);
    }
	if (!clt_try_lock(dbs, t_right)) {
		clt_unlock (dbs, t_left);
		nanosleep (&BO, NULL);
		return clt_find_or_put (dbs, k);
	}
	/* Start of CS */
    /* =========== */

    int count = 0;
    size_t j = idx;
    uint32_t num_in_group = 0;
    size_t q;

    // determine which emty location is closer -> less swapping 
    int dir = (idx-t_left)-(t_right-idx);
    if (dir > 0) {
        while (j < t_right) {
            count -= dbs->table[j].virgin;
            ++j;
        }

        if (dbs->table[idx].virgin) { // Only search for the element if the virgin bit is set
            int c2 = count;
            --j;
            while (c2 < 1 && dbs->table[j].occupied) {
                assert (j < dbs->size+B_SPACE+B_SPACE);
                c2 += dbs->table[j].change;
                if (c2 == 0) {
                    if (dbs->table[j].rest < r) break;
                    if (dbs->table[j].rest == r) {
                        clt_unlock(dbs, t_left);
                        clt_unlock(dbs, t_right);
                        return 1; // FOUND
                    }
                }
                --j;
            }
        }

        q = t_right;
        while(1) {
            assert (q <  dbs->size+B_SPACE+B_SPACE);
            count += dbs->table[q-1].change;

            if(     (dbs->table[idx].virgin && count == 0 && dbs->table[q-1].rest < r) ||    //bij de juiste groep zoeken naar de correcte positie in de groep
                    (count == 1) ||                                                  //eerste element in de groep of bij nieuwe groep middel/rechts van het cluster
                    (count == 0 && !dbs->table[q-1].occupied)                          //eerste element van de groep met daarna geen groepen meer (count==0)
            )
                break;

            if (count == 0)
                ++num_in_group;

            dbs->table[q].rest       = dbs->table[q-1].rest;
            dbs->table[q].change     = dbs->table[q-1].change;
            dbs->table[q].occupied   = 1;
            --q;
            assert (q < dbs->size+B_SPACE+B_SPACE);
        }

        dbs->table[q].rest      = r;
        dbs->table[q].occupied  = 1;

        if(!dbs->table[idx].virgin){
            dbs->table[idx].virgin  = 1;
            dbs->table[q].change    = 1;
        } else if (num_in_group == 0) { //first of the group, set changebit to one
            dbs->table[q-1].change  = 0;
            dbs->table[q].change    = 1;
        }else{
            dbs->table[q].change    = 0;
        }
    } else {
        while (j > t_left) {
            count -= dbs->table[j].virgin;
            --j;
        }

        if (dbs->table[idx].virgin) { // Only search for the element if the virgin bit is set
            int c2 = count;
            ++j;
            while (c2 < 0 && dbs->table[j].occupied) {
                assert (j < dbs->size+B_SPACE+B_SPACE);
                if (c2 == -1) {
                    if (dbs->table[j].rest > r) break;
                    if (dbs->table[j].rest == r) {
                        clt_unlock(dbs, t_left);
                        clt_unlock(dbs, t_right);
                        return 1; // FOUND
                    }
                }
                c2 += dbs->table[j].change;
                ++j;
            }
        }

        // swap 
        q = t_left;
        while (1) {
            if ((dbs->table[idx].virgin && count == -1 && dbs->table[q+1].rest > r) || count == 0) {
                break;
            }

            dbs->table[q].rest     = dbs->table[q+1].rest;
            dbs->table[q].change   = dbs->table[q+1].change;
            dbs->table[q].occupied = 1;

            count += dbs->table[q+1].change;
            if (dbs->table[idx].virgin && count == 0)
                dbs->table[q].change = 0;

            ++q;
            assert (q < dbs->size+B_SPACE+B_SPACE);
        }

        dbs->table[q].rest         = r;
        dbs->table[q].occupied     = 1;
        if (count == 0) {
            dbs->table[idx].virgin = 1;
            dbs->table[q].change   = 1;
        } else {
            dbs->table[q].change   = 0;
        }
    }
    /* End of CS */
    /* ========= */
	clt_unlock (dbs, t_left);
	clt_unlock (dbs, t_right);
    //printtable(dbs);
    return 0;
}

clt_dbs_t *
clt_create (uint32_t ksize, uint32_t log_size)
{
    assert(sizeof(clt_bucket_t) == sizeof(uint32_t));
    assert(log_size <= 40);
    assert(ksize > 0 && ksize <= log_size + R_BITS);

    clt_dbs_t              *dbs = RTmalloc(sizeof(clt_dbs_t));
    dbs->size = 1UL << log_size;
    dbs->mask = dbs->size - 1;
    dbs->log_size = log_size;
    dbs->keysize = ksize;
    dbs->table = RTmallocZero((dbs->size + 2*B_SPACE) * sizeof(clt_bucket_t));
    if (dbs->table == NULL)
        Fatal(1, error, "Something went wrong allocating the table array... aborting.\n");
    return dbs;
}

void clt_free(clt_dbs_t* dbs)
{
    free(dbs->table);
    free(dbs);
}
