#include <hre/config.h>

#include <mc-lib/unionfind.h>

#include <mc-lib/atomics.h>
#include <util-lib/util.h>


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
    //T//HREassert (sizeof(uf_node_t) == sizeof(int[8]), "Improper structure packing for uf_node_t. Expected: size = %zu", sizeof(int[8]));
    ////T//HREassert (sizeof(sz_w)*8 >= W, "Too many workers for the current structure; please redefine sz_w to a larger size");
    uf_t           *uf = RTmalloc (sizeof(uf_t));
    uf->array          = RTalignZero ( sizeof(int[10]),
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
uf_is_in_list (const uf_t* uf, ref_t state)
{
    return atomic_read (&uf->array[state].list_status) != LIST_TOMBSTONE;
}


pick_e
uf_pick_from_list (const uf_t* uf, ref_t state, ref_t *ret)
{
    ref_t a, b, c;
    //T//HREassert (atomic_read (&uf->array[state].list_status) == LIST_TOMBSTONE);

    // a -> b -> c
    a = state;
    b = atomic_read (&uf->array[a].list_next);

    while (!uf_sameset (uf, a, b))
        b = atomic_read (&uf->array[a].list_next);

    ////T//HREassert (uf_sameset (uf, a, b));

    while (1) {

        if (atomic_read (&uf->array[b].list_status) == LIST_LIVE) {
            *ret = b;
            return PICK_SUCCESS;
        }

        c = atomic_read (&uf->array[b].list_next);
        while (!uf_sameset (uf, a, c))
            c = atomic_read (&uf->array[b].list_next);

        ////T//HREassert (uf_sameset (uf, a, c)); //T//HREassert (uf_sameset (uf, b, c));

        if (a == c) {
            if (uf_mark_dead (uf, a)) {
                //Warning(info, "marked %zu (and %zu) dead", a, b);
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


pick_e
uf_pick_from_list_lock (const uf_t* uf, ref_t state, ref_t *ret)
{
    //T//HREassert (atomic_read(&uf->array[state].list_status) == LIST_TOMBSTONE);

    // a -> b -> c
    ref_t a     = state;
    ref_t b     = atomic_read(&uf->array[a].list_next);
    ref_t c;

    // a -> a (single state TOMBSTONE => dead SCC)
    if (a == b) {
        if (uf_mark_dead (uf, a)) {
            //Warning(info, "marked singleton %zu dead", a);
            ////T//HREassert(0, "we should not get singleton reports");
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

            //uf_debug(uf, a);

            //Warning(info, "removing state from list: %zu -> %zu -> %zu", a, b, c);
            cas(&uf->array[a].list_next, b,  c);

            //uf_debug(uf, a);

        }
        a = b; // TODO optimize
        b = c;
    }

    *ret = b;
    return PICK_SUCCESS;
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
uf_sameset (const uf_t* uf, ref_t a, ref_t b)
{
    ref_t a_r = uf_find (uf, a);
    ref_t b_r = uf_find (uf, b);

    if (a_r == b_r)
        return 1;

    if (atomic_read (&uf->array[a_r].parent) == a)
        return 0;

    return uf_sameset (uf, a_r, b_r);
}


bool
uf_lock (const uf_t* uf, ref_t a, ref_t b)
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


bool
uf_lock_q (const uf_t* uf, ref_t a)
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
uf_unlock_q (const uf_t* uf, ref_t a)
{
    //T//HREassert (atomic_read (&uf->array[a].uf_status) == UF_BUSY,
    //        "We may only unlock locked states");
    atomic_write (&uf->array[a].uf_status, UF_LIVE);
}


void
uf_unlock (const uf_t* uf, ref_t a, ref_t b)
{
    atomic_write (&uf->array[a].uf_status, UF_LIVE);
    atomic_write (&uf->array[b].uf_status, UF_LIVE);
}


bool
uf_union (const uf_t* uf, ref_t a, ref_t b)
{
    ref_t a_r, b_r, r, q, r_n, q_n;

    while (1) {
        // 1. find the representatives
        a_r = uf_find (uf, a);
        b_r = uf_find (uf, b);
        if (a_r == b_r)
            return 0;

        // 2. decide on the new root (deterministically)
        r = a_r;
        q = b_r;
        if (a_r < b_r) { // take the highest index as root
            r = b_r;
            q = a_r;
        }

        // 3. lock the non-root
        if (!uf_lock_q (uf, q))
            continue;

        // 4. check that the non-root is a representative
        if (atomic_read (&uf->array[q].parent) != q) {
            uf_unlock_q (uf, q);
            continue;
        }

        break;
    }

    // we can now ensure that q is a representative
    //   and no other worker may change this

    // reason for check in (5):
    //
    // t   s   r   q --> sn
    //    /   /              sn != s.next => r.next will get modified
    // tn   sn  rn  qn                       goes wrong if q completes union


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

            /*// 6.1 set q_n to busy (it might point to some s_n)
            if (q != q_n) {
                if (!atomic_read (&uf->array[q_n].uf_status) == UF_LIVE)
                    continue;
                if (!cas (&uf->array[q_n].uf_status, UF_LIVE, UF_BUSY))
                    continue;
            }
            // ensures that we have set the lock*/

            // 6.2 set q.next --> r_n
            if (uf_find (uf, q_n) == q) { // not sure if this is necessary
                if (cas (&uf->array[q].list_next, q_n, r_n))
                    break;
            }

            // 6.3 if fail, reset and retry
            //if (q != q_n)
            //    atomic_write (&uf->array[q_n].uf_status, UF_LIVE);
            q_n = atomic_read (&uf->array[q].list_next);
        }

        // assert: q_n remains 'second' item of q's set and won't be removed

        // 7. set r.next --> q_n
        if (!cas (&uf->array[r].list_next, r_n, q_n)) {
            // r_n got updated after (5)
            if (!cas (&uf->array[q].list_next, r_n, q_n))
                HREassert(0, "resetting q.next --> q_n should not fail");
            //if (q != q_n)
            //    atomic_write (&uf->array[q_n].uf_status, UF_LIVE);
            continue;
        }

        // 8. check if r is still the representative (r might not be in uf[r]'s list)
        if (atomic_read (&uf->array[r].parent) != r) {
            // r got updated, so reset and retry
            if (!cas (&uf->array[r].list_next, q_n, r_n))
                HREassert(0, "resetting r.next --> r_n should not fail");
            if (!cas (&uf->array[q].list_next, r_n, q_n))
                HREassert(0, "resetting q.next --> q_n should not fail");
            //if (q != q_n)
            //    atomic_write (&uf->array[q_n].uf_status, UF_LIVE);
            continue;
        }

        break;
    }

    // 9. update worker set
    sz_w q_w = atomic_read (&uf->array[q].p_set);
    or_fetch (&uf->array[r].p_set, q_w);


    // 10. update parent
    if (!cas (&uf->array[q].parent, q, r)) {
        HREassert(0, "parent for q shouldn't change while locked");
    }

    // 11. unlock
    //if (q != q_n)
    //    atomic_write (&uf->array[q_n].uf_status, UF_LIVE);
    atomic_write (&uf->array[q].uf_status, UF_LIVE);

    //T//HREassert (uf_sameset(uf, a, b), "states should be merged after a union");
    return 1;
}


bool
uf_union_lock (const uf_t* uf, ref_t a, ref_t b)
{
    ref_t a_r, b_r;

    // do this in a while loop
    // recursion causes segfault (a lot of iterations)
    while(1) {
        a_r = uf_find (uf, a);
        b_r = uf_find (uf, b);

        //Warning(info, "union(%zu, %zu)", a, b); // DEBUG
        //Warning(info, "union(%zu, %zu)", a_r, b_r); // DEBUG


        /*Warning(info, " - printing list for a_r = %zu", a_r);
        uf_print_list(uf, a_r); // DEBUG
        Warning(info, " - printing list for b_r = %zu", b_r);
        uf_print_list(uf, b_r); // DEBUG
        uf_debug(uf, a_r,0);
        uf_debug(uf, b_r);*/

        // return if already united
        if (a_r == b_r) {
            //T//HREassert (uf_sameset(uf, a, b), "uf_union: states should be in the same set");
            return 0;
        }

        //T//HREassert (!uf_is_dead (uf, a) && !uf_is_dead (uf, b),
        //        "uf_union(%d, %d): states should not be dead",
        //        uf_debug(uf,a), uf_debug(uf,b));


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

    //T//HREassert (atomic_read (&uf->array[a_r].uf_status) == UF_LOCKED, "a_r:%zu must be locked %d", a_r, uf_debug(uf,a_r));
    //T//HREassert (atomic_read (&uf->array[b_r].uf_status) == UF_LOCKED, "b_r:%zu must be locked %d", b_r, uf_debug(uf,b_r));

    //uf_debug(uf, a);
    //uf_debug(uf, b);



    // decide new representative by rank (or pick a_r and update rank if equal)
    if (uf->array[a_r].rank < uf->array[b_r].rank) {
        //Warning(info, "switching a_r and b_r"); // DEBUG
        ref_t tmp = a_r;
        a_r = b_r;
        b_r = tmp;
    } else if (uf->array[a_r].rank == uf->array[b_r].rank) {
        //Warning(info, "increasing rank"); // DEBUG
        uf->array[a_r].rank ++;
    }

    // a_r is chosen as the new representative


    ref_t b_n = atomic_read (&uf->array[b_r].list_next);
    ref_t a_n = atomic_read (&uf->array[a_r].list_next);


    // Update b_r -> a_n
        atomic_write (&uf->array[b_r].list_next, a_n);

    // Update a_r -> b_n
        atomic_write (&uf->array[a_r].list_next, b_n);

    // 2. parent for b_r is updated
    // Do this last so states won't get marked TOMBSTONE too soon
    //T//HREassert (atomic_read (&uf->array[b_r].parent) == b_r,
    //        "Locked parent should not change %zu, %d", b_r, uf_debug(uf, b_r));
    atomic_write (&uf->array[b_r].parent, a_r);


    // 1. worker set for a_r is updated
    sz_w workers = uf->array[a_r].p_set | uf->array[b_r].p_set;
    atomic_write (&uf->array[a_r].p_set, workers);


    // 6. unlock a_r (and b_r)
    uf_unlock (uf, a_r, b_r);


    //Warning(info, "after union(%zu, %zu)", a_r, b_r); // DEBUG
    //uf_debug(uf, a_r);
    //uf_debug(uf, b_r);
    ////T//HREassert (0);

    //T//HREassert (uf_sameset(uf, a, b), "uf_union: states should be in the same set");
    return 1;
}


// dead

bool
uf_mark_dead (const uf_t* uf, ref_t state) 
{

    ref_t f          = uf_find(uf, state);
    //Warning(info,"\x1B[40mmark dead: %zu %zu %d\x1B[0m", state, f, uf_debug(uf,state));
    bool result      = false;

    // wait for possible unions to completely finish
    // (part between last parent update and unlock)
    while (atomic_read (&uf->array[f].uf_status) == UF_LOCKED);

    while (atomic_read (&uf->array[f].uf_status) != UF_DEAD)
        result = cas (&uf->array[f].uf_status, UF_LIVE, UF_DEAD);

    //T//HREassert (atomic_read (&uf->array[f].parent) == f,
    //        "representative should not change while marking dead %d", uf_debug(uf,f));

    //T//HREassert (uf_is_dead(uf, f),     "state should be dead");
    //T//HREassert (uf_is_dead(uf, state), "state should be dead");

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
uf_mark_undead (const uf_t *uf, ref_t state)
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
uf_debug_aux (const uf_t* uf, ref_t state, int depth)
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
uf_debug_list (const uf_t* uf, ref_t start, ref_t state, int depth)
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
uf_debug (const uf_t* uf, ref_t state)
{
    uf_debug_aux (uf, state, 0);
    //ref_t f = uf_find(uf, state);
    //uf_debug_list (uf, state, state, 0);

    return 1;
}


void         
uf_free (uf_t* uf)
{
    RTfree(uf->array);
    RTfree(uf);
}

