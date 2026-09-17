/*
 * Standalone unit test for vset_lddmc_reachdd.c (ReachDDMdd, Alg. 6).
 *
 * It #includes the .c directly, with RLM_NO_HRE and its own shims for
 * Abort/RTmalloc/RTfree, so that it can reach the static split and interning
 * functions; that is why it links against Sylvan alone and not against
 * libvset.  Built and run by `make check` when Sylvan is present; to build it
 * by hand instead, e.g.
 *
 *   clang -g -O1 -std=gnu11 -Wall -Wextra \
 *       -I <ltsmin>/src -I <ltsmin>/src/vset-lib -I <sylvan-prefix>/include \
 *       -o rlm_test src/vset-lib/vset_lddmc_reachdd_test.c \
 *       -L <sylvan-prefix>/lib -Wl,-rpath,<sylvan-prefix>/lib \
 *       -lsylvan -llace -lpthread
 *   ./rlm_test
 *
 * -DNLEV / -DNVAL / -DNACT resize the random domain; -DRLM_CROSS_WHOLE=1 makes
 * the off-diagonal phase use lddmc_relprod's own block enumeration instead of
 * the hand-written one, which is the sharpest available check on the split.
 *
 * What it checks.  (1) One named case per meta class against a hand-computed
 * answer.  (2) The residual-list invariants: canonicity (two logically equal
 * lists are the identical handle), the cell layout, and that duplicate pids in
 * one chain are legal.  (3) The copy-node convention -- that a copy at an
 * only-read level aborts, and that one at a written level is the identity.
 * (4) Randomised: small LDD relations with random per-level meta classes
 * (skip / only-read / only-write / read-and-written, with copy nodes at meta 4
 * and meta 2), 1..4 of them at a time, over a random initial set, comparing
 * rlm_run against the reference closure computed by repeatedly applying
 * lddmc_relprod for every relation until a fixpoint -- and additionally
 * against the same run with every relation pinned to the apply-whole
 * (saturation) schedule of Cor. th:sat-special.
 */
#define RLM_NO_HRE 1

#include <assert.h>
#include <inttypes.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sylvan.h>
#include <sylvan_int.h>

/* ---- hre shims ------------------------------------------------------- */
static jmp_buf  abort_jmp;
static int      abort_expected = 0;
static char     abort_msg[1024];

static void rlm_test_abort(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(abort_msg, sizeof(abort_msg), fmt, ap);
    va_end(ap);
    if (abort_expected) longjmp(abort_jmp, 1);
    fprintf(stderr, "ABORT: %s\n", abort_msg);
    exit(1);
}

#define Abort(...)   rlm_test_abort(__VA_ARGS__)
#define RTmalloc(s)  malloc(s)
#define RTfree(p)    free(p)

#include "vset_lddmc_reachdd.c"

/* ---- the model under test -------------------------------------------- */

#ifndef NLEV
#define NLEV  4          /* state levels                                   */
#endif
#ifndef NVAL
#define NVAL  3          /* values per level                               */
#endif
#ifndef NACT
#define NACT  2          /* action labels                                  */
#endif

typedef struct {
    MDD      mdd;        /* the relation diagram                           */
    MDD      meta;
    MDD      topmeta;
    int      topvar;
    uint32_t cls[NLEV];  /* 0 skip, 1 read+write, 3 only-read, 4 only-write */
    int      size;       /* relation depth minus the action level          */
} trel_t;

static MDD
build_meta(const uint32_t *cls, int n, int *topvar_out, MDD *topmeta_out,
           int *size_out)
{
    uint32_t meta[2 * NLEV + 2];
    int j = 0, size = 0, topvar = -1, last = -1;
    for (int i = 0; i < n; i++) if (cls[i] != 0) last = i;
    for (int i = 0; i <= (last < 0 ? 0 : last); i++) {
        if (cls[i] == 0) { meta[j++] = 0; }
        else if (cls[i] == 3) { meta[j++] = 3; size += 1; }
        else if (cls[i] == 4) { meta[j++] = 4; size += 1; }
        else { meta[j++] = 1; meta[j++] = 2; size += 2; }
        if (cls[i] != 0 && topvar == -1) topvar = i;
    }
    meta[j++] = 5;
    meta[j++] = (uint32_t)-1;
    MDD m = lddmc_cube(meta, j);
    *topvar_out = topvar;
    *size_out   = size;
    *topmeta_out = (topvar < 0) ? lddmc_false
                                : lddmc_cube(meta + topvar, j - topvar);
    return m;
}

