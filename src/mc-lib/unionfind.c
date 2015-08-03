#include <hre/config.h>

#include <mc-lib/unionfind.h>

#include <mc-lib/atomics.h>
#include <util-lib/util.h>

//#define UFDEBUG

#define UFVERSION 4
/*
 * UFVERSION 1 = original version (4 locks, also list locks)
 * UFVERSION 2 = newer version (2 locks)
 * UFVERSION 3 = newest version (1 locks)
 */

typedef enum uf_status_e {
    UF_UNSEEN     = 0, // initial
    UF_INIT       = 1,
    UF_LIVE       = 2,
    UF_LOCKED     = 3,
    UF_DEAD       = 4,
    UF_BUSY       = 5
} uf_status;

typedef enum list_status_e {
    LIST_LIVE     = 0, // initial
    LIST_BUSY     = 1,
    LIST_TOMBSTONE= 2
} list_status;

typedef uint64_t sz_w;
#define WORKER_BITS 64
#define REF_ERROR ((size_t)-1)


struct uf_node_s {
    sz_w            p_set;                  // Set of worker IDs (one bit for each worker)
    ref_t           parent;                 // The parent in the UF tree
    ref_t           list_next;              // next list `pointer' (we could also use a standard pointer)
    unsigned char   rank;                   // The height of the UF tree
    unsigned char   uf_status;              // {UNSEEN, INIT, LIVE, LOCKED, DEAD}
    unsigned char   list_status;            // {LIVE, BUSY, TOMBSTONE}
    char            pad[5];                 // padding to ensure that 64 bit fields are aligned properly in array
};

typedef struct uf_node_s uf_node_t;

struct uf_s {
    uf_node_t      *array;   // array: [ref_t] -> uf_node
};

uf_t *
uf_create ()
{
#ifdef UFDEBUG
    HREassert (sizeof(uf_node_t) == sizeof(int[8]),
            "Improper structure packing for uf_node_t. Expected: size = %zu", sizeof(int[8]));
#endif
    uf_t           *uf = RTmalloc (sizeof(uf_t));
    uf->array          = RTalignZero ( sizeof(int[8]),
                                       sizeof(uf_node_t) * (1ULL << dbs_size) );
    return uf;
}


// successor handling


/**
 * return -1 for states owner by other workers
 * return 1 for states locally owned
 */
int
uf_owner (const uf_t *uf, ref_t state, size_t worker)
{
    sz_w            w_id = 1ULL << worker;
    sz_w            W = atomic_read (&uf->array[state].p_set);
    return W & w_id ? 1 : (W & ~w_id ? -1 : 0);
}

bool
uf_is_in_list (const uf_t *uf, ref_t state)
{
    return atomic_read (&uf->array[state].list_status) != LIST_TOMBSTONE;
}

bool
uf_remove_from_list (const uf_t *uf, ref_t state)
{
    // only remove if it has LIST_LIVE , otherwise (LIST_BUSY) wait
    while (true) {
        // Poll first (cheap)
        list_status list_s = atomic_read(&uf->array[state].list_status);
        if (list_s == LIST_LIVE) {
            if (cas(&uf->array[state].list_status, LIST_LIVE, LIST_TOMBSTONE))
                return 1;
        } else if (list_s == LIST_TOMBSTONE)
            return 0;
    }
}


char     
uf_make_claim (const uf_t *uf, ref_t state, size_t worker)
{
#ifdef UFDEBUG
    HREassert (worker < WORKER_BITS);
#endif
    sz_w            w_id = 1ULL << worker;
    uf_status       uf_s = atomic_read (&uf->array[state].uf_status);

    // Is the state unseen? ==> Initialize it
    if (uf_s == UF_UNSEEN) {
        if (cas(&uf->array[state].uf_status, UF_UNSEEN, UF_INIT)) {
            // create state and add worker

            atomic_write (&uf->array[state].parent, state);
            atomic_write (&uf->array[state].list_next, state);
            atomic_write (&uf->array[state].p_set, w_id);
            atomic_write (&uf->array[state].uf_status, UF_LIVE);

            return CLAIM_FIRST;
        }
    }

    // Is someone currently initializing the state?
    while (uf_s == UF_INIT)
        uf_s = atomic_read (&uf->array[state].uf_status);

    ref_t f = uf_find(uf, state); 

    // Is the state dead?
    if (atomic_read (&uf->array[f].uf_status) == UF_DEAD)
        return CLAIM_DEAD;

    // Did I already explore `this' state?
    if ((atomic_read (&uf->array[f].p_set) & w_id ) != 0) {
        return CLAIM_FOUND;
        // NB: cycle is possibly missed (in case f got updated)
        // - but next iterations should fix this
    }

    // Add our worker ID to the set (and make sure it is the UF representative)
    or_fetch (&uf->array[f].p_set, w_id);
    while (atomic_read (&uf->array[f].parent) != f) {
        f = atomic_read (&uf->array[f].parent);
        or_fetch (&uf->array[f].p_set, w_id);
        if (atomic_read (&uf->array[f].uf_status) == UF_DEAD) {
            return CLAIM_DEAD;
        }
    }
    return CLAIM_SUCCESS;
}


