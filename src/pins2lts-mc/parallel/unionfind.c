#include <hre/config.h>

#include <pins2lts-mc/parallel/unionfind.h>

static char         UF_UNSEEN     = 0; // initial
static char         UF_INIT       = 1;
static char         UF_LIVE       = 2;
static char         UF_LOCKED     = 3;
static char         UF_DEAD       = 4;

static char         LIST_LIVE     = 0; // initial
static char         LIST_BUSY     = 1;
static char         LIST_REMOVED  = 2;

uf_t *
uf_create ()
{
    //HREassert (sizeof(sz_w)*8 >= W, "Too many workers for the current structure; please redefine sz_w to a larger size");
    uf_t           *uf = RTmalloc (sizeof(uf_t));
    uf->array          = RTmallocZero ( sizeof(uf_node_t) * (1ULL << dbs_size) );
    return uf;
}

// successor handling

char    
uf_pick_from_list (const uf_t* uf, ref_t state, ref_t *node) 
{

    // check if the state is dead (= no successors)
    if (uf_is_dead(uf, state)) {
        return PICK_DEAD;
    }

    ref_t s = state;
    ref_t next_1 = uf->array[s].list_next;
    while (uf->array[next_1].list_status == LIST_REMOVED) {

        ref_t next_2 = uf->array[next_1].list_next;

        // removed last from list ==> dead
        if (next_1 == next_2) {
            if (uf_mark_dead(uf, s))
                return PICK_MARK_DEAD;
            return PICK_DEAD;
        }

        // 'remove' next state

        // Make sure that another worker does not combine successors
        bool success = false;
        while (!success) {
            // retry until state is LIST_REMOVED or until we marked it LIST_BUSY
            success = cas(&uf->array[s].list_status, LIST_LIVE, LIST_BUSY);
            if (uf->array[s].list_status == LIST_REMOVED) 
                break;
        }
        // update the next pointer
        cas(&uf->array[s].list_next, next_1, next_2); 
        if (success) {
            // LIST_BUSY -> LIST_LIVE
            uf->array[s].list_status = LIST_LIVE;
        }

        s = next_1;
        next_1 = uf->array[s].list_next; // == next_2

    }

    *node = next_1; // set the node reference
    return PICK_SUCCESS;
}

void    
uf_remove_from_list (const uf_t* uf, ref_t state)
{
    // only remove if it has LIST_LIVE , otherwise (LIST_BUSY) wait
    while (uf->array[state].list_status != LIST_REMOVED) {
        cas(&uf->array[state].list_status, LIST_LIVE, LIST_REMOVED);
    }
}

bool    
uf_is_in_list (const uf_t* uf, ref_t state)
{
    return uf->array[state].list_status != LIST_REMOVED;
}

char     
uf_make_claim (const uf_t* uf, ref_t state, sz_w w_id)
{
    // Is the state unseen? ==> Initialize it
    if (cas(&uf->array[state].uf_status, UF_UNSEEN, UF_INIT)) {
        // create state and add worker

        uf->array[state].parent     = state;
        uf->array[state].list_next  = state;
        uf->array[state].w_set      = w_id;
        uf->array[state].uf_status  = UF_LIVE;

        return CLAIM_FIRST;
    }

    // Is someone currently initializing the state?
    while (uf->array[state].uf_status == UF_INIT);

    ref_t f = uf_find(uf, state); 
    
    // Is the state dead?
    if (uf->array[f].uf_status == UF_DEAD) {
        return CLAIM_DEAD;
    }

    // Did I already explore `this' state?
    if (((uf->array[f].w_set) & w_id ) != 0) {
        return CLAIM_FOUND;
        // NB: cycle is possibly missed (in case f got updated)
        // - but next iterations should fix this
    }

    // Add our worker ID to the set (and make sure it is the UF representative)
    do {
        f = uf->array[f].parent;
        //while (uf->array[f].uf_status == UF_LOCKED); // not sure if this helps
        __sync_or_and_fetch(&uf->array[f].w_set, w_id);
    } while (uf->array[f].parent != f 
          || uf->array[f].uf_status == UF_LOCKED);

    return CLAIM_SUCCESS;
}

