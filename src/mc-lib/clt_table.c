#include <hre/config.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <hre/user.h>
#include <mc-lib/atomics.h>
#include <mc-lib/clt_table.h>
#include <mc-lib/dbs-ll.h>
#include <mc-lib/treedbs-ll.h>
#include <util-lib/fast_hash.h>
#include <util-lib/util.h>

static const struct timespec BO = {0, 2500};

#define NONE 0
#define EMPTY 1
#define LEFT 2
#define RIGHT 3
#define CLT_REM(k)      (k & ((1ULL << R_BITS) - 1))
#define CLT_HASH(d,k)   (k >> d->diff) //monotonic hash function

typedef struct __attribute__ ((__packed__)) clt_bucket {
    uint32_t            rest        : R_BITS;
    uint32_t            occupied    : 1;
    uint32_t            locked      : 1;
    uint32_t            virgin      : 1;
    uint32_t            change      : 1;
} clt_bucket_t;

struct clt_dbs_s {
    clt_bucket_t       *table;
    uint32_t            keysize; // in bits
    size_t              size;
    size_t              thresh;
    size_t              mask;
    size_t              log_size;
    size_t              diff;
    size_t              b_space;
};

/**
 * return > 0 if table[pos].key >  k
 * return   0 if table[pos].key == k
 * return < 0 if table[pos].key <  k
static inline size_t
clt_compare (const clt_dbs_t* dbs, size_t pos, uint32_t r)
{
	if (dbs->table[pos].rest > r) return 1;
    if (dbs->table[pos].rest < r) return -1;
    return 0;
}
 */

static inline size_t
clt_find_left_from (const clt_dbs_t* dbs, size_t pos)
{
	do {
        HREassert (pos > 0, "Cleary table overflow"); //TODO: return negative error (see dbs-ll.c)
		pos--;
	} while (dbs->table[pos].occupied);
	return pos;
}

static inline size_t
clt_find_right_from (const clt_dbs_t* dbs, size_t pos)
{
	do {
		pos++;
		HREassert (pos < dbs->size + dbs->b_space + dbs->b_space, "Cleary table overflow");
	} while (dbs->table[pos].occupied);
	return pos;
}

static inline uint32_t
e (clt_bucket_t val)
{
    void *p = &val;
    return *(uint32_t *)p;
}

static inline clt_bucket_t
b (uint32_t val)
{
    void *p = &val;
    return *(clt_bucket_t *)p;
}

static inline clt_bucket_t
read_table (const clt_dbs_t* dbs, size_t pos)
{
    return atomic_read (&dbs->table[pos]);
}

static inline void
write_table (const clt_dbs_t* dbs, size_t pos, clt_bucket_t val)
{
    atomic_write (&dbs->table[pos], val);
}

static inline bool
cas_table (const clt_dbs_t* dbs, size_t pos, clt_bucket_t currval,
           clt_bucket_t newval)
{
    return cas ((uint32_t*)&dbs->table[pos], e(currval), e(newval));
}

static inline clt_bucket_t
cas_ret_table (const clt_dbs_t* dbs, size_t pos, clt_bucket_t currval,
               clt_bucket_t newval)
{
    uint32_t ret = cas_ret ((uint32_t*)&dbs->table[pos], e(currval), e(newval));
    return b(ret);
}

static inline bool
clt_try_lock (const clt_dbs_t* dbs, size_t pos)
{
    clt_bucket_t currval, newval;
    currval = newval = read_table (dbs, pos);
    if (currval.locked) return false;
    currval.occupied = 0;
    currval.locked = 0;
    newval.locked = 1;
    return cas_table (dbs, pos, currval, newval);
}

static inline void
clt_unlock (const clt_dbs_t* dbs, size_t pos)
{
    clt_bucket_t b = read_table (dbs, pos);
    b.locked = 0;
    write_table (dbs, pos, b);
}

