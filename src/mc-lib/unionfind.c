/**
 *
 */

#include <hre/config.h>

#include <mc-lib/unionfind.h>
#include <mc-lib/atomics.h>
#include <util-lib/util.h>


// #define UFDEBUG
typedef uint64_t sz_w;
#define WORKER_BITS 64


typedef enum uf_status_e {
    UF_LIVE           = 0,                    // LIVE state
    UF_LOCK           = 1,                    // prevent other workers from
                                              //   updating the parent
    UF_DEAD           = 2,                    // completed SCC
} uf_status;


typedef enum list_status_e {
    LIST_LIVE         = 0,                    // LIVE (initial value)
    LIST_LOCK         = 1,                    // busy merging lists
    LIST_TOMB         = 2,                    // fully explored state
} list_status;


/**
 * information per UF state
 */
struct uf_state_s {
    sz_w                p_set;                // set of worker IDs
                                              // (one bit for each worker)
    ref_t               parent;               // the parent in the UF tree
    ref_t               list_next;            // next list item 'pointer'
    uint32_t            acc_set;              // TGBA acceptance set
    unsigned char       uf_status;            // the UF status of the state
    unsigned char       list_status;          // the list status
    char                pad[2];               // padding for data alignment
};


/**
 * shared array of UF states
 */
struct uf_s {
    uf_state_t         *array;                // array[ref_t] ->uf_state_t
};


static bool      uf_lock_uf (const uf_t *uf, ref_t a);
static void      uf_unlock_uf (const uf_t *uf, ref_t a);
static bool      uf_lock_list (const uf_t *uf, ref_t a, ref_t *a_l);
static void      uf_unlock_list (const uf_t *uf, ref_t a_l);

/**
 * initializer for the UF array
 */
uf_t *
uf_create ()
{
    HREassert (sizeof (uf_state_t) == sizeof (int[8]),
               "Improper structure for uf_state_t. Expected: size = %zu",
               sizeof (int[8]));

    uf_t               *uf = RTmalloc    (sizeof (uf_t));
    // allocate one entry extra since [0] is not used
    uf->array              = RTalignZero (sizeof(int[8]),
                             sizeof (uf_state_t) * ( (1ULL << dbs_size) + 1 ) );
    return uf;
}


/* **************************** list operations **************************** */


bool
uf_is_in_list (const uf_t *uf, ref_t state)
{
    return (atomic_read (&uf->array[state].list_status) != LIST_TOMB);
}



/**
 * searches the first LIVE state in the cyclic list, starting from state:
 * if all elements in the list are TOMB, we mark the SCC DEAD
 * if three consecutive items a -> b -> c with a  == TOMB and b == TOMB :
 *   we try to update a -> c (and thereby reducing the size of the cyclic list)
 */
pick_e
uf_pick_from_list (const uf_t *uf, ref_t state, ref_t *ret)
{
    // invariant: every consecutive non-LOCK state is in the same set
    ref_t               a, b, c;
    list_status         a_status, b_status;

    a = state;

    while ( 1 ) {
        // HREassert ( a != 0 );

        // if we exit this loop, a.status == TOMB or we returned a LIVE state
        while ( 1 ) {
            a_status = atomic_read (&uf->array[a].list_status);

            // return directly if a is LIVE
            if (a_status == LIST_LIVE) {
                *ret = a;
                return PICK_SUCCESS;
            }

            // otherwise wait until a is TOMB (it might be LOCK now)
            else if (a_status == LIST_TOMB)
                break;
        }

        // find next state: a --> b
        b = atomic_read (&uf->array[a].list_next);

        // if a is TOMB and only element, then the SCC is DEAD

        if (a == b || b == 0) {
            if ( uf_mark_dead (uf, a) )
                return PICK_MARK_DEAD;
            return PICK_DEAD;
        }

        // if we exit this loop, b.status == TOMB or we returned a LIVE state
        while ( 1 ) {
            b_status = atomic_read (&uf->array[b].list_status);

            // return directly if b is LIVE
            if (b_status == LIST_LIVE) {
                *ret = b;
                return PICK_SUCCESS;
            }

            // otherwise wait until b is TOMB (it might be LOCK now)
            else if (b_status == LIST_TOMB)
                break;
        }

        // a --> b --> c
        c = atomic_read (&uf->array[b].list_next);

        // HREassert ( c != 0 );

        // make the list shorter (a --> c)
        //cas (&uf->array[a].list_next, b, c);
        atomic_write (&uf->array[a].list_next, c);

        a = c; // continue searching from c
    }
}