// 'basic' union find

ref_t
uf_find (const uf_t *uf, ref_t state)
{
    // recursively find and update the parent (path compression)
    ref_t parent = atomic_read (&uf->array[state].parent);
    if (parent == state)
        return parent;
    ref_t root = uf_find (uf, parent);
    if (root != parent)
        atomic_write (&uf->array[state].parent, root);
    return root;
}

#ifdef USE_PATH_HALVING
ref_t
uf_find (const uf_t *uf, ref_t a)
{
    // recursively find and update the parent (path halving)
    ref_t state, parent, grandparent;
    state = a;

    while (1) {
        parent = atomic_read (&uf->array[state].parent);
        if (parent == state)
            return parent;

        grandparent = atomic_read (&uf->array[parent].parent);
        if (grandparent == parent)
            return grandparent;

        // path halving
        atomic_write(&uf->array[state].parent, grandparent);
        state = grandparent;
    }
}
#endif

bool
uf_sameset (const uf_t *uf, ref_t a, ref_t b)
{
    ref_t a_r = uf_find (uf, a);
    ref_t b_r = uf_find (uf, b);

    if (a_r == b_r)
        return 1;

    if (atomic_read (&uf->array[a_r].parent) == a)
        return 0;

    return uf_sameset (uf, a_r, b_r);
}

// dead

bool
uf_mark_dead (const uf_t *uf, ref_t state)
{

    ref_t f          = uf_find(uf, state);
    //Warning(info,"\x1B[40mmark dead: %zu %zu %d\x1B[0m", state, f, uf_debug(uf,state));
    bool result      = false;

    // wait for possible unions to completely finish
    // (part between last parent update and unlock)
    //while (atomic_read (&uf->array[f].uf_status) == UF_LOCKED);

    while (atomic_read (&uf->array[f].uf_status) != UF_DEAD)
        result = cas (&uf->array[f].uf_status, UF_LIVE, UF_DEAD);

#ifdef UFDEBUG
    HREassert (atomic_read (&uf->array[f].parent) == f,
            "representative should not change while marking dead %d", uf_debug(uf,f));

    HREassert (uf_is_dead(uf, f),     "state should be dead");
    HREassert (uf_is_dead(uf, state), "state should be dead");
#endif

    return result;
}

bool
uf_is_dead (const uf_t *uf, ref_t state)
{
    ref_t f = uf_find(uf, state);
    return atomic_read(&uf->array[f].uf_status) == UF_DEAD;
}

// different versions of pickfromlist and union

#if UFVERSION == 1

pick_e
uf_pick_from_list (const uf_t *uf, ref_t state, ref_t *node)
{

#ifdef UFDEBUG
    HREassert (atomic_read(&uf->array[state].list_status) == LIST_TOMBSTONE);
#endif

    // compress list by lazily removing tombstone sequences
    ref_t s     = state;
    ref_t n1    = atomic_read(&uf->array[s].list_next);
    ref_t n2;

    while (atomic_read(&uf->array[s].list_status) == LIST_TOMBSTONE) {

        if (s == n1) {
            if (uf_mark_dead (uf, state))
                return PICK_MARK_DEAD;
            return PICK_DEAD;
        }

        // [s|T] -> [n1|T] -> [n2|?] (iff s==TOMB && n1==TOMB)
        // [s|T] -----------> [n2|?] (update next pointer from s)
        list_status n1_status = atomic_read(&uf->array[n1].list_status);
        if (n1_status == LIST_TOMBSTONE) {
            n2 = atomic_read(&uf->array[n1].list_next);
            if (s == n2) {
                if (uf_mark_dead(uf, state))
                    return PICK_MARK_DEAD;
                return PICK_DEAD;
            }
            atomic_write (&uf->array[s].list_next, n2);
            s  = n2;
            n1 = atomic_read(&uf->array[s].list_next);
        } else {
            // list_status == ( LIST_LIVE | LIST_BUSY ) (no need to check)
            s = n1;
            break;
        }
    }
    atomic_write (&uf->array[state].list_next, s);

    while (atomic_read(&uf->array[s].list_status) == LIST_BUSY) ; // wait for busy states

    *node = s; // set the node reference
    return PICK_SUCCESS;
}

