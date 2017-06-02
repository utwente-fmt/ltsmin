#include <hre/config.h>

#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <dm/dm.h>
#include <hre/stringindex.h>
#include <hre/user.h>
#include <lts-io/user.h>
#include <pins-lib/pins.h>
#include <pins-lib/pins-impl.h>
#include <pins-lib/pins-util.h>
#include <pins2lts-sym/alg/aux.h>
#include <pins2lts-sym/alg/scc.h>
#include <pins2lts-sym/aux/options.h>
#include <ltsmin-lib/ltsmin-standard.h>
#include <ltsmin-lib/ltsmin-syntax.h>
#include <ltsmin-lib/ltsmin-tl.h>
#include <mc-lib/atomics.h>
#include <mc-lib/bitvector-ll.h>
#include <util-lib/bitset.h>
#include <util-lib/dfs-stack.h>
#include <util-lib/util.h>

#ifdef HAVE_SYLVAN
#include <sylvan.h>

vset_t
get_svar_eq_int_set (int state_idx, int state_match, vset_t visited)
{
  vset_t result = vset_create(domain, -1, NULL);
  int proj[1] = {state_idx};
  int match[1] = {state_match};
  vset_copy_match(result, visited, 1, proj, match);

  return result;
}

typedef struct scc_data_s {
    int             *v;
    dfs_stack_t      T;
} scc_data_t;

pthread_key_t           tls; // TODO: hre_key_t when sym tool uses HRE properly

static inline scc_data_t *
getLocal ()
{
    scc_data_t         *data = pthread_getspecific (tls);
    if (data == NULL) {
        data = RTmalloc (sizeof(scc_data_t));
        data->v = RTmalloc (sizeof(int[N]));
        data->T = NULL;
        if (sccs > 2)
            data->T = dfs_stack_create (INT_SIZE(sizeof(vset_t)));
        pthread_setspecific (tls, data);
    }
    return data;
}

static void
destroyLocal (void *tls)
{
    scc_data_t         *data = (scc_data_t *) tls;
    RTfree (data->v);
    if (data->T != NULL)
        RTfree (data->T);
    RTfree (data);
}

static int report_shift = 2;

static inline bool
report_scc_progress (size_t scc_count)
{
    int shift = atomic_read (&report_shift);
    if ((scc_count >> shift) != 0) {
        if (cas (&report_shift, shift, shift + 1)) {
            Warning(info, "SCC iteration: %zu", (size_t)1ULL << shift);
        }
        return true;
    }
    return false;
}

static inline vset_t
empty ()
{
    return vset_create (domain, -1, NULL);
}

static inline vset_t
copy (vset_t B)
{
    vset_t S = empty ();
    vset_copy (S, B);
    return S;
}

static inline vset_t
singleton (int *v)
{
    vset_t S = empty ();
    vset_add (S, v);
    return S;
}

static inline bool
trim (vset_t P, size_t *scc_count)
{
    if (!trimming || vset_is_empty(P)) return false;

    //vset_t Pruned = empty ();
    bool trimmed = false;
    vset_t Ptemp = empty ();
    while (true) {
        add_step (false, Ptemp, P, P);
        add_step (true, Ptemp, P, P);
        if (vset_equal(P, Ptemp)) break;
        trimmed = true;
        vset_minus (P, Ptemp);
        double c;
        vset_count (P, NULL, &c);
        *scc_count += c;
        while (report_scc_progress(*scc_count)) {}
        //vset_union (Pruned, P);
        vset_copy (P, Ptemp);
        vset_clear (Ptemp);
    };
    vset_destroy (Ptemp);
    return trimmed;
    //vset_destroy (Pruned);
}

void
reach_in (bool backward, vset_t B, vset_t universe)
{
    vset_t          front   = copy  (B);
    vset_t          temp    = empty ();
    while (!vset_is_empty(front)) {
        add_step (backward, temp, front,  universe);
        vset_minus (temp, B);
        vset_union (B, temp);
        vset_copy (front, temp);
        vset_clear (temp);
    }
    vset_destroy (temp);
    vset_destroy (front);
}