/* rel_add_act, transcribed from vset_lddmc.c (copy flags only at meta 4). */
static void
trel_add(trel_t *r, const uint32_t *src, const uint32_t *dst, const int *cpy,
         uint32_t act)
{
    uint32_t vec[2 * NLEV + 2];
    int      cpy_vec[2 * NLEV + 2];
    int i = 0, k = 0;
    MDD meta = r->meta;
    for (;;) {
        const uint32_t v = lddmc_getvalue(meta);
        if (v == 1 || v == 3)      { cpy_vec[k] = 0; vec[k++] = src[i]; }
        else if (v == 2 || v == 4) { cpy_vec[k] = (cpy && cpy[i]) ? 1 : 0;
                                     vec[k++] = dst[i]; }
        else if (v == 5)           { cpy_vec[k] = 0; vec[k++] = act; }
        else if (v == (uint32_t)-1) break;
        if (v == 0 || v == 2 || v == 3 || v == 4) i++;
        meta = lddmc_follow(meta, v);
    }
    r->mdd = lddmc_union_cube_copy(r->mdd, vec, cpy_vec, k);
}

/* ---- reference closure ------------------------------------------------ */

TASK_3(MDD, ref_closure, MDD, S, trel_t*, rels, int, k)
{
    MDD cur = S, prev = lddmc_false, img = lddmc_false;
    lddmc_refs_pushptr(&cur);
    lddmc_refs_pushptr(&prev);
    lddmc_refs_pushptr(&img);
    while (cur != prev) {
        prev = cur;
        for (int m = 0; m < k; m++) {
            img = CALL(lddmc_relprod, cur, rels[m].mdd, rels[m].meta);
            cur = CALL(lddmc_union, cur, img);
            img = lddmc_false;
        }
    }
    lddmc_refs_popptr(3);
    return cur;
}

/* ---- random generation ------------------------------------------------ */

static uint64_t rng_state = 88172645463325252ULL;
static uint32_t rnd(uint32_t n)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return (uint32_t)(rng_state % n);
}

static int
gen_relation(trel_t *r, int allow_copy)
{
    int any_w;
    do {
        any_w = 0;
        for (int i = 0; i < NLEV; i++) {
            uint32_t c = rnd(4);
            r->cls[i] = (c == 0) ? 0 : (c == 1) ? 3 : (c == 2) ? 4 : 1;
            if (r->cls[i] == 1 || r->cls[i] == 4) any_w = 1;
        }
    } while (!any_w);

    r->mdd = lddmc_false;
    r->meta = build_meta(r->cls, NLEV, &r->topvar, &r->topmeta, &r->size);
    lddmc_protect(&r->mdd);
    lddmc_protect(&r->meta);
    lddmc_protect(&r->topmeta);

    const int ntrans = 1 + (int)rnd(6);
    for (int t = 0; t < ntrans; t++) {
        uint32_t src[NLEV], dst[NLEV];
        int      cpy[NLEV];
        for (int i = 0; i < NLEV; i++) {
            src[i] = rnd(NVAL);
            dst[i] = rnd(NVAL);
            /* allow_copy 1 = meta 4 only, which is all vset_lddmc.c's
             * rel_add_act ever builds; allow_copy 2 = meta 2 as well, which
             * the convention admits, lddmc_relprod implements and ldd2bdd
             * accepts, and which the split handles through lddmc_followcopy on
             * the write chain.  Exercising 2 is what keeps that branch honest. */
            cpy[i] = (allow_copy && rnd(2) &&
                      (r->cls[i] == 4 || (allow_copy == 2 && r->cls[i] == 1)))
                   ? 1 : 0;
        }
        trel_add(r, src, dst, cpy, rnd(NACT));
    }
    return 1;
}

static MDD
gen_set(int nstates)
{
    MDD s = lddmc_false;
    lddmc_refs_pushptr(&s);
    for (int i = 0; i < nstates; i++) {
        uint32_t v[NLEV];
        for (int j = 0; j < NLEV; j++) v[j] = rnd(NVAL);
        s = lddmc_union_cube(s, v, NLEV);
    }
    lddmc_refs_popptr(1);
    return s;
}

/* ---- tests ------------------------------------------------------------ */

static int failures = 0;