bool
lock_lists (const uf_t *uf, ref_t list_a, ref_t list_b, ref_t *locked_a, ref_t *locked_b)
{
    // lock list entries alphabetically
    // returns  true   <====>  locked both entries (we may have SameSet(a,b))
    // returns  false  <====>  SameSet(a,b) (and no lock is placed)

    // get LIVE entries for lists a and b
    // order these entries alphabetically and lock these in order
    // if fail: (unlock and) retry

    ref_t a = list_a;
    ref_t b = list_b;

    while (1) {
        bool lock_a = false;
        bool lock_b = false;

        if (uf_sameset (uf, a, b)) return false;

        // get live states for a and b
        uf_pick_from_list (uf, a, &a);
        uf_pick_from_list (uf, b, &b);

        if (a > b) { // SWAP(a,b)
            ref_t tmp = a;
            a = b;
            b = tmp;
        }

        // lock a
        if (atomic_read (&uf->array[a].list_status) == LIST_LIVE) {
            lock_a = cas (&uf->array[a].list_status, LIST_LIVE, LIST_BUSY);
        }

        // lock b
        if (lock_a) {
            if (atomic_read (&uf->array[b].list_status) == LIST_LIVE) {
                lock_b = cas (&uf->array[b].list_status, LIST_LIVE, LIST_BUSY);
            }

            if (lock_b) { // locked both a and b
                *locked_a = a;
                *locked_b = b;
                return true;
            } else {
                // if lock_b failed: unlock a
                atomic_write (&uf->array[a].list_status, LIST_LIVE);
            }
        }

    }
}

static inline ref_t
lock_node (const uf_t *uf, ref_t x)
{
    while (true) {
        while (atomic_read(&uf->array[x].list_status) == LIST_TOMBSTONE) {
            pick_e result = uf_pick_from_list (uf, x, &x);

            if (result == PICK_DEAD || result == PICK_MARK_DEAD) {
                return REF_ERROR;
            }
        }

        // No other workers can merge lists (states are locked)
#ifdef UFDEBUG
        HREassert(atomic_read(&uf->array[x].list_status) != LIST_BUSY);
#endif

        if (cas(&uf->array[x].list_status, LIST_LIVE, LIST_BUSY)) {
            return x;
        }
        else {
            if ( uf_is_dead(uf, x) )
                return REF_ERROR;
        }
    }
}

void
uf_merge_list(const uf_t *uf, ref_t list_x, ref_t list_y)
{
    // assert \exists x' \in List(x) (also for y)
    // - because states are locked and union(x,y) did not take place yet

#ifdef UFDEBUG
    HREassert(atomic_read(&uf->array[list_x].uf_status) == UF_LOCKED);
    HREassert(atomic_read(&uf->array[list_y].uf_status) == UF_LOCKED);
    HREassert(uf_find(uf, list_x) != uf_find(uf, list_y));
#endif

    ref_t x = list_x;
    ref_t y = list_y;

    x = lock_node (uf, x);
    //if (x == REF_ERROR) return;
    // no merge needed (x became empty, which might happen
    // if other threads united these lists and subsequently processed them)
    y = lock_node (uf, y);

#ifdef UFDEBUG
    HREassert (x != REF_ERROR && y != REF_ERROR, "Contradiction: non-tombstone x was locked");
    HREassert(atomic_read(&uf->array[x].list_status) == LIST_BUSY);
    HREassert(atomic_read(&uf->array[y].list_status) == LIST_BUSY);
#endif

    //swap (uf->array[x].list_next, uf->array[y].list_next);
    // unsure if swap preserves atomic operations - therefore just do it
    ref_t tmp = atomic_read(&uf->array[x].list_next);
    atomic_write(&uf->array[x].list_next, atomic_read(&uf->array[y].list_next));
    atomic_write(&uf->array[y].list_next, tmp);


    atomic_write(&uf->array[x].list_status, LIST_LIVE);
    atomic_write(&uf->array[y].list_status, LIST_LIVE);
}

