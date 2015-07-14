#include <hre/config.h>

#include <mc-lib/unionfind.h>

#include <mc-lib/atomics.h>
#include <util-lib/util.h>


typedef enum uf_status_e {
    UF_UNSEEN     = 0, // initial
    UF_INIT       = 1,
    UF_LIVE       = 2,
    UF_LOCKED     = 3,
    UF_DEAD       = 4
} uf_status;

typedef enum list_status_e {
    LIST_LIVE     = 0, // initial
    LIST_BUSY     = 1,
    LIST_TOMBSTONE  = 2
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
    HREassert (sizeof(uf_node_t) == sizeof(int[8]), "Improper structure packing for uf_node_t. Expected: size = %zu", sizeof(int[8]));
    //HREassert (sizeof(sz_w)*8 >= W, "Too many workers for the current structure; please redefine sz_w to a larger size");
    uf_t           *uf = RTmalloc (sizeof(uf_t));
    uf->array          = RTalignZero ( sizeof(int[8]),
                                       sizeof(uf_node_t) * (1ULL << dbs_size) );
    return uf;
}

// successor handling

pick_e
uf_pick_from_list (const uf_t* uf, ref_t state, ref_t *node) 
{

    HREassert (atomic_read(&uf->array[state].list_status) == LIST_TOMBSTONE);

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


ref_t
uf_pick_from_list2 (const uf_t* uf, ref_t state) 
{

    //HREassert (atomic_read(&uf->array[state].list_status) != LIST_LIVE);

    // compress list by lazily removing tombstone sequences
    ref_t s     = state;
    ref_t n1    = atomic_read(&uf->array[s].list_next);
    ref_t n2;

    while (atomic_read(&uf->array[s].list_status) != LIST_LIVE) {

        list_status s_status = atomic_read(&uf->array[s].list_status);
        if (s == n1 ) {
            if (s_status == LIST_TOMBSTONE)
                return REF_ERROR;
        }

        // [s|T] -> [n1|T] -> [n2|?] (iff s==TOMB && n1==TOMB)
        // [s|T] -----------> [n2|?] (update next pointer from s)
        list_status n1_status = atomic_read(&uf->array[n1].list_status);
        if (n1_status != LIST_LIVE) {

            n2 = atomic_read(&uf->array[n1].list_next);
            if (s == n2) {
                if (s_status == LIST_TOMBSTONE)
                    return REF_ERROR;
            }

            if (s_status == LIST_TOMBSTONE && n1_status == LIST_TOMBSTONE) {
                atomic_write (&uf->array[s].list_next, n2);
            }

            s  = n2;
            n1 = atomic_read(&uf->array[s].list_next);
        } else { 

            s = n1;
            break;
        }
    }
    atomic_write (&uf->array[state].list_next, s);

    //while (atomic_read(&uf->array[s].list_status) == LIST_BUSY) ; // wait for busy states 

    return s;
}

bool    
uf_remove_from_list (const uf_t* uf, ref_t state)
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
uf_is_in_list (const uf_t* uf, ref_t state)
{
    return atomic_read (&uf->array[state].list_status) != LIST_TOMBSTONE;
}

char     
uf_make_claim (const uf_t* uf, ref_t state, size_t worker)
{
    HRE_ASSERT (worker < WORKER_BITS);
    sz_w            w_id = 1ULL << worker;

    // Is the state unseen? ==> Initialize it
    if (atomic_read(&uf->array[state].uf_status) == UF_UNSEEN) {
        if (cas(&uf->array[state].uf_status, UF_UNSEEN, UF_INIT)) {
            // create state and add worker

            atomic_write (&uf->array[state].parent, state);
            atomic_write (&uf->array[state].list_next, state);
            uf->array[state].p_set = w_id;
            atomic_write (&uf->array[state].uf_status, UF_LIVE);

            return CLAIM_FIRST;
        }
    }

    // Is someone currently initializing the state?
    while (atomic_read(&uf->array[state].uf_status) == UF_INIT);
    ref_t f = uf_find(uf, state); 
    // Is the state dead?
    if (atomic_read(&uf->array[f].uf_status) == UF_DEAD) {
        return CLAIM_DEAD;
    }
    // Did I already explore `this' state?
    if (((uf->array[f].p_set) & w_id ) != 0) {
        return CLAIM_FOUND;
        // NB: cycle is possibly missed (in case f got updated)
        // - but next iterations should fix this
    }
    // Add our worker ID to the set (and make sure it is the UF representative)
    or_fetch (&uf->array[f].p_set, w_id);
    while (atomic_read(&uf->array[f].parent) != f ||
            atomic_read(&uf->array[f].uf_status) == UF_LOCKED) {
        f = atomic_read(&uf->array[f].parent);
        //while (uf->array[f].uf_status == UF_LOCKED); // not sure if this helps
        or_fetch (&uf->array[f].p_set, w_id);
        if (atomic_read(&uf->array[f].uf_status) == UF_DEAD) {
            return CLAIM_DEAD;
        }
    }
    return CLAIM_SUCCESS;
}



// 'basic' union find

ref_t
uf_find (const uf_t* uf, ref_t state)
{
    // recursively find and update the parent (path compression)
    ref_t parent = atomic_read(&uf->array[state].parent);
    if (parent == state)
        return parent;
    ref_t root = uf_find (uf, parent);
    if (root != parent)
        atomic_write(&uf->array[state].parent, root);
    return root;
}

bool
uf_sameset (const uf_t* uf, ref_t state_x, ref_t state_y)
{
    ref_t x_f = state_x;
    ref_t y_f = state_y;
    ref_t x_p;

    while (1) {
        x_f = uf_find (uf, x_f);
        y_f = uf_find (uf, y_f);

        if (x_f == y_f) return true;
        x_p = atomic_read (&uf->array[x_f].parent);
        if (x_p == x_f) return false;
    }
}

// lockless union-find

void     
uf_merge_list2(const uf_t* uf, ref_t x, ref_t y)
{
    HREassert(atomic_read(&uf->array[x].list_status) == LIST_BUSY);
    HREassert(atomic_read(&uf->array[y].list_status) == LIST_BUSY);

    // SWAP (x.next, y.next)
    ref_t tmp = atomic_read(&uf->array[x].list_next);
    atomic_write(&uf->array[x].list_next, atomic_read(&uf->array[y].list_next));
    atomic_write(&uf->array[y].list_next, tmp);


    atomic_write(&uf->array[x].list_status, LIST_LIVE);
    atomic_write(&uf->array[y].list_status, LIST_LIVE);
}

ref_t
pick_list_live (const uf_t *uf, ref_t state) 
{
    // helper method for lock_lists
    // ensures that returned state is LIVE, not TOMB nor BUSY
    ref_t s = state;

    while (1) {
        // check if state is not dead
        if ( uf_is_dead(uf, s) )
            return REF_ERROR;

        list_status l_status = atomic_read(&uf->array[s].list_status);

        if (l_status == LIST_LIVE) {
            return s;
        } 
        else { // TOMB or BUSY
            s = atomic_read(&uf->array[s].list_next);
            // TODO: use uf_pick_from_list for this (but don't mark dead!)
        }
    }
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
        a = uf_pick_from_list2 (uf, a);
        b = uf_pick_from_list2 (uf, b);
    
        if (a == REF_ERROR || b == REF_ERROR) { // DEAD check
            if (!uf_sameset(uf, list_a, list_b)) {
                HREassert ( false );
            }
            return false;
        }

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

bool
uf_union2 (const uf_t *uf, ref_t state_x, ref_t state_y)
{
    // 1. Lock list entries
    // 2. Update UF parent  ==> SameSet(x,y)
    // 3. Update Pset (may be outdated)
    // 4. Update rank (may be outdated)
    // 5. Merge lists and unlock list entries

    bool result = false;
    ref_t x_f, y_f, root, other;
    ref_t locked_a = state_x;
    ref_t locked_b = state_y;
    sz_w workers;

    // check if both states are not dead
    HREassert ( !uf_is_dead (uf, state_x) );
    HREassert ( !uf_is_dead (uf, state_y) );

    // 1. lock list entries for x and y
    if (!lock_lists (uf, state_x, state_y, &locked_a, &locked_b)) {
        HREassert (uf_sameset (uf, state_x, state_y));
        return false;
    }

    HREassert(atomic_read(&uf->array[locked_a].list_status) == LIST_BUSY);
    HREassert(atomic_read(&uf->array[locked_b].list_status) == LIST_BUSY);


    while (!result) {
        x_f = uf_find (uf, state_x);
        y_f = uf_find (uf, state_y);

        if (x_f == y_f) { // SameSet(x,y)
            atomic_write (&uf->array[locked_a].list_status, LIST_LIVE);
            atomic_write (&uf->array[locked_b].list_status, LIST_LIVE);
            return false;
        }
        
        root  = x_f;
        other = y_f;

        // swap(root,other) if root.rank < other.rank
        if (uf->array[x_f].rank < uf->array[y_f].rank) { 
            root  = y_f;
            other = x_f;
        } 

        // 2. set other.parent = root
        if (atomic_read (&uf->array[other].parent) == other) {
            result = cas(&uf->array[other].parent, other, root);
            if (uf->array[root].parent != root) 
                result = false;
        }
    }

    HREassert (uf_sameset (uf, root, other));

    // 3. combine the worker sets and update rank
    workers = uf->array[root].p_set | uf->array[other].p_set;
    atomic_write (&uf->array[root].p_set, workers);

    // 4. increment the rank if it is equal
    if (uf->array[root].rank == uf->array[other].rank) {
        uf->array[root].rank ++;
    }

    // 5. merge the lists (and unlock)
    uf_merge_list2 (uf, locked_a, locked_b);

    return true;
}


// original union-find


static inline ref_t
lock_node (const uf_t* uf, ref_t x)
{
    while (true) {
        while (atomic_read(&uf->array[x].list_status) == LIST_TOMBSTONE) {
            pick_e result = uf_pick_from_list (uf, x, &x);

            if (result == PICK_DEAD || result == PICK_MARK_DEAD) {
                return REF_ERROR;
            }
        }

        // No other workers can merge lists (states are locked)
        //HREassert(atomic_read(&uf->array[x].list_status) != LIST_BUSY);

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
uf_merge_list(const uf_t* uf, ref_t list_x, ref_t list_y)
{
    // assert \exists x' \in List(x) (also for y) 
    // - because states are locked and union(x,y) did not take place yet

    HREassert(atomic_read(&uf->array[list_x].uf_status) == UF_LOCKED);
    HREassert(atomic_read(&uf->array[list_y].uf_status) == UF_LOCKED);
    HREassert(uf_find(uf, list_x) != uf_find(uf, list_y));

    ref_t x = list_x;
    ref_t y = list_y;

    x = lock_node (uf, x);
    //if (x == REF_ERROR) return; 
    // no merge needed (x became empty, which might happen 
    // if other threads united these lists and subsequently processed them)
    y = lock_node (uf, y);
    HREassert (x != REF_ERROR && y != REF_ERROR, "Contradiction: non-tombstone x was locked");

    HREassert(atomic_read(&uf->array[x].list_status) == LIST_BUSY);
    HREassert(atomic_read(&uf->array[y].list_status) == LIST_BUSY);

    //swap (uf->array[x].list_next, uf->array[y].list_next);
    // unsure if swap preserves atomic operations - therefore just do it 
    ref_t tmp = atomic_read(&uf->array[x].list_next);
    atomic_write(&uf->array[x].list_next, atomic_read(&uf->array[y].list_next));
    atomic_write(&uf->array[y].list_next, tmp);


    atomic_write(&uf->array[x].list_status, LIST_LIVE);
    atomic_write(&uf->array[y].list_status, LIST_LIVE);
}


ref_t      
uf_lock (const uf_t* uf, ref_t state_x, ref_t state_y)
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
uf_unlock (const uf_t* uf, ref_t state)
{
    HREassert(uf->array[state].uf_status == UF_LOCKED)
    atomic_write (&uf->array[state].uf_status, UF_LIVE);
}


void
uf_union_aux (const uf_t* uf, ref_t root, ref_t other)
{
    // don't need CAS because the states are locked
    uf->array[root].p_set   |= uf->array[other].p_set;
    atomic_write(&uf->array[other].parent, root);
}

bool
uf_union (const uf_t* uf, ref_t state_x, ref_t state_y)
{
    if (uf_lock(uf, state_x, state_y)) {
        HREassert(!uf_is_dead(uf, state_x));
        HREassert(!uf_is_dead(uf, state_y));
        //HREassert (!uf_sameset(uf, state_x, state_y), "uf_union: states should not be in the same set");

        ref_t x_f = uf_find(uf, state_x);
        ref_t y_f = uf_find(uf, state_y);

        // Combine the successors BEFORE the union 
        // - ensures that there is a successor v with list_status != LIST_REMOVED

        uf_merge_list(uf, x_f, y_f);

        if (x_f > y_f) {
            uf_union_aux(uf, x_f, y_f); // x_f is the new root
        } else {
            uf_union_aux(uf, y_f, x_f); // y_f is the new root
        }

        /*if (uf->array[x_f].rank > uf->array[y_f].rank) {
            uf_union_aux(uf, x_f, y_f); // x_f is the new root
        } else { 
            uf_union_aux(uf, y_f, x_f); // y_f is the new root

            // increment the rank if it is equal
            if (uf->array[x_f].rank == uf->array[y_f].rank) {
                uf->array[y_f].rank ++;
            }
        }*/


        uf_unlock(uf, x_f);
        uf_unlock(uf, y_f);
        HREassert (uf_sameset(uf, state_x, state_y), "uf_union: states should be in the same set");
        return 1;
    }
    else {
        HREassert (uf_sameset(uf, state_x, state_y), "uf_union: states should be in the same set");
        return 0;
    }
}


// combined


bool
uf_union3 (const uf_t* uf, ref_t state_x, ref_t state_y)
{
    // 1. lock list entries for x and y
    // 2. lock representatives
    // - update parent
    // - update pset
    // 3. merge lists

    ref_t x_f, y_f;
    ref_t locked_a = state_x;
    ref_t locked_b = state_y;

    // check if both states are not dead
    //HREassert ( !uf_is_dead (uf, state_x) ); // TODO: may be merged already -> dead possibility
    //HREassert ( !uf_is_dead (uf, state_y) );

    // 1. lock list entries for x and y
    if (!lock_lists (uf, state_x, state_y, &locked_a, &locked_b)) {
        HREassert (uf_sameset (uf, state_x, state_y));
        return false;
    }

    HREassert(atomic_read(&uf->array[locked_a].list_status) == LIST_BUSY);
    HREassert(atomic_read(&uf->array[locked_b].list_status) == LIST_BUSY);



    // 2. lock representatives
    if (uf_lock(uf, state_x, state_y)) {
        HREassert(!uf_is_dead(uf, state_x));
        HREassert(!uf_is_dead(uf, state_y));
        //HREassert (!uf_sameset(uf, state_x, state_y), "uf_union: states should not be in the same set");

        x_f = uf_find(uf, state_x);
        y_f = uf_find(uf, state_y);

        // Combine the successors BEFORE the union 
        // - ensures that there is a successor v with list_status != LIST_REMOVED

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
        HREassert (uf_sameset(uf, state_x, state_y), "uf_union: states should be in the same set");



        // 5. merge the lists (and unlock)
        uf_merge_list2 (uf, locked_a, locked_b);



        return 1;
    }
    else {

        // unlock lists
        atomic_write (&uf->array[locked_a].list_status, LIST_LIVE);
        atomic_write (&uf->array[locked_b].list_status, LIST_LIVE);


        HREassert (uf_sameset(uf, state_x, state_y), "uf_union: states should be in the same set");
        return 0;
    }
}




// dead

bool
uf_mark_dead (const uf_t* uf, ref_t state) 
{
    // returns if it has marked the state dead
    bool result = false;

    ref_t f = uf_find(uf, state); 

    /*while (!uf_is_dead(uf, f)) {
        f = uf_find(uf, f); 

        uf_status tmp = atomic_read (&uf->array[f].uf_status);
        if (tmp == UF_LIVE) {
            result = cas (&uf->array[f].uf_status, tmp, UF_DEAD);
        }
    }*/
    uf_status tmp = atomic_read (&uf->array[f].uf_status);

    while (tmp == UF_LOCKED) {
        tmp = atomic_read (&uf->array[f].uf_status);
    }
    
    HREassert (tmp != UF_LOCKED); // this can fail


    if (tmp == UF_LIVE) 
        result = cas (&uf->array[f].uf_status, tmp, UF_DEAD);

    HREassert (atomic_read (&uf->array[f].parent) == f);

    HREassert (uf_is_dead(uf, state), "state should be dead");

    return result;
}

bool
uf_is_dead (const uf_t* uf, ref_t state)
{
    ref_t f = uf_find(uf, state); 
    return atomic_read(&uf->array[f].uf_status) == UF_DEAD;
}

// testing

bool
uf_mark_undead (const uf_t* uf, ref_t state) 
{
    // only used for testing

    bool result = 0;

    ref_t f = uf_find (uf, state);

    result = cas (&uf->array[f].uf_status, UF_DEAD, UF_LIVE);

    return result;
}

/*static char *
uf_get_str_p_set(sz_w p_set) 
{
    char *s = RTmalloc ((W + 1)*sizeof(char));
    s[W] = '\0';
    sz_w i = 0;
    while (i < W) {
        s[W - 1 - i] = ((p_set & (1ULL << i)) != 0) ? '1' : '0';
        i++;
    }
    return s;
}*/

int
uf_print_list(const uf_t* uf, ref_t state)
{
    list_status l_status    = atomic_read(&uf->array[state].list_status);;
    ref_t next              = atomic_read(&uf->array[state].list_next);
    Warning(info, "Start: [%zu | %d] Dead: %d", state, l_status, uf_is_dead(uf, state));

    int cntr = 0;
    while (cntr++ < 50) {
        l_status            = atomic_read(&uf->array[next].list_status);
        next                = atomic_read(&uf->array[next].list_next);
        Warning(info, "Next:  [%zu | %d]", next, l_status);
    }
    return 0;
}

int         
uf_debug (const uf_t* uf, ref_t state) 
{ 
    Warning(info, "state:  %zu\t, parent: %zu\t, rank:   %d\t, uf_status: %d\t, list_status: %d\t",
        state, 
        atomic_read(&uf->array[state].parent),  
        atomic_read(&uf->array[state].rank),  
        atomic_read(&uf->array[state].uf_status),  
        atomic_read(&uf->array[state].list_status));
    if (uf->array[state].parent != state) {
        uf_debug (uf, uf->array[state].parent); 
    }
    return 0;
}

void         
uf_free (uf_t* uf)
{
    RTfree(uf->array);
    RTfree(uf);
}

