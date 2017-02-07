/**
 *
 */

#include <hre/config.h>

#include <mc-lib/iterset.h>
#include <mc-lib/atomics.h>
#include <util-lib/util.h>


typedef uint64_t sz_w;

typedef enum list_status_e {
    LIST_LIVE         = 0,                    // LIVE (initial value)
    LIST_LOCK         = 1,                    // busy merging lists
    LIST_TOMB         = 2,                    // fully explored state
} list_status;


/**
 * information per iterstate
 */
struct iterset_state_s {
    ref_t               list_next;            // next list item 'pointer'
    unsigned char       list_status;          // the list status
    char                pad[7];               // padding for data alignment
};


/**
 * shared array of iterstates
 */
struct iterset_s {
    ref_t               current;
    iterset_state_t    *array;                // array[ref_t] ->iterset_state_t
};


static bool      iterset_lock_list (iterset_t *is, ref_t a, ref_t *a_l);
static void      iterset_unlock_list (const iterset_t *is, ref_t a_l);

/**
 * initializer for the iterstate array
 */
iterset_t *
iterset_create ()
{
    HREassert (sizeof (iterset_state_t) == sizeof (char[16]),
               "Improper structure for iterset_state_t. Expected: size = %zu",
               sizeof (char[16]) );

    iterset_t             *is = RTmalloc    (sizeof (iterset_t));
    // allocate one entry extra since [0] is not used
    is->array              = RTalignZero (sizeof(char[16]),
                             sizeof (iterset_state_t) * ( (1ULL << dbs_size) + 1 ) );
    is->current            = 0; // unused
    return is;
}

void
iterset_clear (iterset_t *is)
{
    // NB: might not be thread-safe
    is->array              = RTalignZero (sizeof(char[16]),
                             sizeof (iterset_state_t) * ( (1ULL << dbs_size) + 1 ) );
    is->current            = 0; // unused
}

/* ******************************* operations ****************************** */


bool
iterset_is_in_set (const iterset_t *is, ref_t state)
{
    return (atomic_read (&is->array[state].list_next) != 0);
}


is_pick_e
iterset_pick_state_from (iterset_t *is, ref_t state, ref_t *ret)
{
    // invariant: every consecutive non-LOCK state is in the same set
    ref_t               a, b, c;
    list_status         a_status, b_status;

    a = state;

    while ( 1 ) {
        //HREassert ( a != 0 ); // TODO: This has triggered once, but could not replicate

        // if we exit this loop, a.status == TOMB or we returned a LIVE state
        while ( 1 ) {
            a_status = atomic_read (&is->array[a].list_status);

            // return directly if a is LIVE
            if (a_status == LIST_LIVE) {
                *ret = a;
                return IS_PICK_SUCCESS;
            }

            // otherwise wait until a is TOMB (it might be LOCK now)
            else if (a_status == LIST_TOMB)
                break;

            // wait and try again if a_status == LIST_LOCK
        }

        //HREassert ( a_status == LIST_TOMB );

        // find next state: a --> b
        b = atomic_read (&is->array[a].list_next);

        //HREassert ( b != 0 );

        // if a is TOMB and only element, then the set is empty
        if (a == b) {
            atomic_write (&is->current, 0);
            return IS_PICK_DEAD;
        }

        // if we exit this loop, b.status == TOMB or we returned a LIVE state
        while ( 1 ) {
            b_status = atomic_read (&is->array[b].list_status);

            // return directly if b is LIVE
            if (b_status == LIST_LIVE) {
                *ret = b;
                return IS_PICK_SUCCESS;
            }

            // otherwise wait until b is TOMB (it might be LOCK now)
            else if (b_status == LIST_TOMB)
                break;
        }

        //HREassert ( a_status == LIST_TOMB );
        //HREassert ( b_status == LIST_TOMB );

        // a --> b --> c
        c = atomic_read (&is->array[b].list_next);

        if (c == a) {
            atomic_write (&is->current, 0);
            return IS_PICK_DEAD;
        }

        //HREassert ( c != 0 );

        // make the list shorter (a --> c)
        atomic_write (&is->array[a].list_next, c);

        a = c; // continue searching from c
    }
}


is_pick_e
iterset_pick_state (iterset_t *is, ref_t *ret)
{
    // starting position for picking a state
    ref_t state = atomic_read (&is->current);

    if (state == 0) {
        return IS_PICK_DEAD;
    }
    
    is_pick_e result = iterset_pick_state_from(is, state, ret);

    if (result == IS_PICK_SUCCESS) {
        // update current to point to the next one (might not be optimal)
        ref_t next = atomic_read (&is->array[*ret].list_next);
        if (next != 0) {
            cas(&is->current, state, next);
        }
    }
    
    return result;
}