ref_t
uf_lock (const uf_t *uf, ref_t state_x, ref_t state_y)
{
    ref_t a = state_x;
    ref_t b = state_y;
    while (1) {
        a = uf_find(uf, state_x);
        b = uf_find(uf, state_y);

        // SameSet(a,b)
        if (a == b) {
            return false;
        }

        if (a > b) { // SWAP(a,b)
            ref_t tmp = a;
            a = b;
            b = tmp;
        }

        // lock smallest ref first
        if (atomic_read(&uf->array[a].uf_status) == UF_LIVE) {
            if (cas(&uf->array[a].uf_status, UF_LIVE, UF_LOCKED)) {
                if (atomic_read(&uf->array[a].parent) == a) {
                    // a is successfully locked
                    if (atomic_read(&uf->array[b].uf_status) == UF_LIVE) {
                        if (cas(&uf->array[b].uf_status, UF_LIVE, UF_LOCKED)) {
                            if (atomic_read(&uf->array[b].parent) == b) {
                                // b is successfully locked
                                return true;
                            }
                            atomic_write (&uf->array[b].uf_status, UF_LIVE);
                        }
                    }
                }
                atomic_write (&uf->array[a].uf_status, UF_LIVE);
            }
        }
    }
}

void
uf_unlock (const uf_t *uf, ref_t state)
{
#ifdef UFDEBUG
    HREassert(uf->array[state].uf_status == UF_LOCKED);
#endif
    atomic_write (&uf->array[state].uf_status, UF_LIVE);
}

void
uf_union_aux (const uf_t *uf, ref_t root, ref_t other)
{
    // don't need CAS because the states are locked
    uf->array[root].p_set   |= uf->array[other].p_set;
    atomic_write(&uf->array[other].parent, root);
}

bool
uf_union (const uf_t *uf, ref_t state_x, ref_t state_y)
{
    if (uf_lock(uf, state_x, state_y)) {
#ifdef UFDEBUG
        HREassert(!uf_is_dead(uf, state_x));
        HREassert(!uf_is_dead(uf, state_y));
#endif

        ref_t x_f = uf_find(uf, state_x);
        ref_t y_f = uf_find(uf, state_y);

        // Combine the successors BEFORE the union
        // - ensures that there is a successor v with list_status != LIST_REMOVED

        uf_merge_list(uf, x_f, y_f);

        if (uf->array[x_f].rank > uf->array[y_f].rank) {
            uf_union_aux(uf, x_f, y_f); // x_f is the new root
        } else {
            uf_union_aux(uf, y_f, x_f); // y_f is the new root

            // increment the rank if it is equal
            if (uf->array[x_f].rank == uf->array[y_f].rank) {
                uf->array[y_f].rank ++;
            }
        }
        uf_unlock(uf, x_f);
        uf_unlock(uf, y_f);
#ifdef UFDEBUG
        HREassert (uf_sameset(uf, state_x, state_y), "uf_union: states should be in the same set");
#endif
        return 1;
    }
    else {
#ifdef UFDEBUG
        HREassert (uf_sameset(uf, state_x, state_y), "uf_union: states should be in the same set");
#endif
        return 0;
    }
}

#endif


#if UFVERSION == 2

pick_e
uf_pick_from_list (const uf_t *uf, ref_t state, ref_t *ret)
{
#ifdef UFDEBUG
    HREassert (atomic_read(&uf->array[state].list_status) == LIST_TOMBSTONE);
#endif

    // a -> b -> c
    ref_t a     = state;
    ref_t b     = atomic_read(&uf->array[a].list_next);
    ref_t c;

    // a -> a (single state TOMBSTONE => dead SCC)
    if (a == b) {
        if (uf_mark_dead (uf, a)) {
#ifdef UFDEBUG
            HREassert(0, "we should not get singleton reports");
#endif
            return PICK_MARK_DEAD;
        }
        return PICK_DEAD;
    }

    while (atomic_read(&uf->array[b].list_status) == LIST_TOMBSTONE) {
        c = atomic_read(&uf->array[b].list_next);

        // a -> b -> a (representative and last state both TOMBSTONE => dead SCC)
        if (a == c) {
            if (uf_mark_dead (uf, a)) {
                //Warning(info, "marked %zu (and %zu) dead", a, b);
                return PICK_MARK_DEAD;
            }
            return PICK_DEAD;
        }

        // both b and c may not be the representative
        if (atomic_read(&uf->array[a].parent) != a &&
            atomic_read(&uf->array[b].parent) != b) {

            // remove state b from the list
            cas(&uf->array[a].list_next, b,  c);
        }
        a = b; // TODO optimize
        b = c;
    }

    *ret = b;
    return PICK_SUCCESS;
}