VOID_TASK_3(run_one, int, seed, int, k, int, allow_copy)
{
    rng_state = 88172645463325252ULL + (uint64_t)seed * 6364136223846793005ULL;
    for (int i = 0; i < 7; i++) rnd(1000);

    trel_t rels[8];
    for (int m = 0; m < k; m++) gen_relation(&rels[m], allow_copy);

    MDD S = gen_set(1 + (int)rnd(4));
    lddmc_refs_pushptr(&S);

    MDD expect = CALL(ref_closure, S, rels, k);
    lddmc_refs_pushptr(&expect);

    rlm_relspec_t specs[8];
    for (int m = 0; m < k; m++) {
        specs[m].mdd        = &rels[m].mdd;
        specs[m].meta       = rels[m].meta;
        specs[m].topmeta    = rels[m].topmeta;
        specs[m].learn      = NULL;
        specs[m].learn_self = NULL;
    }
    rlm_ctx_t *ctx = rlm_ctx_create(NLEV, specs, k);
    MDD got = (ctx == NULL) ? S : rlm_run(ctx, S);
    lddmc_refs_pushptr(&got);

    if (got != expect) {
        failures++;
        printf("  FAIL seed=%d k=%d copy=%d: |expect|=%.0Lf |got|=%.0Lf\n",
               seed, k, allow_copy, lddmc_satcount(expect), lddmc_satcount(got));
        for (int m = 0; m < k; m++) {
            printf("       rel %d cls = [%u %u %u %u]\n", m,
                   rels[m].cls[0], rels[m].cls[1], rels[m].cls[2], rels[m].cls[3]);
        }
    }

    /* Pinned (apply-whole, Cor. th:sat-special) must give the same answer. */
    if (ctx != NULL) {
        rlm_set_pin_all(1);
        rlm_ctx_t *ctx2 = rlm_ctx_create(NLEV, specs, k);
        MDD gotp = rlm_run(ctx2, S);
        rlm_set_pin_all(0);
        if (gotp != expect) {
            failures++;
            printf("  FAIL(pin) seed=%d k=%d copy=%d\n", seed, k, allow_copy);
        }
        rlm_ctx_destroy(ctx2);
    }

    if (ctx != NULL) rlm_ctx_destroy(ctx);
    lddmc_refs_popptr(3);
    for (int m = 0; m < k; m++) {
        lddmc_unprotect(&rels[m].mdd);
        lddmc_unprotect(&rels[m].meta);
        lddmc_unprotect(&rels[m].topmeta);
    }
}

/* Canonicity: two logically equal residual lists are the identical handle. */
VOID_TASK_0(test_canonical)
{
    uint32_t va[3] = { 1, 2, 0 }, vb[3] = { 2, 1, 0 };
    MDD A = lddmc_cube(va, 3), B = lddmc_cube(vb, 3);
    lddmc_refs_push(A); lddmc_refs_push(B);

    rlm_entry_t e1[3] = { {7, A}, {3, B}, {7, A} };   /* dup + unsorted */
    rlm_entry_t e2[2] = { {3, B}, {7, A} };
    int n1 = rlm_sort_dedup(e1, 3);
    int n2 = rlm_sort_dedup(e2, 2);
    MDD L1 = rlm_build(e1, n1);
    lddmc_refs_push(L1);
    MDD L2 = rlm_build(e2, n2);
    lddmc_refs_push(L2);

    if (n1 != 2 || n2 != 2 || L1 != L2) {
        failures++;
        printf("  FAIL canonicity: n1=%d n2=%d L1=%zu L2=%zu\n",
               n1, n2, (size_t)L1, (size_t)L2);
    } else {
        printf("  ok  canonicity: equal lists are the identical handle\n");
    }
    /* And the cell accessors round-trip. */
    if (rlm_pid(L1) != 3 || rlm_res(L1) != B || rlm_pid(rlm_tail(L1)) != 7 ||
        rlm_res(rlm_tail(L1)) != A || rlm_tail(rlm_tail(L1)) != RLM_EPS) {
        failures++;
        printf("  FAIL cell accessors\n");
    } else {
        printf("  ok  cell layout: (pid, residual, tail) round-trips\n");
    }
    /* Duplicate pids in one chain are legal. */
    rlm_entry_t e3[2] = { {5, A}, {5, B} };
    int n3 = rlm_sort_dedup(e3, 2);
    MDD L3 = rlm_build(e3, n3);
    lddmc_refs_push(L3);
    if (n3 != 2 || rlm_pid(L3) != 5 || rlm_pid(rlm_tail(L3)) != 5) {
        failures++;
        printf("  FAIL duplicate pids in one chain\n");
    } else {
        printf("  ok  duplicate pids in one chain\n");
    }
    lddmc_refs_pop(5);
}