bool
iterset_add_state_at (iterset_t *is, ref_t state, ref_t pos)
{
    // ensure that list next pointer is never 0 for LIVE or TOMB states

    // check if the state is already in the list
    ref_t next = atomic_read (&is->array[state].list_next);
    list_status status =  atomic_read (&is->array[state].list_status);
    if (next != 0 || status == LIST_LOCK) {
        // wait until the list is not locked
        while (status == LIST_LOCK) {
            status =  atomic_read (&is->array[state].list_status);
        }
        // status might be LIVE or TOMB now
        //next = atomic_read (&is->array[state].list_next);
        //HREassert (next != 0);
        return false; // already added
    }

    //HREassert(status == LIST_LIVE);

    // Otherwise: Lock state
    if (!cas(&is->array[state].list_status, LIST_LIVE, LIST_LOCK)) {
        // CAS failed, we should start over
        return iterset_add_state_at (is, state, pos);
    }

    // the state is locked now

    // check again if the next pointer was updated before the CAS
    next = atomic_read (&is->array[state].list_next);
    if (next != 0) {
        // next pointer is already updated, we should unlock the state
        iterset_unlock_list (is, state);
        return false; // already added
    }

    // state is locked and next = 0 at this point

    //ref_t current = atomic_read (&is->current);

    // first state to be added ([state]->[state])
    if (pos == 0) { // current = 0

        ref_t current = atomic_read (&is->current);
        if (current != 0) {
            iterset_unlock_list (is, state);
            return iterset_add_state_at (is, state, current);
        }

        // status = LIST_LIVE
        atomic_write (&is->array[state].list_next, state);
        if (cas(&is->current, 0, state)) {
            iterset_unlock_list (is, state);
            return true;
        }

        // someone else has updated is->current: normal procedure
        pos = atomic_read (&is->current);
        //HREassert (pos != 0);
    }

    // Try to lock pos (we want [pos_l]->[state]->[pos_l->next])
    ref_t pos_l;
    HREassert(iterset_lock_list (is, pos, &pos_l));

    // update the next pointer of state
    ref_t cur_l_next = atomic_read (&is->array[pos_l].list_next);
    atomic_write (&is->array[state].list_next, cur_l_next);

    // update the next pointer of pos_l
    atomic_write (&is->array[pos_l].list_next, state);

    // unlock pos_l and state
    iterset_unlock_list (is, pos_l);
    iterset_unlock_list (is, state);

    //atomic_write (&is->current, current_l); // update current, because why not?

    return true;
}


bool
iterset_add_state (iterset_t *is, ref_t state)
{
    ref_t current = atomic_read (&is->current);
    return iterset_add_state_at (is, state, current);
}


bool
iterset_remove_state (iterset_t *is, ref_t state)
{
    list_status         list_s;

    // only remove list item if it is LIVE , otherwise (LIST_LOCK) wait
    while ( true ) {
        list_s = atomic_read (&is->array[state].list_status);
        if (list_s == LIST_LIVE) {
            if (cas (&is->array[state].list_status, LIST_LIVE, LIST_TOMB) )
                return 1;
        } else if (list_s == LIST_TOMB)
            return 0;
    }
}


bool
iterset_is_empty (iterset_t *is)
{
    if (atomic_read (&is->current) == 0) return true;
    ref_t dummy;
    return (iterset_pick_state_from (is, is->current, &dummy) != IS_PICK_SUCCESS);
}


/* ******************************** locking ******************************** */

static bool
iterset_lock_list (iterset_t *is, ref_t a, ref_t *a_l)
{
    char pick;

    while ( 1 ) {
        pick = iterset_pick_state_from (is, a, a_l);
        if ( pick != IS_PICK_SUCCESS )
            return 0;
        if (cas (&is->array[*a_l].list_status, LIST_LIVE, LIST_LOCK) )
            return 1;
    }
}


static void
iterset_unlock_list (const iterset_t *is, ref_t a_l)
{
    atomic_write (&is->array[a_l].list_status, LIST_LIVE);
}



/* ******************************** testing ******************************** */


static char*
iterset_print_list_status (list_status ls)
{
    if (ls == LIST_LIVE) return "LIVE";
    if (ls == LIST_LOCK) return "LOCK";
    if (ls == LIST_TOMB) return "TOMB";
    else                 return "????";
}


static void
iterset_debug_list_aux (const iterset_t *is, ref_t start, ref_t state, int depth)
{
    if (depth == 25) {
        Warning (info, "\x1B[40mreached depth 25\x1B[0m");
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
             iterset_print_list_status (atomic_read(&is->array[state].list_status)),
             atomic_read (&is->array[state].list_next));

    if (atomic_read (&is->array[state].list_next) != start &&
        atomic_read (&is->array[state].list_next) != state) {
        iterset_debug_list_aux (is, start, is->array[state].list_next, depth+1);
    }
}


ref_t
iterset_debug (const iterset_t *is)
{
    Warning (info, "Debugging");
    ref_t current = atomic_read (&is->current);
    iterset_debug_list_aux (is, current, current, 0);
    return current;
}


int
iterset_size (const iterset_t *is)
{
    ref_t current = atomic_read (&is->current);
    if (current == 0) {
        return 0;
    }
    else {
        int counter = 1;
        ref_t state = current;
        ref_t next = 0;
        while (next != current) {
            next = atomic_read (&is->array[state].list_next);
            state = next;
            counter ++;
        }
        return counter;
    }
}