bool
uf_lock (const uf_t *uf, ref_t a, ref_t b)
{
    ref_t a_r = a;
    ref_t b_r = b;

    if (a < b) {
        a_r = b;
        b_r = a;
    }

    if (atomic_read (&uf->array[a_r].uf_status) == UF_LIVE) {
       if (cas (&uf->array[a_r].uf_status, UF_LIVE, UF_LOCKED)) {
           if (atomic_read (&uf->array[b_r].uf_status) == UF_LIVE) {
               if (cas (&uf->array[b_r].uf_status, UF_LIVE, UF_LOCKED)) {
                   return 1;
               }
           }
           atomic_write (&uf->array[a_r].uf_status, UF_LIVE);
       }
    }
    return 0;
}

void
uf_unlock (const uf_t *uf, ref_t a, ref_t b)
{
    atomic_write (&uf->array[a].uf_status, UF_LIVE);
    atomic_write (&uf->array[b].uf_status, UF_LIVE);
}

bool
uf_union (const uf_t *uf, ref_t a, ref_t b)
{
    ref_t a_r, b_r;

    // do this in a while loop
    // recursion causes segfault (a lot of iterations)
    while(1) {
        a_r = uf_find (uf, a);
        b_r = uf_find (uf, b);

        // return if already united
        if (a_r == b_r) {
#ifdef UFDEBUG
            HREassert (uf_sameset(uf, a, b), "uf_union: states should be in the same set");
#endif
            return 0;
        }

#ifdef UFDEBUG
        HREassert (!uf_is_dead (uf, a) && !uf_is_dead (uf, b),
                "uf_union(%d, %d): states should not be dead",
                uf_debug(uf,a), uf_debug(uf,b));
#endif

        // lock a and b (or retry if fail)
        if (!uf_lock(uf, a_r, b_r))
            continue;

        // unlock if a_r and b_r if we did not lock the representatives
        if (a_r != uf->array[a_r].parent || b_r != uf->array[b_r].parent) {
            uf_unlock (uf, a_r, b_r);
            continue;
        }

        break;
    }

    // we have now locked the representatives for uf[a] and uf[b]

#ifdef UFDEBUG
    HREassert (atomic_read (&uf->array[a_r].uf_status) == UF_LOCKED,
        "a_r:%zu must be locked %d", a_r, uf_debug(uf,a_r));
    HREassert (atomic_read (&uf->array[b_r].uf_status) == UF_LOCKED,
        "b_r:%zu must be locked %d", b_r, uf_debug(uf,b_r));
#endif



    // decide new representative by rank (or pick a_r and update rank if equal)
    if (uf->array[a_r].rank < uf->array[b_r].rank) {
        ref_t tmp = a_r;
        a_r = b_r;
        b_r = tmp;
    } else if (uf->array[a_r].rank == uf->array[b_r].rank) {
        uf->array[a_r].rank ++;
    }

    // a_r is chosen as the new representative


    ref_t b_n = atomic_read (&uf->array[b_r].list_next);
    ref_t a_n = atomic_read (&uf->array[a_r].list_next);


    // 1. Update b_r -> a_n
        atomic_write (&uf->array[b_r].list_next, a_n);

    // 1. Update a_r -> b_n
        atomic_write (&uf->array[a_r].list_next, b_n);

#ifdef UFDEBUG
    HREassert (atomic_read (&uf->array[b_r].parent) == b_r,
            "Locked parent should not change %zu, %d", b_r, uf_debug(uf, b_r));
#endif
    // 3. parent for b_r is updated
    // Do this last so states won't get marked TOMBSTONE too soon
    atomic_write (&uf->array[b_r].parent, a_r);

    // 4. worker set for a_r is updated
    sz_w workers = uf->array[a_r].p_set | uf->array[b_r].p_set;
    atomic_write (&uf->array[a_r].p_set, workers);

    // 5. unlock a_r (and b_r)
    uf_unlock (uf, a_r, b_r);

#ifdef UFDEBUG
    HREassert (uf_sameset(uf, a, b), "uf_union: states should be in the same set");
#endif
    return 1;
}

#endif


#if UFVERSION == 3