#define scc_fb(P) CALL(scc_fb, (P))
TASK_1(size_t, scc_fb, vset_t, P)
{
    size_t          scc_count = 0;
    trim (P, &scc_count);

    if (vset_is_empty(P)) { vset_destroy (P); return 0; }

    scc_data_t         *data = getLocal ();
    vset_example (P, data->v);                  // v in P
    vset_t          F = singleton (data->v);    // B := {v}
    vset_t          B = singleton (data->v);    // F := {v}

    //vset_least_fixpoint (F, F, group_next, nGrps);
    reach_in (false, F, P);                     // F := Succ(F)
    reach_in (true,  B, P);                     // B := Pred(B)

    vset_t          SCC = copy (B);
    vset_intersect (SCC, F);                    // SCC := F n B
    scc_count++;

    vset_minus (P, F);
    vset_minus (P, B);                          // P := P \ B \ F
    vset_minus (F, SCC);                        // F := F \ SCC
    vset_minus (B, SCC);                        // B := B \ SCC
    vset_destroy (SCC);

    SPAWN (scc_fb, B);                          // FB (B)
    SPAWN (scc_fb, F);                          // FB (F)
    scc_count += scc_fb (P);                    // FB (P)
    scc_count += SYNC (scc_fb);
    scc_count += SYNC (scc_fb);

    report_scc_progress (scc_count);

    return scc_count;
}

static inline vset_t
converge (bool backward, vset_t N, vset_t Nfront, vset_t C, vset_t P)
{
    // Converge N
    vset_t          Ntemp   = copy (Nfront);    // N' := Nf
    vset_intersect (Ntemp, C);                  // N' := N' n C
    while (!vset_is_empty (Ntemp)) {            // N' n C != {}
        vset_clear (Ntemp);                     // N' := {}
        add_step (backward, Ntemp, Nfront, P);  // N' := XXXX(Nf) n P
        vset_minus (Ntemp, N);                  // N' := N' \ N
        vset_union (N, Ntemp);                  // N  := N U N'
        vset_copy  (Nfront, Ntemp);             // Nf := N'
        vset_intersect (Ntemp, C);              // N' := N' n C
    }
    vset_destroy (Ntemp);
    vset_destroy (Nfront);

    // The SCC containing v
    vset_t          SCC = copy (C);
    vset_intersect (SCC, N);                    // SCC := C n N
    vset_destroy (N);

    // Recursive calls
    vset_minus (P, C);                          // P := P \ C
    vset_minus (C, SCC);                        // C := C \ SCC
    vset_destroy (SCC);

    return C;
}

#define scc_lock_step(P) CALL(scc_lock_step, (P))
TASK_1 (size_t, scc_lock_step, vset_t, P)
{
    size_t          scc_count = 0;
    trim (P, &scc_count);

    scc_data_t         *data = getLocal ();
    vset_random (P, data->v);                  // v in P
    vset_t          F = singleton (data->v);    // F := {v}
    vset_t          B = singleton (data->v);    // B := {v}
    vset_t          Ffront  = copy  (F);        // Ff := F
    vset_t          Bfront  = copy  (B);        // Bf := B

    vset_t          Btemp   = empty ();
    vset_t          Ftemp   = empty ();
    while (!vset_is_empty(Bfront) && !vset_is_empty(Ffront)) {
        add_step (true, Btemp, Bfront, P);      // Bf := Pred(Bf) n P
        vset_minus (Btemp, B);                  // Bf := Bf \ B
        vset_union (B, Btemp);                  // B := B U Bf
        vset_copy  (Bfront, Btemp);
        vset_clear (Btemp);

        add_step (false, Ftemp, Ffront, P);     // Ff := Succ(Ff) n P
        vset_minus (Ftemp, F);                  // Ff := Ff \ F
        vset_union (F, Ftemp);                  // F := F U Ff
        vset_copy  (Ffront, Ftemp);
        vset_clear (Ftemp);
    }
    vset_destroy (Btemp);
    vset_destroy (Ftemp);

    // Complete of non-converged set (N) within converged set (C)
    vset_t          C;
    if (vset_is_empty(Bfront)) {
        vset_destroy (Bfront);
        C = converge (false, F, Ffront, B, P);
    } else {
        vset_destroy (Ffront);
        C = converge (true,  B, Bfront, F, P);
    }

//    double c, cc;
//    vset_count (P, NULL, &c);
//    vset_count (C, NULL, &cc);
//    Warning (info, "%d -- %d", (int)c, (int)cc);

    size_t spawns = 0;
    if (!vset_is_empty(C)) {
        SPAWN (scc_lock_step, C);                                // LockStep (C)
        spawns++;
    }
    if (!vset_is_empty(P)) {
        SPAWN (scc_lock_step, P);                                // LockStep (P)
        scc_count += SYNC (scc_lock_step);
    }
    report_scc_progress (scc_count + 1);
    if (spawns) {
        scc_count += SYNC (scc_lock_step);
        spawns--;
    }
    report_scc_progress (scc_count + 1);
    return scc_count + 1;
}

