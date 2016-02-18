/**
 *
 */

#include <hre/config.h>

#include <mc-lib/renault-unionfind.h>
#include <mc-lib/atomics.h>


typedef enum r_uf_status_e {
    R_UF_UNSEEN       = 0,                    // unseen (initial value)
    R_UF_INIT         = 1,                    // busy initializing
    R_UF_LIVE         = 2,                    // LIVE state
    R_UF_DEAD         = 3,                    // completed SCC
} r_uf_status;


/**
 * information per UF state
 */
struct r_uf_state_s {
    ref_t               parent;               // the parent in the UF tree
    unsigned char       r_uf_status;          // the UF status of the state
    uint32_t            acc_set;              // TGBA acceptance set
    char                pad[3];               // padding for data alignment
};


/**
 *  shared array of UF states
 */
struct r_uf_s {
    r_uf_state_t        *array;               // array[ref_t] ->r_uf_state_t
};


/**
 * initializer for the UF array
 */
r_uf_t *
r_uf_create ()
{
    r_uf_t *uf = RTmalloc     ( sizeof (r_uf_t) );
    uf->array  = RTmallocZero ( sizeof (r_uf_state_t) * (1ULL << dbs_size) );
    return uf;
}


/* ********************* 'basic' union find operations ********************* */


/**
 * returns:
 * - CLAIM_FIRST   : if we initialized the state
 * - CLAIM_SUCCESS : if the state is LIVE
 * - CLAIM_DEAD    : if the state is part of a completed SCC
 */
char     
r_uf_make_claim (const r_uf_t *uf, ref_t state)
{
    r_uf_status status = atomic_read (&uf->array[state].r_uf_status);

    // if the state is UNSEEN : initialize it
    if (status == R_UF_UNSEEN) {
        if (cas (&uf->array[state].r_uf_status, R_UF_UNSEEN, R_UF_INIT) ) {

            // create state and set it to LIVE
            atomic_write (&uf->array[state].parent, state);
            atomic_write (&uf->array[state].r_uf_status, R_UF_LIVE);
            return CLAIM_FIRST;
        }
    }

    // wait if someone currently initializing the state
    while (status == R_UF_INIT) {
        status = atomic_read (&uf->array[state].r_uf_status);
    }

    // check if the state is DEAD, otherwise return SUCCESS
    if (r_uf_is_dead (uf, state) )
        return CLAIM_DEAD;
    else 
        return CLAIM_SUCCESS;
}


/**
 * returns the representative for the UF set
 */
ref_t
r_uf_find (const r_uf_t *uf, ref_t state)
{
    // recursively find and update the parent (path compression)
    ref_t               parent = atomic_read(&uf->array[state].parent);
    ref_t               root;

    if (parent == state)
        return parent;

    root = r_uf_find (uf, parent);

    if (root != parent)
        atomic_write (&uf->array[state].parent, root);

    return root;
}


/**
 * returns whether or not a and b reside in the same UF set
 */
bool
r_uf_sameset (const r_uf_t *uf, ref_t a, ref_t b)
{
    ref_t               a_r = r_uf_find (uf, a);
    ref_t               b_r = r_uf_find (uf, b);

    // return true if the representatives are equal
    if (a_r == b_r)
        return 1;

    // return false if the parent for a has not been updated
    if (atomic_read (&uf->array[a_r].parent) == a)
        return 0;

    // otherwise retry
    else
        return r_uf_sameset (uf, a_r, b_r);
}


/**
 * unites UF sets a and b to a single set
 */
bool
r_uf_union (const r_uf_t *uf, ref_t a, ref_t b)
{
    ref_t               a_r, b_r, root, other;
    uint32_t            q_a, r_a;

    while ( 1 ) {

        a_r = r_uf_find (uf, a);
        b_r = r_uf_find (uf, b);

        // x and y are already in the same set ==> return
        if (a_r == b_r)
            return 0;

        // take the state with the highest index as the new root
        root  = a_r;
        other = b_r;
        if (a_r < b_r) {
            root  = b_r;
            other = a_r;
        }

        // try to set other.parent = root
        if ( !cas (&uf->array[other].parent, other, root) )
            continue;

        // if root.parent has changed : change other.parent back
        if (atomic_read (&uf->array[root].parent) != root) {
            atomic_write (&uf->array[other].parent, other);
            continue;
        }

        break;
    }

    // only update acceptance set for r if q adds acceptance marks
    r_a = atomic_read (&uf->array[root].acc_set);
    q_a = atomic_read (&uf->array[other].acc_set);
    if ( (q_a | r_a) != r_a) {
        // update!
        fetch_or (&uf->array[root].acc_set, q_a);
        while (atomic_read (&uf->array[root].parent) != root) {
            root = r_uf_find (uf, root);
            fetch_or (&uf->array[root].acc_set, q_a);
        }
    }

    HREassert (r_uf_sameset (uf, a, b), "union(%zu, %zu) did not unite",
               r_uf_debug(uf, a),
               r_uf_debug(uf, b));
    return 1;
}