pick_e
uf_pick_from_list (const uf_t *uf, ref_t state, ref_t *ret)
{
    ref_t a, b, c;
#ifdef UFDEBUG
    HREassert (atomic_read (&uf->array[state].list_status) == LIST_TOMBSTONE);
#endif

    // a -> b -> c
    a = state;
    b = atomic_read (&uf->array[a].list_next);

    while (!uf_sameset (uf, a, b))
        b = atomic_read (&uf->array[a].list_next);

#ifdef UFDEBUG
    HREassert (uf_sameset (uf, a, b));
#endif

    while (1) {

        if (atomic_read (&uf->array[b].list_status) == LIST_LIVE) {
            *ret = b;
            return PICK_SUCCESS;
        }

        c = atomic_read (&uf->array[b].list_next);
        while (!uf_sameset (uf, a, c))
            c = atomic_read (&uf->array[b].list_next);

#ifdef UFDEBUG
        HREassert (uf_sameset (uf, a, c));
        HREassert (uf_sameset (uf, b, c));
#endif

        if (a == c) {
            if (uf_mark_dead (uf, a)) {
                return PICK_MARK_DEAD;
            }
            return PICK_DEAD;
        }

        // list is at least size 3

        // make the list shorter
        if (atomic_read (&uf->array[a].parent) != a &&
            atomic_read (&uf->array[b].parent) != b) {
            cas (&uf->array[a].list_next, b,  c);
        }

        a = b;
        b = c;
    }
}

bool
uf_lock (const uf_t *uf, ref_t a)
{
    if (atomic_read (&uf->array[a].uf_status) == UF_LIVE) {
       if (cas (&uf->array[a].uf_status, UF_LIVE, UF_BUSY)) {
           // successfully locked
           // ensure that we actually locked the representative
           if (atomic_read (&uf->array[a].parent) == a)
               return 1;
           // otherwise unlock and try again
           atomic_write (&uf->array[a].uf_status, UF_LIVE);
       }
    }
    return 0;
}

void
uf_unlock (const uf_t *uf, ref_t a)
{
    atomic_write (&uf->array[a].uf_status, UF_LIVE);
}

bool
uf_union (const uf_t *uf, ref_t a, ref_t b)
{
    ref_t a_r, b_r, r, q, r_n, q_n;

    while (1) {
        // 1. find the representatives
        a_r = uf_find (uf, a);
        b_r = uf_find (uf, b);
        if (a_r == b_r) {
#ifdef UFDEBUG
            HREassert (uf_sameset(uf, a, b), "states should be merged after a union");
#endif
            return 0;
        }

        // 2. decide on the new root (deterministically)
        r = a_r;
        q = b_r;
        if (a_r < b_r) { // take the highest index as root
            r = b_r;
            q = a_r;
        }

        // 3. lock the non-root
        if (!uf_lock (uf, q))
            continue;

        // 4. check that the non-root is a representative
        if (atomic_read (&uf->array[q].parent) != q) {
            uf_unlock (uf, q);
            continue;
        }

        break;
    }

    // we can now ensure that q is a representative
    //   and no other worker may change this

    while (1) {
        // 5. check if r_n is in the same set as r (once true, this is always true)
        r_n = atomic_read (&uf->array[r].list_next);
        while (uf_find (uf, r_n) != r) {
            // either some other worker is busy (or has finished) with union(r,s)
            r = uf_find (uf, r);
            r_n = atomic_read (&uf->array[r].list_next);
        }

        // 6. set q.next --> r_n
        q_n = atomic_read (&uf->array[q].list_next);
        while (1) {

            // 6.2 set q.next --> r_n
            if (uf_find (uf, q_n) == q) { // not sure if this is necessary
                if (cas (&uf->array[q].list_next, q_n, r_n))
                    break;
            }

            q_n = atomic_read (&uf->array[q].list_next);
        }

        // q_n remains 'second' item of q's set and won't be removed

        // 7. set r.next --> q_n
        if (!cas (&uf->array[r].list_next, r_n, q_n)) {
            // r_n got updated after (5)
#ifdef UFDEBUG
            HREassert(atomic_read (&uf->array[q].list_next) == r_n,
                    "resetting q.next --> q_n should not fail");
#endif
            atomic_write (&uf->array[q].list_next, q_n);
            continue;
        }

        // 8. check if r is still the representative (r might not be in uf[r]'s list)
        if (atomic_read (&uf->array[r].parent) != r) {
            // r got updated, so reset and retry
#ifdef UFDEBUG
            HREassert(atomic_read (&uf->array[r].list_next) == q_n,
                    "resetting r.next --> r_n should not fail");
            HREassert(atomic_read (&uf->array[q].list_next) == r_n,
                    "resetting q.next --> q_n should not fail");
#endif
            atomic_write (&uf->array[r].list_next, r_n);
            atomic_write (&uf->array[q].list_next, q_n);
            continue;
        }

        break;
    }

    // 9. update worker set
    sz_w q_w = atomic_read (&uf->array[q].p_set);
    or_fetch (&uf->array[r].p_set, q_w);

    // 10. update parent
#ifdef UFDEBUG
    HREassert(atomic_read (&uf->array[q].parent) == q,
            "parent for q shouldn't change while locked");
#endif
    atomic_write (&uf->array[q].parent, r);

    // 11. unlock
    atomic_write (&uf->array[q].uf_status, UF_LIVE);

#ifdef UFDEBUG
    HREassert (uf_sameset(uf, a, b), "states should be merged after a union");
#endif
    return 1;
}