bool
uf_remove_from_list (const uf_t *uf, ref_t state)
{
    list_status         list_s;

    // only remove list item if it is LIVE , otherwise (LIST_LOCK) wait
    while ( true ) {
        list_s = atomic_read (&uf->array[state].list_status);
        if (list_s == LIST_LIVE) {
            if (cas (&uf->array[state].list_status, LIST_LIVE, LIST_TOMB) )
                return 1;
        } else if (list_s == LIST_TOMB)
            return 0;
    }
}


/* ********************* 'basic' union find operations ********************* */


/**
 * return -1 for states owner by other workers
 * return 1 for states locally owned
 */
int
uf_owner (const uf_t *uf, ref_t state, size_t worker)
{
    sz_w                w_id = 1ULL << worker;
    sz_w                W    = atomic_read (&uf->array[state].p_set);
    return W & w_id ? 1 : (W & ~w_id ? -1 : 0);
}


/**
 * returns:
 * - CLAIM_FIRST   : if we initialized the state
 * - CLAIM_FOUND   : if the state is LIVE and we have visited its SCC before
 * - CLAIM_SUCCESS : if the state is LIVE and we have not yet visited its SCC
 * - CLAIM_DEAD    : if the state is part of a completed SCC
 */
char     
uf_make_claim (const uf_t *uf, ref_t state, size_t worker)
{
    HREassert (worker < WORKER_BITS);

    sz_w                w_id   = 1ULL << worker;
    ref_t               f      = uf_find (uf, state);
    sz_w                orig_pset;

    // is the state dead?
    if (atomic_read (&uf->array[f].uf_status) == UF_DEAD)
        return CLAIM_DEAD;

    // did we previously explore a state in this SCC?
    if ( (atomic_read (&uf->array[f].p_set) & w_id ) != 0) {
        return CLAIM_FOUND;
        // NB: cycle is possibly missed (in case f got updated)
        // - however, next iteration should detect this
    }

    // Add our worker ID to the set, and ensure it is the UF representative
    orig_pset = fetch_or (&uf->array[f].p_set, w_id);
    while ( atomic_read (&uf->array[f].parent) != 0 ) {
        f = uf_find (uf, f);
        fetch_or (&uf->array[f].p_set, w_id);
    }
    if (orig_pset == 0ULL)
        return CLAIM_FIRST;
    else
        return CLAIM_SUCCESS;
}


/**
 * returns the representative for the UF set
 */
ref_t
uf_find (const uf_t *uf, ref_t state)
{
    // recursively find and update the parent (path compression)
    ref_t               x      = state;
    ref_t               parent = atomic_read (&uf->array[x].parent);
    ref_t               y;

    while (parent != 0) {
        y = parent;
        parent = atomic_read (&uf->array[y].parent);
        if (parent == 0) {
            return y;
        }
        atomic_write (&uf->array[x].parent, parent);
        x = parent;
        parent = atomic_read (&uf->array[x].parent);
    }
    return x;
}


/**
 * returns whether or not a and b reside in the same UF set
 */
bool
uf_sameset (const uf_t *uf, ref_t a, ref_t b)
{
    // TODO: try to improve performance (if necessary)
    ref_t               a_r = uf_find (uf, a);
    ref_t               b_r = uf_find (uf, b);

    // return true if the representatives are equal
    if (a_r == b_r)
        return 1;

    // return false if the parent for a has not been updated
    if (atomic_read (&uf->array[a_r].parent) == 0)
        return 0;

    // otherwise retry
    else
        return uf_sameset (uf, a_r, b_r);
}


/**
 * unites two sets and ensures that their cyclic lists are combined to one list
 */