/* The copy-node convention must fire on a relation that violates it. */
VOID_TASK_0(test_copy_convention)
{
    /* One only-read level carrying a copy node: legal Sylvan (relprod reads it
     * as "for any value"), forbidden by our convention, and impossible for
     * vset_lddmc.c's rel_add_act to build -- so it is built by hand here. */
    uint32_t cls[NLEV] = { 3, 4, 0, 0 };
    trel_t r;
    r.mdd = lddmc_false;
    r.meta = build_meta(cls, NLEV, &r.topvar, &r.topmeta, &r.size);
    lddmc_refs_push(r.meta);

    /* chain: copy(read level) -> value 1 (write level) -> action 0 */
    uint32_t vec[3]  = { 0, 1, 0 };
    int      cpyv[3] = { 1, 0, 0 };          /* copy at the ONLY-READ level */
    r.mdd = lddmc_union_cube_copy(lddmc_false, vec, cpyv, 3);
    lddmc_refs_push(r.mdd);

    rlm_relspec_t spec;
    spec.mdd = &r.mdd; spec.meta = r.meta; spec.topmeta = r.topmeta;
    spec.learn = NULL; spec.learn_self = NULL;

    abort_expected = 1;
    if (setjmp(abort_jmp) == 0) {
        rlm_ctx_t *c = rlm_ctx_create(NLEV, &spec, 1);
        (void)c;
        failures++;
        printf("  FAIL copy convention: a copy at an only-read level was "
               "accepted\n");
    } else {
        printf("  ok  copy convention fires: %.120s\n", abort_msg);
    }
    abort_expected = 0;
    lddmc_refs_pop(2);
}

/* A copy node at a WRITTEN level must be accepted, and must mean identity. */
VOID_TASK_0(test_copy_written)
{
    uint32_t cls[NLEV] = { 4, 0, 0, 0 };
    trel_t r;
    r.mdd = lddmc_false;
    r.meta = build_meta(cls, NLEV, &r.topvar, &r.topmeta, &r.size);
    lddmc_refs_push(r.meta);
    uint32_t vec[2]  = { 0, 0 };
    int      cpyv[2] = { 1, 0 };             /* copy at the ONLY-WRITE level */
    r.mdd = lddmc_union_cube_copy(lddmc_false, vec, cpyv, 2);
    lddmc_refs_push(r.mdd);

    MDD S = lddmc_false;
    lddmc_refs_pushptr(&S);
    uint32_t s1[NLEV] = { 0, 0, 0, 0 }, s2[NLEV] = { 2, 1, 0, 0 };
    S = lddmc_union_cube(S, s1, NLEV);
    S = lddmc_union_cube(S, s2, NLEV);

    rlm_relspec_t spec;
    spec.mdd = &r.mdd; spec.meta = r.meta; spec.topmeta = r.topmeta;
    spec.learn = NULL; spec.learn_self = NULL;
    rlm_ctx_t *ctx = rlm_ctx_create(NLEV, &spec, 1);
    MDD got = (ctx == NULL) ? S : rlm_run(ctx, S);
    lddmc_refs_push(got);
    MDD expect = CALL(ref_closure, S, &r, 1);
    lddmc_refs_push(expect);
    if (got != expect || got != S) {
        failures++;
        printf("  FAIL copy at a written level: |S|=%.0Lf |got|=%.0Lf "
               "|expect|=%.0Lf\n", lddmc_satcount(S), lddmc_satcount(got),
               lddmc_satcount(expect));
    } else {
        printf("  ok  copy at a written level is the identity\n");
    }
    if (ctx != NULL) rlm_ctx_destroy(ctx);
    lddmc_refs_pop(4);
    lddmc_refs_popptr(1);
}

/*
 * One named case per meta class, checked against lddmc_relprod on the same
 * relation and against a hand-computed answer.  The randomised sweep below
 * covers the same ground statistically; this is the readable table.
 */
static const char *clsname(uint32_t c)
{
    return c == 0 ? "skip" : c == 1 ? "read-and-written"
         : c == 3 ? "only-read" : c == 4 ? "only-write" : "?";
}