//  Construct Forward Skeleton <F, Sn, Nn>
void
skeleton (bool backward, vset_t V, vset_t N, vset_t F, vset_t Sn, vset_t Nn, vset_t E)
{
    scc_data_t     *data = getLocal ();
    HREassert (!vset_is_empty(N));
    //HREassert (dfs_stack_size(data->T) == 0); // does not hold in paralllel!
    size_t          start = dfs_stack_size (data->T);

    vset_t          L = copy  (N);              // L  := N
    vset_t          Ltemp = empty ();           // Lf := {}
    while (!vset_is_empty(L)) {
        dfs_stack_push (data->T, (int *)&L);    // Push(T, L)
        vset_union (F, L);                      // F  := F U L
        add_step (backward, Ltemp, L, V);       // Lf := Succ(L) n V
        L = copy  (Ltemp);                      // L  := Lf
        vset_minus (L, F);                      // L  := L \ F
        vset_clear (Ltemp);                     // Lf := {}
    }
    vset_destroy (L);

    L = *(vset_t *) dfs_stack_pop (data->T);    // L  := Pop(T)
    vset_copy (E, L);
    vset_example (L, data->v);                  // v  in L
    vset_add (Sn, data->v);                     // Sn := {v}
    vset_add (Nn, data->v);                     // Nn := {v}
    HREassert (vset_is_empty(Sn) || !vset_is_empty(Nn), "Unexpected lack of skeleton predecessor");
    vset_destroy (L);
    while (dfs_stack_size(data->T) != start) {
        L = *(vset_t *) dfs_stack_pop (data->T);// L  := Pop(T)
        add_step (!backward, Ltemp, Sn, L);     // Lf := Pred(Sn) n L
        HREassert (!vset_is_empty(Ltemp), "Unexpected lack of skeleton predecessor");
        vset_example (Ltemp, data->v);          // v in Lf
        vset_add (Sn, data->v);                 // Sn := Sn U {v}
        vset_clear (Ltemp);                     // Lf := {}
        vset_destroy (L);
    }
    vset_destroy (Ltemp);

    HREassert (!vset_is_empty(Nn));
}

#define sscc(V, S, N) CALL(sscc, (V), (S), (N))
TASK_3(size_t, sscc, vset_t, V, vset_t, S, vset_t, N)
{
    // trim (V); // TODO

    if (vset_is_empty(V)) {                     // V = {}
        vset_destroy (V);
        vset_destroy (S);
        vset_destroy (N);
        return 0;
    }

    if (vset_is_empty(S)) {                     // S = {}
        scc_data_t         *data = getLocal ();
        vset_example (V, data->v);              // v  in V
        vset_add (N, data->v);                  // N  := {v}
    }

    vset_t          F = empty ();                // F  := {}
    vset_t          Sn = empty ();               // Sn := {}
    vset_t          Nn = empty ();               // Nn := {}
    vset_t          E = empty ();                // Nn := {}
    skeleton (false, V, N, F, Sn, Nn, E);
    vset_destroy (E);

    // Construct SCC by going backward from N in F
    vset_t          SCC = copy  (N);            // SCC := N
    reach_in (true, SCC, F);                    // mu SCC. N U (Pred(SCC) n F)

    // The SCC containing v
    size_t          scc_count = 1;

    // Recursive calls
    vset_minus (V, F);                          // V := V \ F
    vset_t           SCCp = copy (SCC);
    vset_intersect (SCCp, S);                   // SCC' := SCC n S
    vset_minus (S, SCC);                        // S' := S \ SCC
    vset_clear (N);
    add_step (true, N, SCCp, S);                // N := Pred(SCC n S) n S'
    HREassert (vset_is_empty(S) || !vset_is_empty(N), "Unexpected lack of skeleton predecessor");
    vset_destroy (SCCp);

    vset_minus (F, SCC);
    vset_minus (Sn, SCC);
    vset_destroy (SCC);

    SPAWN (sscc, V, S, N);                                   // SSCC (C)
    scc_count += sscc (F, Sn, Nn);                           // SSCC (P)
    scc_count += SYNC (sscc);                                // SSCC (C)
    report_scc_progress (scc_count);
    return scc_count;
}