bool
uf_union (const uf_t *uf, ref_t a, ref_t b)
{
    if (a == b) return 1;
    
    ref_t               a_r, b_r, a_l, b_l, a_n, b_n, r, q;
    sz_w                q_w, r_w;
    uint32_t            q_a, r_a;

    while ( 1 ) {

        a_r = uf_find (uf, a);
        b_r = uf_find (uf, b);

        // find the representatives
        if (a_r == b_r) {
            return 0;
        }

        // decide on the new root (deterministically)
        // take the highest index as root
        r = a_r;
        q = b_r;
        if (a_r < b_r) {
            r = b_r;
            q = a_r;
        }

        // lock the non-root
        if ( !uf_lock_uf (uf, q) )
            continue;

        break;
    }

    // lock the list entries
    if ( !uf_lock_list (uf, a, &a_l) ) {
        // HREassert ( uf_is_dead(uf, a) && uf_sameset(uf, a, b) );
        return 0;
    }
    if ( !uf_lock_list (uf, b, &b_l) ) {
        // HREassert ( uf_is_dead(uf, b) && uf_sameset(uf, a, b) );
        uf_unlock_list (uf, a_l);
        return 0;
    }

    // swap the list entries
    a_n = atomic_read (&uf->array[a_l].list_next);
    b_n = atomic_read (&uf->array[b_l].list_next);

    if (a_n == 0) // singleton
        a_n = a_l;

    if (b_n == 0) // singleton
        b_n = b_l;

    atomic_write (&uf->array[a_l].list_next, b_n);
    atomic_write (&uf->array[b_l].list_next, a_n);

    // update parent
    atomic_write (&uf->array[q].parent, r);


    // only update acceptance set for r if q adds acceptance marks
    r_a = atomic_read (&uf->array[r].acc_set);
    q_a = atomic_read (&uf->array[q].acc_set);
    if ( (q_a | r_a) != r_a) {
        // update!
        fetch_or (&uf->array[r].acc_set, q_a);
        while (atomic_read (&uf->array[r].parent) != 0) {
            r = uf_find (uf, r);
            fetch_or (&uf->array[r].acc_set, q_a);
        }
    }

    // only update worker set for r if q adds workers
    q_w = atomic_read (&uf->array[q].p_set);
    r_w = atomic_read (&uf->array[r].p_set);
    if ( (q_w | r_w) != r_w) {
        // update!
        fetch_or (&uf->array[r].p_set, q_w);
        while (atomic_read (&uf->array[r].parent) != 0) {
            r = uf_find (uf, r);
            fetch_or (&uf->array[r].p_set, q_w);
        }
    }

    // unlock
    uf_unlock_list (uf, a_l);
    uf_unlock_list (uf, b_l);
    uf_unlock_uf (uf, q);

    return 1;
}


/* ******************************* dead SCC ******************************** */


/**
 * (return == 1) ==> ensures DEAD (we cannot ensure a non-DEAD state)
 */
bool
uf_is_dead (const uf_t *uf, ref_t state)
{
    ref_t               f = uf_find (uf, state);
    return ( atomic_read (&uf->array[f].uf_status) == UF_DEAD );
}


/**
 * set the UF status for the representative of state to DEAD
 */
bool
uf_mark_dead (const uf_t *uf, ref_t state)
{
    bool                result = false;
    ref_t               f      = uf_find (uf, state);
    uf_status           status = atomic_read (&uf->array[f].uf_status);

    while ( status != UF_DEAD ) {
        if (status == UF_LIVE)
            result = cas (&uf->array[f].uf_status, UF_LIVE, UF_DEAD);
        status = atomic_read (&uf->array[f].uf_status);
    }

    HREassert (atomic_read (&uf->array[f].parent) == 0,
               "the parent of a DEAD representative should not change");
    HREassert (uf_is_dead (uf, state), "state should be dead");

    return result;
}


static void
uf_unlock_uf (const uf_t *uf, ref_t a)
{
    // HREassert (atomic_read (&uf->array[a].uf_status) == UF_LOCK);
    atomic_write (&uf->array[a].uf_status, UF_LIVE);
}


/* ******************************** locking ******************************** */


static bool
uf_lock_uf (const uf_t *uf, ref_t a)
{
    if (atomic_read (&uf->array[a].uf_status) == UF_LIVE) {
       if (cas (&uf->array[a].uf_status, UF_LIVE, UF_LOCK)) {

           // successfully locked
           // ensure that we actually locked the representative
           if (atomic_read (&uf->array[a].parent) == 0)
               return 1;

           // otherwise unlock and try again
           atomic_write (&uf->array[a].uf_status, UF_LIVE);
       }
    }
    return 0;
}

bool
uf_try_grab (const uf_t *uf, ref_t a)
{
    char x = atomic_read (&uf->array[a].uf_status);
    if (x == UF_LOCK) return false;
    return cas (&uf->array[a].uf_status, x, UF_LOCK);
}

