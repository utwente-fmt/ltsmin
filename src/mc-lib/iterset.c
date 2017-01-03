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
iterstate_clear(iterset_t *is)
{
    // NB: might not be thread-safe
    is->array              = RTalignZero (sizeof(char[16]),
                             sizeof (iterset_state_t) * ( (1ULL << dbs_size) + 1 ) );
    is->current            = 0; // unused
}

/* ******************************* operations ****************************** */


bool
iterstate_is_in_set (const iterset_t *is, ref_t state)
{
    return (atomic_read (&is->array[state].list_status) != LIST_TOMB);
}


is_pick_e
iterset_pick_state (const iterset_t *is, ref_t *ret)
{
    return IS_PICK_SUCCESS;
}


bool
iterset_add_state (const iterset_t *is, ref_t state)
{
    return false;
}


bool
iterset_remove_state (const iterset_t *is, ref_t state)
{
    return false;
}


bool
iterset_is_empty (const iterset_t *is)
{
    return false;
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
             iterset_print_list_status (atomic_read(&is->array[state].list_status)),
             atomic_read (&is->array[state].list_next));

    if (atomic_read (&is->array[state].list_next) != start ||
        atomic_read (&is->array[state].list_next) != state ||
        atomic_read (&is->array[state].list_next) != 0) {
        iterset_debug_list_aux (is, start, is->array[state].list_next, depth+1);
    }
}


ref_t
iterset_debug (const iterset_t *is, ref_t state)
{
    iterset_debug_list_aux (is, state, state, 0);
    return state;
}