int
clt_find_or_put (const clt_dbs_t* dbs, uint64_t k, bool insert)
{
    HREassert (k < (1ULL << dbs->keysize), "Cleary table: invalid key");
    clt_bucket_t        check, oldval, newval;
    uint32_t            rem = CLT_REM (k);
    uint64_t            idx = CLT_HASH (dbs,k) + dbs->b_space; // add breathing space
	check = read_table (dbs, idx);
	if (!check.occupied) {
	    newval.rest = rem;
    	newval.occupied = newval.change = newval.virgin = 1; newval.locked = 0;
        check.occupied = 0;
    	check.locked = 0;
    	oldval = cas_ret_table (dbs, idx, check, newval);
    	if (!oldval.occupied && !oldval.locked)	{
    		return 0;
    	} else if (!oldval.occupied && oldval.locked) {
    		// locked, start over
            nanosleep (&BO, NULL);
    		return clt_find_or_put (dbs, k, insert);
    	} // oldval.occupied -> normal insert
    }
    size_t          t_left = clt_find_left_from (dbs, idx);
    size_t          t_right = clt_find_right_from (dbs, idx);

    if (t_right - t_left >= dbs->thresh) return DB_FULL;

    if (!clt_try_lock(dbs, t_left)) {
        nanosleep (&BO, NULL);
        return clt_find_or_put (dbs, k, insert);
    }

    if (!clt_try_lock(dbs, t_right)) {
        clt_unlock (dbs, t_left);
        nanosleep (&BO, NULL);
        return clt_find_or_put (dbs, k, insert);
    }
    /* Startof CS */
    /* =========== */

    int count = 0;
    size_t j = idx;
    uint32_t num_in_group = 0;
    size_t q;

    // determine which empty location is closer -> less swapping
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
                HREassert (j < dbs->size + dbs->b_space + dbs->b_space, "Cleary table overflow");
                c2 += dbs->table[j].change;
                if (c2 == 0) {
                    if (dbs->table[j].rest < rem) break;
                    if (dbs->table[j].rest == rem) {
                        clt_unlock (dbs, t_left);
                        clt_unlock (dbs, t_right);
                        return 1; // FOUND
                    }
                }
                --j;
            }
        }

        if (!insert) {
            clt_unlock (dbs, t_left);
            clt_unlock (dbs, t_right);
            return DB_NOT_FOUND; // NOT FOUND
        }

        // swap
        q = t_right;
        while (1) {
            HREassert (q <  dbs->size + dbs->b_space + dbs->b_space, "Cleary table overflow");
            count += dbs->table[q-1].change;

            if ( (dbs->table[idx].virgin && count == 0 && dbs->table[q-1].rest < rem) ||  //bij de juiste groep zoeken naar de correcte positie in de groep
                 (count == 1) ||                                                        //eerste element in de groep of bij nieuwe groep middel/rechts van het cluster
                 (count == 0 && !dbs->table[q-1].occupied) )                            //eerste element van de groep met daarna geen groepen meer (count==0)
                break;

            if (count == 0)
                ++num_in_group;

            dbs->table[q].rest       = dbs->table[q-1].rest;
            dbs->table[q].change     = dbs->table[q-1].change;
            dbs->table[q].occupied   = 1;
            --q;
            HREassert (q < dbs->size + dbs->b_space + dbs->b_space, "Cleary table overflow");
        }

        dbs->table[q].rest      = rem;
        dbs->table[q].occupied  = 1;

        if (!dbs->table[idx].virgin){
            dbs->table[idx].virgin  = 1;
            dbs->table[q].change    = 1;
        } else if (num_in_group == 0) { //first of the group, set changebit to one
            dbs->table[q-1].change  = 0;
            dbs->table[q].change    = 1;
        } else {
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
                HREassert (j < dbs->size + dbs->b_space + dbs->b_space, "Cleary table overflow");
                if (c2 == -1) {
                    if (dbs->table[j].rest > rem) break;
                    if (dbs->table[j].rest == rem) {
                        clt_unlock (dbs, t_left);
                        clt_unlock (dbs, t_right);
                        return 1; // FOUND
                    }
                }
                c2 += dbs->table[j].change;
                ++j;
            }
        }

        if (!insert) {
            clt_unlock (dbs, t_left);
            clt_unlock (dbs, t_right);
            return DB_NOT_FOUND; // NOT FOUND
        }

        // swap 
        q = t_left;
        while (1) {
            if ((dbs->table[idx].virgin && count == -1 && dbs->table[q+1].rest > rem) || count == 0) {
                break;
            }

            dbs->table[q].rest     = dbs->table[q+1].rest;
            dbs->table[q].change   = dbs->table[q+1].change;
            dbs->table[q].occupied = 1;

            count += dbs->table[q+1].change;
            if (dbs->table[idx].virgin && count == 0)
                dbs->table[q].change = 0;

            ++q;
            HREassert (q < dbs->size + dbs->b_space + dbs->b_space, "Cleary table overflow");
        }

        dbs->table[q].rest         = rem;
        dbs->table[q].occupied     = 1;
        if (count == 0) {
            dbs->table[idx].virgin = 1;
            dbs->table[q].change   = 1;
        } else {
            dbs->table[q].change   = 0;
        }
    }
    mfence(); // TODO: use atomic_reads+atomic_writes instead
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
    HREassert (sizeof(clt_bucket_t) == sizeof(uint32_t), "Wrong bucket size");
    HREassert (log_size <= 40, "Cleary table to large: 2^%u", log_size);
    HREassert (log_size > 7, "Cleary table to small to add breathing space: 2^%u", log_size);
    HREassert (ksize > 0 && ksize <= log_size + R_BITS, "Wrong Cleary table dimensions "
               "(logsize:2^%u, keysize: %u)", log_size, ksize);
    HREassert (ksize > log_size, "Cleary keysize to small for good distribution "
               "(logsize:2^%u, keysize: %u)", log_size, ksize);
    clt_dbs_t              *dbs = RTmalloc (sizeof(clt_dbs_t));
    dbs->size = 1ULL << log_size;
    dbs->thresh = dbs->size / 64;
    dbs->thresh = min (dbs->thresh, 1ULL << 20);
    dbs->mask = dbs->size - 1;
    dbs->log_size = log_size;
    dbs->keysize = ksize;
    dbs->diff = dbs->keysize - dbs->log_size;
    dbs->b_space = dbs->size >> 5; // < 5 %
    dbs->table = RTmallocZero ((dbs->size + 2*dbs->b_space) * sizeof(clt_bucket_t));
    HREassert ( !(((size_t)dbs->table) & 3), "Wrong alignment");
    if (dbs->table == NULL)
        Abort ("Something went wrong allocating the table array... aborting.\n");
    return dbs;
}

void clt_free(clt_dbs_t* dbs)
{
    RTfree(dbs->table);
    RTfree(dbs);
}