/* ******************************* dead SCC ******************************** */


bool
r_uf_is_dead (const r_uf_t *uf, ref_t state)
{
    r_uf_status         status = atomic_read(&uf->array[state].r_uf_status);
    ref_t               f;

    if (status == R_UF_UNSEEN || status == R_UF_INIT)
        return false;

    f = r_uf_find (uf, state);
    return atomic_read (&uf->array[f].r_uf_status) == R_UF_DEAD;
}


/**
 * set the UF status for the representative of state to DEAD
 */
bool
r_uf_mark_dead (const r_uf_t *uf, ref_t state)
{
    bool                result = false;
    ref_t               f      = r_uf_find (uf, state);
    r_uf_status         status = atomic_read (&uf->array[f].r_uf_status);

    while ( status != R_UF_DEAD ) {
        if (status == R_UF_LIVE)
            result = cas (&uf->array[f].r_uf_status, R_UF_LIVE, R_UF_DEAD);
        f      = r_uf_find (uf, state);
        status = atomic_read (&uf->array[f].r_uf_status);
    }

    HREassert (atomic_read (&uf->array[f].parent) == f,
               "the parent of a DEAD representative should not change");
    HREassert (r_uf_is_dead (uf, state), "state should be dead");

    return result;
}

/* **************************** TGBA acceptance **************************** */


uint32_t
r_uf_get_acc (const r_uf_t *uf, ref_t state)
{
    ref_t r = r_uf_find (uf, state);
    return  atomic_read (&uf->array[r].acc_set);
}

/**
 * unites the acceptance set of the uf representative with acc (via logical OR)
 * returns the new acceptance set for the uf representative
 */
uint32_t
r_uf_add_acc (const r_uf_t *uf, ref_t state, uint32_t acc)
{
    // just return the acceptance set if nothing is added
    if (acc == 0)
        return r_uf_get_acc (uf, state);

    ref_t    r     = r_uf_find (uf, state);
    uint32_t r_acc = atomic_read (&uf->array[r].acc_set);

    // only unite if it updates the acceptance set
    if ( (r_acc | acc) != r_acc) {
        // update!
        r_acc = or_fetch (&uf->array[r].acc_set, acc);
        while (atomic_read (&uf->array[r].parent) != r) {
            r = r_uf_find (uf, r);
            r_acc = or_fetch (&uf->array[r].acc_set, acc);
        }
    }
    return r_acc;
}


/* ******************************** testing ******************************** */


static char*
r_uf_print_status (r_uf_status us)
{
    if (us == R_UF_UNSEEN) return "UNSN";
    if (us == R_UF_INIT)   return "INIT";
    if (us == R_UF_LIVE)   return "LIVE";
    if (us == R_UF_DEAD)   return "DEAD";
    else                   return "????";
}


static void
r_uf_debug_aux (const r_uf_t *uf, ref_t state, int depth)
{
    if (depth == 0) {
        Warning (info, "\x1B[45m%5s %10s %10s %7s\x1B[0m",
                 "depth",
                 "state",
                 "parent",
                 "status");
    }

    Warning(info, "\x1B[44m%5d %10zu %10zu %7s\x1B[0m",
            depth,
            state,
            atomic_read (&uf->array[state].parent),
            r_uf_print_status (atomic_read (&uf->array[state].r_uf_status)) );

    if (uf->array[state].parent != state) {
        r_uf_debug_aux (uf, uf->array[state].parent, depth+1);
    }
}


ref_t
r_uf_debug (const r_uf_t *uf, ref_t state)
{ 
    r_uf_debug_aux (uf, state, 0);
    return state;
}