VOID_TASK_0(test_meta_classes)
{
    /* Domain: 3 levels, values 0..2.  S = {(0,0,0), (1,1,0)}. */
    MDD S = lddmc_false;
    lddmc_refs_pushptr(&S);
    { uint32_t a[3] = {0,0,0}, b[3] = {1,1,0};
      S = lddmc_union_cube(S, a, 3); S = lddmc_union_cube(S, b, 3); }

    struct { uint32_t cls[NLEV]; uint32_t src[NLEV]; uint32_t dst[NLEV];
             int cpy[NLEV]; const char *what; long expect; } cases[] = {
      /* only-write at level 0: every source value goes to 2   -> +2 states  */
      { {4,0,0,0}, {0,0,0,0}, {2,0,0,0}, {0,0,0,0}, "only-write, unconditional", 4 },
      /* only-read at level 0: identity, guarded by reading 0  -> +0 states  */
      { {3,0,0,0}, {0,0,0,0}, {0,0,0,0}, {0,0,0,0}, "only-read is the identity", 2 },
      /* read-and-written at level 0: 0 -> 2 only               -> +1 state   */
      { {1,0,0,0}, {0,0,0,0}, {2,0,0,0}, {0,0,0,0}, "read 0, write 2", 3 },
      /* copy at an only-write level: the identity              -> +0 states  */
      { {4,0,0,0}, {0,0,0,0}, {0,0,0,0}, {1,0,0,0}, "copy at only-write", 2 },
      /* write at level 1 only, level 0 skipped                 -> +2 states  */
      { {0,4,0,0}, {0,0,0,0}, {0,2,0,0}, {0,0,0,0}, "skip then only-write", 4 },
      /* read level 0, write level 1 (two relations' worth in one)            */
      { {3,4,0,0}, {1,0,0,0}, {0,2,0,0}, {0,0,0,0}, "only-read then only-write", 3 },
    };

    for (unsigned i = 0; i < sizeof(cases)/sizeof(cases[0]); i++) {
        trel_t r;
        r.mdd = lddmc_false;
        r.meta = build_meta(cases[i].cls, NLEV, &r.topvar, &r.topmeta, &r.size);
        lddmc_refs_push(r.meta);
        lddmc_refs_pushptr(&r.mdd);
        trel_add(&r, cases[i].src, cases[i].dst, cases[i].cpy, 0);

        rlm_relspec_t spec = { &r.mdd, r.meta, r.topmeta, NULL, NULL };
        rlm_ctx_t *ctx = rlm_ctx_create(NLEV, &spec, 1);
        MDD got = (ctx == NULL) ? S : rlm_run(ctx, S);
        lddmc_refs_push(got);
        MDD expect = CALL(ref_closure, S, &r, 1);
        lddmc_refs_push(expect);
        long n = (long)lddmc_satcount(got);
        if (got != expect || n != cases[i].expect) {
            failures++;
            printf("  FAIL meta case [%s %s %s %s] %-28s got=%ld expect=%ld "
                   "relprod=%.0Lf\n",
                   clsname(cases[i].cls[0]), clsname(cases[i].cls[1]),
                   clsname(cases[i].cls[2]), clsname(cases[i].cls[3]),
                   cases[i].what, n, cases[i].expect, lddmc_satcount(expect));
        } else {
            printf("  ok  meta case %-28s |S.[[L]]*| = %ld\n", cases[i].what, n);
        }
        if (ctx != NULL) rlm_ctx_destroy(ctx);
        lddmc_refs_pop(3);
        lddmc_refs_popptr(1);
    }
    lddmc_refs_popptr(1);
}

VOID_TASK_0(all_tests)
{
    printf("-- one case per meta class, against lddmc_relprod\n");
    CALL(test_meta_classes);
    printf("-- structural tests\n");
    CALL(test_canonical);
    CALL(test_copy_convention);
    CALL(test_copy_written);

    printf("-- randomised closure tests (rlm_run vs repeated lddmc_relprod)\n");
    int n = 0;
    for (int k = 1; k <= 4; k++) {
        for (int copy = 0; copy <= 2; copy++) {
            for (int seed = 0; seed < 150; seed++) {
                CALL(run_one, seed + 1000 * k + 500000 * copy, k, copy);
                n++;
            }
        }
    }
    printf("   %d randomised cases, %d failures\n", n, failures);
}

int
main(int argc, char **argv)
{
    (void)argc; (void)argv;
    /* RLM_WORKERS>1 exercises the multi-worker path: the sequential schedule
     * never SPAWNs, but lddmc_relprod and lddmc_union do, so the images and
     * unions really do run on several workers -- which is how every LTSmin run
     * behaves.  RLM_SIZES shrinks the tables to force collections. */
    const char *w = getenv("RLM_WORKERS");
    const char *z = getenv("RLM_SIZES");
    const int sz = z != NULL ? atoi(z) : 20;
    lace_start(w != NULL ? (unsigned)atoi(w) : 1, 0);
    sylvan_set_sizes(1LL<<sz, 1LL<<(sz+2), 1LL<<sz, 1LL<<(sz+2));
    sylvan_init_package();
    sylvan_init_ldd();

    RUN(all_tests);

    sylvan_quit();
    lace_stop();
    printf(failures == 0 ? "ALL TESTS PASSED\n" : "%d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