void     
uf_merge_list(const uf_t* uf, ref_t list_x, ref_t list_y)
{
    // assert \exists x' \in List(x) (also for y) 
    // - because states are locked and union(x,y) did not take place yet

    //HREassert(!uf_sameset(uf, list_x, list_y))
    ref_t x = list_x;
    ref_t y = list_y;

    while (!cas(&uf->array[x].list_status, LIST_LIVE, LIST_BUSY))
        uf_pick_from_list(uf, x, &x);

    while (!cas(&uf->array[y].list_status, LIST_LIVE, LIST_BUSY)) 
        uf_pick_from_list(uf, y, &y);

    // SWAP (x.next, y.next)
    ref_t tmp = uf->array[x].list_next;
    uf->array[x].list_next = uf->array[y].list_next;
    uf->array[y].list_next = tmp;

    uf->array[x].list_status = LIST_LIVE;
    uf->array[y].list_status = LIST_LIVE;
}

// 'basic' union find

ref_t
uf_find (const uf_t* uf, ref_t state)
{
    // recursively find and update the parent (path compression)
    if (uf->array[state].parent != state)
        uf->array[state].parent = uf_find (uf, uf->array[state].parent);

    return uf->array[state].parent;
}

bool
uf_sameset (const uf_t* uf, ref_t state_x, ref_t state_y)
{
    ref_t x_f = state_x;
    ref_t y_f = state_y;
    // while loop is necessary, in case uf root gets updated
    // otherwise sameset might return false if it is actually true
    while ((uf->array[x_f].uf_status == UF_LOCKED) ||
           (uf->array[y_f].uf_status == UF_LOCKED) ||
           (uf->array[x_f].parent != x_f) ||
           (uf->array[y_f].parent != y_f)) {
        x_f = uf->array[x_f].parent;
        y_f = uf->array[y_f].parent;
    }
    // TODO: possibly change

    return x_f == y_f;
}

void
uf_union_aux (const uf_t* uf, ref_t root, ref_t other)
{
    // don't need CAS because the states are locked
    uf->array[root].w_set   |= uf->array[other].w_set;
    uf->array[other].parent  = root;
}

void
uf_union (const uf_t* uf, ref_t state_x, ref_t state_y)
{
    
    if (uf_lock(uf, state_x, state_y)) {
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
    //HREassert (uf_sameset(uf, state_x, state_y), "uf_union: states should be in the same set");
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

        char tmp = uf->array[f].uf_status;
        if (tmp != UF_DEAD) {
            result = cas(&uf->array[f].uf_status, tmp, UF_DEAD);
        }
    }


    //HREassert (uf_is_dead(uf, state), "state should be dead");

    return result;
}

bool
uf_is_dead (const uf_t* uf, ref_t state)
{
    ref_t f = uf_find(uf, state); 
    return uf->array[f].uf_status == UF_DEAD;
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
            if (uf->array[a].parent == a) {
                // a is successfully locked
                if (cas(&uf->array[b].uf_status, UF_LIVE, UF_LOCKED)) {
                    if (uf->array[b].parent == b) {
                        // b is successfully locked
                        return true;
                    } 
                    uf->array[b].uf_status = UF_LIVE;
                } 
            } 
            uf->array[a].uf_status = UF_LIVE;
        }
    }
}

void      
uf_unlock (const uf_t* uf, ref_t state)
{
    HREassert(uf->array[state].uf_status == UF_LOCKED)
    uf->array[state].uf_status = UF_LIVE;
}

// testing

bool
uf_mark_undead (const uf_t* uf, ref_t state) 
{
    // only used for testing

    bool result = 0;

    ref_t f = uf_find(uf, state); 

    result = cas(&uf->array[f].uf_status, UF_DEAD, UF_LIVE);

    return result;
}

char *
uf_get_str_w_set(sz_w w_set) 
{
    char *s = RTmalloc ((W+1)*sizeof(char));
    s[W] = '\0';
    sz_w i=0;
    while (i<W) {
        s[W-1-i] = ((w_set & (1ULL << i)) != 0) ? '1' : '0';
        i++;
    }
    return s;
}

void         
uf_debug (const uf_t* uf, ref_t state) 
{ 
    Warning(info, "state:  %zu\t, parent: %zu\t, rank:   %d\t, status: %d\t,  w_set:  %s",
        state, 
        uf->array[state].parent,  
        uf->array[state].rank,  
        uf->array[state].uf_status,  
        uf_get_str_w_set(uf->array[state].w_set));
    if (uf->array[state].parent != state) {
        uf_debug (uf, uf->array[state].parent); 
    }
}

void         
uf_free (uf_t* uf)
{
    RTfree(uf->array);
    RTfree(uf);
}