#endif


#if UFVERSION == 4

pick_e
uf_pick_from_list (const uf_t *uf, ref_t state, ref_t *ret)
{
    // INVARIANT: every consecutive non-BUSY state is in the same set

    ref_t a, b, c;
    list_status a_status, b_status;

    a = state;
    // if we exit this loop, status = TOMBSTONE or we returned a LIVE state
    while (1) {
        a_status = atomic_read (&uf->array[a].list_status);

        // return directly if a is LIVE
        if (a_status == LIST_LIVE) {
            *ret = a;
            return PICK_SUCCESS;
        }

        // otherwise wait until a is TOMBSTONE (it might be BUSY now)
        else if (a_status == LIST_TOMBSTONE)
            break;
    }

    // find next state: a --> b
    b = atomic_read (&uf->array[a].list_next);

    while (1) {

        // if a is TOMBSTONE and only element, then the SCC is DEAD
        if (a == b) {
            if (uf_mark_dead (uf, a))
                return PICK_MARK_DEAD;
            return PICK_DEAD;
        }

        // if we exit this loop, status = TOMBSTONE or we returned a LIVE state
        while (1) {
            b_status = atomic_read (&uf->array[b].list_status);

            // return directly if b is LIVE
            if (b_status == LIST_LIVE) {
                *ret = b;
                return PICK_SUCCESS;
            }

            // otherwise wait until b is TOMBSTONE (it might be BUSY now)
            else if (b_status == LIST_TOMBSTONE)
                break;
        }

        c = atomic_read (&uf->array[b].list_next);

        // make the list shorter
        cas (&uf->array[a].list_next, b,  c);

        a = b;
        b = c;
    }
}

bool
uf_lock (const uf_t *uf, ref_t a)
{
    if (atomic_read (&uf->array[a].uf_status) == UF_LIVE) {
       if (cas (&uf->array[a].uf_status, UF_LIVE, UF_BUSY)) {
           // successfully locked
           // ensure that we actually locked the representative
           if (atomic_read (&uf->array[a].parent) == a)
               return 1;
           // otherwise unlock and try again
           atomic_write (&uf->array[a].uf_status, UF_LIVE);
       }
    }
    return 0;
}

void
uf_unlock (const uf_t *uf, ref_t a)
{
    atomic_write (&uf->array[a].uf_status, UF_LIVE);
}

bool
uf_lock_list (const uf_t *uf, ref_t a, ref_t *a_l)
{
    ref_t tmp; // TODO: test this without tmp
    pick_e pick_status;
    while (1) {
        // don't try to pick the roots
        pick_status = uf_pick_from_list (uf, a, &tmp);
        if (pick_status != PICK_SUCCESS)
            return 0;
        else if (cas (&uf->array[tmp].list_status, LIST_LIVE, LIST_BUSY)) {
            *a_l = tmp;
            return 1;
        }
    }
}

void
uf_unlock_list (const uf_t *uf, ref_t a_l)
{
    atomic_write (&uf->array[a_l].list_status, LIST_LIVE);
}