static bool
uf_lock_list (const uf_t *uf, ref_t a, ref_t *a_l)
{
    char pick;

    while ( 1 ) {
        pick = uf_pick_from_list (uf, a, a_l);
        if ( pick != PICK_SUCCESS )
            return 0;
        if (cas (&uf->array[*a_l].list_status, LIST_LIVE, LIST_LOCK) )
            return 1;
    }
}


static void
uf_unlock_list (const uf_t *uf, ref_t a_l)
{
    // HREassert (atomic_read (&uf->array[a_l].list_status) == LIST_LOCK);
    atomic_write (&uf->array[a_l].list_status, LIST_LIVE);
}


/* **************************** TGBA acceptance **************************** */


uint32_t
uf_get_acc (const uf_t *uf, ref_t state)
{
    ref_t r = uf_find (uf, state);
    return  atomic_read (&uf->array[r].acc_set);
}

/**
 * unites the acceptance set of the uf representative with acc (via logical OR)
 * returns the new acceptance set for the uf representative
 */
uint32_t
uf_add_acc (const uf_t *uf, ref_t state, uint32_t acc)
{
    // just return the acceptance set if nothing is added
    if (acc == 0)
        return uf_get_acc (uf, state);

    ref_t    r;
    uint32_t r_acc;

    do {
        r = uf_find (uf, state);
        r_acc = atomic_read (&uf->array[r].acc_set);

        // only unite if it updates the acceptance set
        if ( (r_acc | acc) == r_acc) return r_acc;

        // update!
        r_acc = or_fetch (&uf->array[r].acc_set, acc);

    } while (atomic_read (&uf->array[r].parent) != 0);

    return r_acc;
}


/* ******************************** testing ******************************** */


static char*
uf_print_list_status (list_status ls)
{
    if (ls == LIST_LIVE) return "LIVE";
    if (ls == LIST_LOCK) return "LOCK";
    if (ls == LIST_TOMB) return "TOMB";
    else                 return "????";
}


static char*
uf_print_uf_status (uf_status us)
{
    if (us == UF_LIVE)   return "LIVE";
    if (us == UF_LOCK)   return "LOCK";
    if (us == UF_DEAD)   return "DEAD";
    else                 return "????";
}


static void
uf_debug_aux (const uf_t *uf, ref_t state, int depth)
{ 
    if (depth == 0) {
        Warning (info, "\x1B[45mParent structure:\x1B[0m");
        Warning (info, "\x1B[45m%5s %10s %10s %7s %7s %10s\x1B[0m",
                 "depth",
                 "state",
                 "parent",
                 "uf_s",
                 "list_s",
                 "next");
    }

    Warning(info, "\x1B[44m%5d %10zu %10zu %7s %7s %10zu\x1B[0m",
        depth,
        state,
        atomic_read (&uf->array[state].parent),
        uf_print_uf_status (atomic_read (&uf->array[state].uf_status) ),
        uf_print_list_status (atomic_read (&uf->array[state].list_status) ),
        atomic_read (&uf->array[state].list_next));

    if (uf->array[state].parent != 0) {
        uf_debug_aux (uf, uf->array[state].parent, depth+1);
    }
}


static void
uf_debug_list_aux (const uf_t *uf, ref_t start, ref_t state, int depth)
{
    if (depth == 50) {
        Warning (info, "\x1B[40mreached depth 50\x1B[0m");
        return;
    }
    if (depth == 0) {
        Warning (info, "\x1B[40mList structure:\x1B[0m");
        Warning (info, "\x1B[40m%5s %10s %7s %10s\x1B[0m",
                 "depth",
                 "state",
                 "list_s",
                 "next");
    }

    Warning (info, "\x1B[41m%5d %10zu %7s %10zu\x1B[0m",
             depth,
             state,
             uf_print_list_status (atomic_read(&uf->array[state].list_status)),
             atomic_read (&uf->array[state].list_next));

    if (atomic_read (&uf->array[state].list_next) != start ||
        atomic_read (&uf->array[state].list_next) != state ||
        atomic_read (&uf->array[state].list_next) != 0) {
        uf_debug_list_aux (uf, start, uf->array[state].list_next, depth+1);
    }
}


ref_t
uf_debug (const uf_t *uf, ref_t state)
{
    uf_debug_aux (uf, state, 0);
    return state;
}


ref_t
uf_debug_list (const uf_t *uf, ref_t state)
{
    uf_debug_list_aux (uf, state, state, 0);
    return state;
}
