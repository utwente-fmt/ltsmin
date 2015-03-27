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

struct uf_node_s {
    sz_w            w_set;                  // Set of worker IDs (one bit for each worker)
    ref_t           parent;                 // The parent in the UF tree
    ref_t           list_next;              // next list `pointer' (we could also use a standard pointer)
    unsigned char   rank;                   // The height of the UF tree
    unsigned char   uf_status;              // {UNSEEN, INIT, LIVE, LOCKED, DEAD}
    unsigned char   list_status;            // {LIVE, BUSY, REMOVED}
    char            pad[5];                 // padding to ensure that 64 bit fields are aligned properly in array
};

typedef struct uf_node_s uf_node_t;

struct uf_s {
    uf_node_t      *array;   // array: [ref_t] -> uf_node
};

uf_t *
uf_create ()
{
    HREassert (sizeof(uf_node_t) == sizeof(int[8]),
               "Improper structure packing for uf_node_t. Expected: size = %zu", sizeof(int[8]));
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
// NO superfluous checks on the UF root for things that may be done local
//    // check if the state is dead (= no successors)
//    if (uf_is_dead(uf, state)) {
//        return PICK_DEAD;
//    }

    HREassert (atomic_read(&uf->array[state].list_status) == LIST_TOMBSTONE);

    // compress list by lazily removing tombstone sequences
    ref_t s     = state;
    ref_t n1    = atomic_read(&uf->array[s].list_next);
    ref_t n2;

    while (atomic_read(&uf->array[s].list_status) == LIST_TOMBSTONE) {

        if (s == n1) {
            if (uf_mark_dead(uf, state))
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

    *node = s; // set the node reference
    return PICK_SUCCESS;
}

void    
uf_remove_from_list (const uf_t* uf, ref_t state)
{
    // only remove if it has LIST_LIVE , otherwise (LIST_BUSY) wait
    while (true) {
        // Poll first (cheap)
        list_status list_s = atomic_read(&uf->array[state].list_status);
        if (list_s == LIST_LIVE) {
            if (cas(&uf->array[state].list_status, LIST_LIVE, LIST_TOMBSTONE))
                break;
        } else if (list_s == LIST_TOMBSTONE)
            break;
    }
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
    if (cas(&uf->array[state].uf_status, UF_UNSEEN, UF_INIT)) {
        // create state and add worker

        atomic_write (&uf->array[state].parent, state);
        atomic_write (&uf->array[state].list_next, state);
        uf->array[state].w_set = w_id;
        atomic_write (&uf->array[state].uf_status, UF_LIVE);

        return CLAIM_FIRST;
    }

    // Is someone currently initializing the state?
    while (atomic_read(&uf->array[state].uf_status) == UF_INIT);
    ref_t f = uf_find(uf, state); 
    // Is the state dead?
    if (atomic_read(&uf->array[f].uf_status) == UF_DEAD) {
        return CLAIM_DEAD;
    }
    // Did I already explore `this' state?
    if (((uf->array[f].w_set) & w_id ) != 0) {
        return CLAIM_FOUND;
        // NB: cycle is possibly missed (in case f got updated)
        // - but next iterations should fix this
    }
    // Add our worker ID to the set (and make sure it is the UF representative)
    or_fetch (&uf->array[f].w_set, w_id);
    while (atomic_read(&uf->array[f].parent) != f ||
            atomic_read(&uf->array[f].uf_status) == UF_LOCKED) {
        f = atomic_read(&uf->array[f].parent);
        //while (uf->array[f].uf_status == UF_LOCKED); // not sure if this helps
        or_fetch (&uf->array[f].w_set, w_id);
        if (atomic_read(&uf->array[f].uf_status) == UF_DEAD) {
            return CLAIM_DEAD;
        }
    }
    return CLAIM_SUCCESS;
}

#define REF_ERROR ((size_t)-1)

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
        HREassert(atomic_read(&uf->array[x].list_status) != LIST_BUSY);

        if (cas(&uf->array[x].list_status, LIST_LIVE, LIST_BUSY)) {
            return x;
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

// 'basic' union find

ref_t
uf_find (const uf_t* uf, ref_t state)
{
    // recursively find and update the parent (path compression)
    ref_t parent = atomic_read(&uf->array[state].parent);
    if (parent != state) {
        atomic_write(&uf->array[state].parent, uf_find (uf, parent));
    }

    return atomic_read(&uf->array[state].parent);
}

bool
uf_sameset (const uf_t* uf, ref_t state_x, ref_t state_y)
{
    // TODO check if correct

    ref_t x_f = uf_find(uf, state_x);
    ref_t y_f = uf_find(uf, state_y);

    // x_f may change during find(y), 
    // - if so, try again
    // - otherwise, x_f is unchanged after finding y_f => sameset holds
    ref_t x_p = atomic_read(&uf->array[x_f].parent);
    ref_t y_p = atomic_read(&uf->array[y_f].parent); // should not be needed

    if ( x_f != x_p || y_f != y_p  ||
        atomic_read(&uf->array[x_f].uf_status) == UF_LOCKED ||
        atomic_read(&uf->array[y_f].uf_status) == UF_LOCKED) {
        // if parent got updated, try again
        return uf_sameset(uf, x_p, y_p);
    }

    return x_f == y_f;
}

void
uf_union_aux (const uf_t* uf, ref_t root, ref_t other)
{
    // don't need CAS because the states are locked
    uf->array[root].w_set   |= uf->array[other].w_set;
    atomic_write(&uf->array[other].parent, root);
}

void
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
    }
    HREassert (uf_sameset(uf, state_x, state_y), "uf_union: states should be in the same set");
}

// dead

bool
uf_mark_dead (const uf_t* uf, ref_t state) 
{
    // returns if it has marked the state dead
    bool result = false;

    ref_t f = uf_find(uf, state); 

    while (!uf_is_dead(uf, f)) {
        f = uf_find(uf, f); 

        uf_status tmp = atomic_read (&uf->array[f].uf_status);
        if (tmp == UF_LIVE) {
            result = cas (&uf->array[f].uf_status, tmp, UF_DEAD);
        }
    }


    HREassert (uf_is_dead(uf, state), "state should be dead");

    return result;
}

bool
uf_is_dead (const uf_t* uf, ref_t state)
{
    ref_t f = uf_find(uf, state); 
    return atomic_read(&uf->array[f].uf_status) == UF_DEAD;
}


// locking

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
        if (cas(&uf->array[a].uf_status, UF_LIVE, UF_LOCKED)) {
            if (atomic_read(&uf->array[a].parent) == a) {
                // a is successfully locked
                if (cas(&uf->array[b].uf_status, UF_LIVE, UF_LOCKED)) {
                    if (atomic_read(&uf->array[b].parent) == b) {
                        // b is successfully locked
                        return true;
                    } 
                    atomic_write (&uf->array[b].uf_status, UF_LIVE);
                } 
            } 
            atomic_write (&uf->array[a].uf_status, UF_LIVE);
        }
    }
}

void      
uf_unlock (const uf_t* uf, ref_t state)
{
    HREassert(uf->array[state].uf_status == UF_LOCKED)
    atomic_write (&uf->array[state].uf_status, UF_LIVE);
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

static char *
uf_get_str_w_set(sz_w w_set) 
{
    char *s = RTmalloc ((W + 1)*sizeof(char));
    s[W] = '\0';
    sz_w i = 0;
    while (i < W) {
        s[W - 1 - i] = ((w_set & (1ULL << i)) != 0) ? '1' : '0';
        i++;
    }
    return s;
}

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
    Warning(info, "state:  %zu\t, parent: %zu\t, rank:   %d\t, uf_status: %d\t, list_status: %d\t,  w_set:  %s",
        state, 
        atomic_read(&uf->array[state].parent),  
        atomic_read(&uf->array[state].rank),  
        atomic_read(&uf->array[state].uf_status),  
        atomic_read(&uf->array[state].list_status),  
        uf_get_str_w_set(uf->array[state].w_set));
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
