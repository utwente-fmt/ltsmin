
#include <hre/config.h>

#include <mc-lib/renault-unionfind.h>

#include <mc-lib/atomics.h>

typedef enum r_uf_status_e {
    r_UF_UNSEEN     = 0, // initial
    r_UF_INIT       = 1,
    r_UF_LIVE       = 2,
    r_UF_DEAD       = 3
} r_uf_status;

struct r_uf_node_s {
    ref_t           parent;                 // The parent in the UF tree
    unsigned char   rank;                   // The height of the UF tree
    unsigned char   r_uf_status;            // {UNSEEN, INIT, LIVE, LOCKED, DEAD}
};

typedef struct r_uf_node_s r_uf_node_t;

struct r_uf_s {
    r_uf_node_t      *array;                // array: [ref_t] -> uf_node
};

r_uf_t *
r_uf_create ()
{
    //HREassert (sizeof(sz_w)*8 >= W, "Too many workers for the current structure; please redefine sz_w to a larger size");
    r_uf_t         *uf = RTmalloc (sizeof(r_uf_t));
    uf->array          = RTmallocZero ( sizeof(r_uf_node_t) * (1ULL << dbs_size) );
    return uf;
}

// successor handling

char     
r_uf_make_claim (const r_uf_t* uf, ref_t state)
{
    // if the state is UNSEEN : initialize it
    if (cas(&uf->array[state].r_uf_status, r_UF_UNSEEN, r_UF_INIT)) {

        // create state and set it to LIVE
        atomic_write (&uf->array[state].parent, state);
        atomic_write (&uf->array[state].r_uf_status, r_UF_LIVE);
        return CLAIM_FIRST;
    }

    // wait if someone currently initializing the state
    while (atomic_read (&uf->array[state].r_uf_status) == r_UF_INIT);

    // check if the state is DEAD, otherwise return SUCCESS
    if (r_uf_is_dead(uf, state))
        return CLAIM_DEAD;
    else 
        return CLAIM_SUCCESS;
}

ref_t
r_uf_find (const r_uf_t* uf, ref_t state)
{
    // recursively find and update the parent (path compression)
    ref_t parent = atomic_read(&uf->array[state].parent);

    if (parent == state)
        return parent;

    ref_t root = r_uf_find (uf, parent);

    if (root != parent)
        atomic_write (&uf->array[state].parent, root);

    return root;
}

bool
r_uf_sameset (const r_uf_t* uf, ref_t a, ref_t b)
{
    ref_t a_r = r_uf_find (uf, a);
    ref_t b_r = r_uf_find (uf, b);

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

void
r_uf_union (const r_uf_t* uf, ref_t state_x, ref_t state_y)
{
    ref_t x_f, y_f, root, other;

    while (1) {
        x_f = r_uf_find(uf, state_x);
        y_f = r_uf_find(uf, state_y);

        // x and y are in the same set
        if (x_f == y_f)
            return;

        root  = x_f;
        other = y_f;

        // swap(root,other) if root.rank < other.rank
        if (uf->array[x_f].rank < uf->array[y_f].rank) { 
            root  = y_f;
            other = x_f;
        } 

        // if ranks are equal:
        else if (uf->array[x_f].rank == uf->array[y_f].rank && y_f < x_f) {
            root  = y_f;
            other = x_f;
        }

        // try to set other.parent = root
        if (!cas(&uf->array[other].parent, other, root))
            continue;

        // check if root.parent has changed, otherwise change back and try again
        if (uf->array[root].parent != root)
            atomic_write (&uf->array[other].parent, other);
            continue;

        // successful merge: increment the rank if it is equal
        if (uf->array[root].rank == uf->array[other].rank) {
            uf->array[root].rank ++;
        }
    }

    HREassert (r_uf_sameset(uf, state_x, state_y), "states should be merged after a union");
}


// dead

bool
r_uf_mark_dead (const r_uf_t* uf, ref_t state) 
{
    // returns if it has marked the state dead
    bool result = false;

    ref_t f = r_uf_find(uf, state); 

    while (!r_uf_is_dead(uf, f)) {
        f = r_uf_find(uf, f); 

        char tmp = atomic_read (&uf->array[f].r_uf_status);
        if (tmp != r_UF_DEAD) {
            result = cas(&uf->array[f].r_uf_status, tmp, r_UF_DEAD);
        }
    }


    HREassert (r_uf_is_dead(uf, state), "state should be dead");

    return result;
}

bool
r_uf_is_dead (const r_uf_t* uf, ref_t state)
{
    if (atomic_read(&uf->array[state].r_uf_status) == r_UF_UNSEEN ||
        atomic_read(&uf->array[state].r_uf_status) == r_UF_INIT) 
        return false;
    ref_t f = r_uf_find(uf, state); 
    return atomic_read(&uf->array[f].r_uf_status) == r_UF_DEAD;
}

// testing

bool
r_uf_mark_undead (const r_uf_t* uf, ref_t state) 
{
    // only used for testing

    bool result = 0;

    ref_t f = r_uf_find(uf, state); 

    result = cas(&uf->array[f].r_uf_status, r_UF_DEAD, r_UF_LIVE);

    return result;
}

void         
r_uf_debug (const r_uf_t* uf, ref_t state) 
{ 
    Warning(info, "state:  %zu\t, parent: %zu\t, rank:   %d\t, status: %d\t",
        state, 
        uf->array[state].parent,  
        uf->array[state].rank,  
        uf->array[state].r_uf_status);
    if (uf->array[state].parent != state) {
        r_uf_debug (uf, uf->array[state].parent); 
    }
}

void         
r_uf_free (r_uf_t* uf)
{
    RTfree(uf->array);
    RTfree(uf);
}