void
test_forward_back (vset_t  initial, vset_t  P)
{
    // do forward reach from initial
    vset_t          F = empty ();
    vset_t          Front = copy  (initial);
    vset_t          Temp = empty ();
    vset_t          Last = empty ();
    while (!vset_is_empty(Front)) {
        vset_union (F, Front);
        vset_copy  (Last, Front);
        add_step (false, Temp, Front, P);
        vset_copy  (Front, Temp);
        vset_minus (Front, F);
        vset_clear (Temp);
    }

    // test forward reach from initial result
    vset_t          P2 = copy  (P);
    vset_minus (P2, F);
    double count;
    vset_count (P2, NULL, &count);
    HREassert (count == 0, "Divergent forward reachability in initial skeleton construction for SCC detection (off by %f)", count);
    HREassert (vset_equal(P, F), "Divergent forward reachability");
    vset_destroy (P2);

    // do backward reach from last level of forward reach
    vset_t          B = empty ();
    vset_copy  (Front, Last);
    HREassert (!vset_is_empty(Front), "empty last front from forward");
    while (!vset_is_empty(Front)) {
        vset_union (B, Front);
        add_step (true, Temp, Front, P);
        vset_copy  (Front, Temp);
        vset_minus (Front, B);
        vset_clear (Temp);
    }
    vset_destroy (Temp);
    vset_destroy (Front);
    vset_destroy (Last);

    // test backward reachablity
    vset_minus (F, B);
    vset_count (F, NULL, &count);
    HREassert (count == 0, "Divergent backward reachability in initial skeleton construction for SCC detection (off by %f)", count);
    HREassert (vset_equal(P, B), "Divergent forward reachability");
    vset_destroy (F);
    vset_destroy (B);
    exit(0);
}

void
detect_sccs (vset_t P)
{
    pthread_key_create (&tls, destroyLocal);

    Warning(info, "Initializing SCC detection");
    vset_t          initial = copy(P);
    rt_timer_t t = RTcreateTimer();
    RTstartTimer(t);

    //test_forward_back (initial, P);

    LACE_ME;
    size_t          scc_count;
    vset_t          V, N, S;
    switch (sccs) {
    case 1:
        V = copy (P);
        scc_count = scc_fb (V);
        break;
    case 2:
        V = copy (P);
        scc_count = scc_lock_step (V);
        break;
    case 3:
        V = copy  (P);
        S = empty ();
        N = empty ();
        scc_count = sscc (V, S, N);
        break;
    case 4:
        V = empty ();
        S = empty ();
        N = empty ();
        vset_t          E = empty ();                // N := {}
        skeleton (false, P, initial, V, S, N, E);
        RTstopTimer(t);
        HREassert (vset_equal(V, P), "Divergent reachability in initial skeleton construction for SCC detection");
        vset_destroy (E);
        Warning (info, " ");
        RTprintTimer(infoShort, t, "SCC initialization took");
        Warning (info, " ");
        RTresetTimer (t);
        RTstartTimer(t);
        scc_count = sscc (V, S, N);
        break;
    default: Abort ("Unimplemented SCC detection function %d", sccs);
    }
    RTstopTimer(t);

    Warning (info, " ");
    Warning (info, "SCCs count: %zu", scc_count);
    RTprintTimer(infoShort, t, "SCC detection took");
    Warning (info, " ");
}

#else // HAVE_SYLVAN

void
detect_sccs (vset_t P)
{
       (void) P;
}

#endif // HAVE_SYLVAN