bool
uf_union (const uf_t *uf, ref_t a, ref_t b)
{
    ref_t a_r, b_r, a_l, b_l, a_n, b_n, r, q;

    while (1) {
        // 1. find the representatives
        a_r = uf_find (uf, a);
        b_r = uf_find (uf, b);

        if (a_r == b_r) {
            return 0;
        }

        // 2. decide on the new root (deterministically)
        r = a_r;
        q = b_r;
        if (a_r < b_r) { // take the highest index as root
            r = b_r;
            q = a_r;
        }

        // 3. lock the non-root
        if (!uf_lock (uf, q))
            continue;

        break;
    }

    // lock the list entries
    if (!uf_lock_list (uf, a, &a_l))
        return 0;
    if (!uf_lock_list (uf, b, &b_l)) {
        uf_unlock_list (uf, a_l);
        return 0;
    }

    // swap the list entries

    a_n = atomic_read (&uf->array[a_l].list_next);
    b_n = atomic_read (&uf->array[b_l].list_next);

    atomic_write (&uf->array[a_l].list_next, b_n);
    atomic_write (&uf->array[b_l].list_next, a_n);

    // 9. update worker set
    sz_w q_w = atomic_read (&uf->array[q].p_set);
    or_fetch (&uf->array[r].p_set, q_w); // might not be root anymore

    // 10. update parent
    atomic_write (&uf->array[q].parent, r);

    // 11. unlock
    uf_unlock_list (uf, a_l);
    uf_unlock_list (uf, b_l);
    uf_unlock (uf, q);

    return 1;
}

#endif


// testing

bool
uf_mark_undead (const uf_t *uf, ref_t state)
{
    // only used for testing

    bool result = 0;

    ref_t f = uf_find (uf, state);

    result = cas (&uf->array[f].uf_status, UF_DEAD, UF_LIVE);

    return result;
}

int
uf_print_list(const uf_t *uf, ref_t state)
{
    list_status l_status    = atomic_read(&uf->array[state].list_status);;
    ref_t next              = atomic_read(&uf->array[state].list_next);
    Warning(info, "Start: [%zu | %d] Dead: %d", state, l_status, uf_is_dead(uf, state));

    int cntr = 0;
    while (cntr++ < 5) {
        l_status            = atomic_read(&uf->array[next].list_status);
        next                = atomic_read(&uf->array[next].list_next);
        Warning(info, "Next:  [%zu | %d]", next, l_status);
    }
    return 0;
}

char*
uf_print_list_status (list_status ls)
{
    if (ls == LIST_LIVE)      return "LIVE";
    if (ls == LIST_BUSY)      return "BUSY";
    if (ls == LIST_TOMBSTONE) return "TOMB";
    else                      return "????";
}

char*
uf_print_uf_status (uf_status us)
{
    if (us == UF_UNSEEN) return "UNSN";
    if (us == UF_INIT)   return "INIT";
    if (us == UF_LIVE)   return "LIVE";
    if (us == UF_LOCKED) return "LOCK";
    if (us == UF_DEAD)   return "DEAD";
    else                 return "????";
}

void
uf_debug_aux (const uf_t *uf, ref_t state, int depth)
{ 
    if (depth == 0) {
        Warning(info, "\x1B[45mParent structure:\x1B[0m");
        Warning(info, "\x1B[45m%5s %10s %10s %6s %7s %7s %10s\x1B[0m",
                "depth",
                "state",
                "parent",
                "rank",
                "uf_s",
                "list_s",
                "next");
    }

    Warning(info, "\x1B[44m%5d %10zu %10zu %6d %7s %7s %10zu\x1B[0m",
        depth,
        state,
        atomic_read(&uf->array[state].parent),  
        atomic_read(&uf->array[state].rank),  
        uf_print_uf_status(atomic_read(&uf->array[state].uf_status)),
        uf_print_list_status(atomic_read(&uf->array[state].list_status)),
        atomic_read(&uf->array[state].list_next));



    if (uf->array[state].parent != state) {
        uf_debug_aux (uf, uf->array[state].parent, depth+1);
    }
}

void
uf_debug_list (const uf_t *uf, ref_t start, ref_t state, int depth)
{
    if (depth == 10) {
        Warning(info, "\x1B[40mreached depth 10\x1B[0m");
        return;
    }
    if (depth == 0) {
        Warning(info, "\x1B[40mList structure:\x1B[0m");
        Warning(info, "\x1B[40m%5s %10s %10s\x1B[0m",
                    "depth",
                    "state",
                    "next");
    }

    Warning(info, "\x1B[41m%5d %10zu %10zu\x1B[0m",
        depth,
        state,
        atomic_read(&uf->array[state].list_next));

    if (uf->array[state].list_next != start) {
        uf_debug_list (uf, start, uf->array[state].list_next, depth+1);
    }
}

int
uf_debug (const uf_t *uf, ref_t state)
{
    uf_debug_aux (uf, state, 0);
    //ref_t f = uf_find(uf, state);
    //uf_debug_list (uf, state, state, 0);

    return 1;
}

void         
uf_free (uf_t *uf)
{
    RTfree(uf->array);
    RTfree(uf);
}

