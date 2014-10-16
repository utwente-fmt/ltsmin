#include <hre/config.h>

#include <stdio.h>

#include <dm/bitvector.h>
#include <dm/dm.h>


void
print_matrix (matrix_t *m)
{
    printf ("matrix(%d, %d)\n", dm_nrows (m), dm_ncols (m));
    dm_print (stdout, m);
    printf ("\n");
}

void
user_bitvector_print (bitvector_t *bv)
{
    int                 i;
    int                 s = bitvector_size (bv);
    printf ("bitvector: %d\n", s);
    for (i = 0; i < s; i++)
        printf ("%c", bitvector_is_set (bv, i) ? '1' : '0');
    printf ("\n");
}

int
max_row_first (matrix_t *r, matrix_t *w, int rowa, int rowb)
{
    int                 i,
                        ra,
                        wa,
                        rb,
                        wb;

    for (i = 0; i < dm_ncols (r); i++) {
        ra = dm_is_set (r, rowa, i);
        wa = dm_is_set (w, rowa, i);
        rb = dm_is_set (r, rowb, i);
        wb = dm_is_set (w, rowb, i);

        if ((ra && wa && rb && wb) || (!ra && !wa && !rb && !wb))
            continue;
        return (rb + wb - ra - wa);
    }

    return 0;
}

int
min_row_first (matrix_t *r, matrix_t *w, int rowa, int rowb)
{
    int                 i,
                        ra,
                        wa,
                        rb,
                        wb;

    for (i = 0; i < dm_ncols (r); i++) {
        ra = dm_is_set (r, rowa, i);
        wa = dm_is_set (w, rowa, i);
        rb = dm_is_set (r, rowb, i);
        wb = dm_is_set (w, rowb, i);

        if ((ra && wa && rb && wb) || (!ra && !wa && !rb && !wb))
            continue;
        return (ra + wa - rb - wb);
    }

    return 0;
}

int
max_col_first (matrix_t *r, matrix_t *w, int cola, int colb)
{
    int                 i,
                        ra,
                        wa,
                        rb,
                        wb;

    for (i = 0; i < dm_nrows (r); i++) {
        ra = dm_is_set (r, cola, i);
        wa = dm_is_set (w, cola, i);
        rb = dm_is_set (r, colb, i);
        wb = dm_is_set (w, colb, i);

        if ((ra && wa && rb && wb) || (!ra && !wa && !rb && !wb))
            continue;
        return (rb + wb - ra - wa);
    }

    return 0;
}

int
min_col_first (matrix_t *r, matrix_t *w, int cola, int colb)
{
    int                 i,
                        ra,
                        wa,
                        rb,
                        wb;

    for (i = 0; i < dm_nrows (r); i++) {
        ra = dm_is_set (r, cola, i);
        wa = dm_is_set (w, cola, i);
        rb = dm_is_set (r, colb, i);
        wb = dm_is_set (w, colb, i);

        if ((ra && wa && rb && wb) || (!ra && !wa && !rb && !wb))
            continue;
        return (ra + wa - rb - wb);
    }

    return 0;
}

int
eq_rows(matrix_t *r, matrix_t *mayw, matrix_t *mustw, int rowa, int rowb, void *context) {
    if (    dm_ones_in_row (r, rowa) != dm_ones_in_row (r, rowb) ||
            dm_ones_in_row (mayw, rowa) != dm_ones_in_row (mayw, rowb) ||
            dm_ones_in_row (mustw, rowa) != dm_ones_in_row (mustw, rowb))
        return 0;
    int                 i;
    for (i = 0; i < dm_ncols (r); i++) {
        int                 ar = dm_is_set (r, rowa, i);
        int                 br = dm_is_set (r, rowb, i);
        int                 amayw = dm_is_set (mayw, rowa, i);
        int                 bmayw = dm_is_set (mayw, rowb, i);
        int                 amustw = dm_is_set (mustw, rowa, i);
        int                 bmustw = dm_is_set (mustw, rowb, i);
        if (ar != br || amayw != bmayw || amustw != bmustw)
            return 0;                  // unequal
    }
    return 1;                          // equal
    (void)context;
}

int
eq_cols(matrix_t *r, matrix_t *mayw, matrix_t *mustw, int cola, int colb) {
    if (    dm_ones_in_col (r, cola) != dm_ones_in_col (r, colb) ||
            dm_ones_in_col (mayw, cola) != dm_ones_in_col (mayw, colb) ||
            dm_ones_in_col (mustw, cola) != dm_ones_in_col (mustw, colb))
        return 0;
    int                 i;
    for (i = 0; i < dm_nrows (r); i++) {
        int                 ar = dm_is_set (r, i, cola);
        int                 br = dm_is_set (r, i, colb);
        int                 amayw = dm_is_set (mayw, i, cola);
        int                 bmayw = dm_is_set (mayw, i, colb);
        int                 amustw = dm_is_set (mustw, i, cola);
        int                 bmustw = dm_is_set (mustw, i, colb);
        if (ar != br || amayw != bmayw || amustw != bmustw)
            return 0;                  // unequal
    }
    return 1;                          // equal
}


int
subsume_rows(matrix_t *r, matrix_t *mayw, matrix_t *mustw, int rowa, int rowb, void *context) {
    int                 i;
    for (i = 0; i < dm_ncols (r); i++) {
        int a = 4;
        if (dm_is_set(mayw, rowa, i)) a |= 1;
        if (dm_is_set(mustw, rowa, i)) a = 1;
        if (dm_is_set(r, rowa, i)) a |= 6;

        int b = 4;
        if (dm_is_set(mayw, rowb, i)) b |= 1;
        if (dm_is_set(mustw, rowb, i)) b = 1;
        if (dm_is_set(r, rowb, i)) b |= 6;

        if (a < (a|b))
            return 0;                  // not subsumed
    }
    return 1;                          // subsumed
    (void)context;
}

int
subsume_cols(matrix_t *r, matrix_t *mayw, matrix_t *mustw, int cola, int colb) {
    int                 i;
    for (i = 0; i < dm_nrows (r); i++) {
        int a = 4;
        if (dm_is_set(mayw, i, cola)) a |= 1;
        if (dm_is_set(mustw, i, cola)) a = 1;
        if (dm_is_set(r, i, cola)) a |= 6;

        int b = 4;
        if (dm_is_set(mayw, i, colb)) b |= 1;
        if (dm_is_set(mustw, i, colb)) b = 1;
        if (dm_is_set(r, i, colb)) b |= 6;

        if (a < (a|b))
            return 0;                  // not subsumed
    }
    return 1;                          // equal
}


int
main (void)
{

    bitvector_t         b1;
    bitvector_t         b2;

    // test size zero bitvectors
    printf ("bitvector_create(&b1, 0) = %d (should be 0)\n", bitvector_create (&b1, 0));
    printf ("bitvector_copy(&b2, &b1) = %d (should be 0)\n", bitvector_copy (&b2, &b1));

    bitvector_free (&b2);
    bitvector_free (&b1);

    bitvector_create (&b1, 20);

    user_bitvector_print (&b1);

    bitvector_set (&b1, 4);
    user_bitvector_print (&b1);

    bitvector_copy (&b2, &b1);

    bitvector_unset (&b1, 4);
    user_bitvector_print (&b1);

    user_bitvector_print (&b2);

    // test is_empty
    printf ("is_empty b1? %c (should be t)\n", bitvector_is_empty(&b1)?'t':'f');
    printf ("is_empty b2? %c (should be f)\n", bitvector_is_empty(&b2)?'t':'f');

    // set even/odd bits in b1/b2
    for(int i=0; i<20; ++i)
    {
        if (i%2)
        {
            bitvector_set(&b1,i);
        } else {
            bitvector_set(&b2,i);
        }
    }

    // print before union
    printf ("before union\n");
    user_bitvector_print (&b1);
    user_bitvector_print (&b2);

    // disjoint?
    printf ("b1,b2 are disjoint %c (should be t)\n", bitvector_is_disjoint(&b1, &b2)?'t':'f');

    printf ("union\n");
    bitvector_union(&b1, &b2);

    // disjoint?
    printf ("b1,b2 are disjoint %c (should be f)\n", bitvector_is_disjoint(&b1, &b2)?'t':'f');

    // print after union
    user_bitvector_print (&b1);
    user_bitvector_print (&b2);
    printf ("intersect\n");
    bitvector_intersect(&b1, &b2);

    // disjoint?
    printf ("b1,b2 are disjoint %c (should be f)\n", bitvector_is_disjoint(&b1, &b2)?'t':'f');

    // print after intersection
    user_bitvector_print (&b1);
    user_bitvector_print (&b2);

    printf ("invert b1\n");
    bitvector_invert(&b1);

    // print after inversion
    user_bitvector_print (&b1);

    // disjoint?
    printf ("b1,b2 are disjoint %c (should be t)\n", bitvector_is_disjoint(&b1, &b2)?'t':'f');

    bitvector_free (&b2);
    bitvector_free (&b1);

    matrix_t            m1;
    matrix_t            m2;
    matrix_t            m3;
    dm_create (&m1, 10, 10);

    print_matrix (&m1);
    printf ("dm_set(4,4)\n");
    dm_set (&m1, 4, 4);
    print_matrix (&m1);
    printf ("dm_unset(4,4)\n");
    dm_unset (&m1, 4, 4);
    print_matrix (&m1);

    printf ("test shift permutation (3,4,5)(6,7)\n");
    printf ("before\n");
    dm_set (&m1, 3, 3);
    dm_set (&m1, 4, 4);
    dm_set (&m1, 5, 5);
    dm_set (&m1, 6, 6);
    dm_set (&m1, 7, 7);
    print_matrix (&m1);

    printf ("after\n");
    // create permutation_group, apply
    permutation_group_t o1;
    dm_create_permutation_group (&o1, 2, NULL);
    dm_add_to_permutation_group (&o1, 3);
    dm_add_to_permutation_group (&o1, 4);
    dm_add_to_permutation_group (&o1, 5);
    dm_close_group (&o1);
    dm_add_to_permutation_group (&o1, 6);
    dm_add_to_permutation_group (&o1, 7);

    dm_permute_cols (&m1, &o1);

    print_matrix (&m1);

    dm_free_permutation_group (&o1);

    printf ("swap cols 6,7\n");
    dm_swap_cols (&m1, 6, 7);
    print_matrix (&m1);

    printf ("swap rows 6,7\n");
    dm_swap_rows (&m1, 6, 7);
    print_matrix (&m1);

    printf ("copy\n");
    dm_copy (&m1, &m2);
    // TODO: needs some more work
    print_matrix (&m2);

    dm_copy(&m1, &m3);

    dm_sort_rows (&m1, &m2, &m3, &min_row_first);
    print_matrix (&m1);

    dm_sort_rows (&m1, &m2, &m3, &max_row_first);
    print_matrix (&m1);

    dm_print_perm (&(m1.row_perm));

    printf ("to nub rows added & resorted\n");
    dm_set (&m1, 7, 3);
    dm_set (&m1, 8, 4);
    dm_sort_rows (&m1, &m2, &m3, &max_row_first);
    print_matrix (&m1);

    printf ("flatten \n");
    dm_flatten (&m1);

    print_matrix (&m1);
    dm_print_perm (&(m1.row_perm));

    printf ("nub sorted\n");

    dm_nub_rows (&m1, &m2, &m3, &eq_rows, NULL);

    print_matrix (&m1);

    dm_print_perm (&(m1.row_perm));
    /* 
     * printf("again, now to test row order & nub idx\n"); dm_free(&m1);
     * dm_create(&m1, 10, 10); dm_set(&m1, 0,0); dm_set(&m1, 1,0);
     * dm_set(&m1, 2,3); dm_set(&m1, 3,3); print_matrix(&m1);
     * 
     * printf("nub sorted\n");
     * 
     * dm_nub_rows(&m1);
     * 
     * print_matrix(&m1);
     * 
     * dm_print_perm(&(m1.row_perm)); */
    printf ("optimize sorted\n");
    dm_set (&m1, 0, 7);
    dm_set (&m1, 1, 6);
    dm_set (&m1, 3, 9);

    printf ("before\n");
    print_matrix (&m1);
    dm_optimize (&m1, &m2, &m3);
    printf ("after\n");
    print_matrix (&m1);

    printf ("resorted\n");
    dm_sort_rows (&m1, &m2, &m3, &max_row_first);
    print_matrix (&m1);

/*
    dm_nub_cols(&m1);
    dm_ungroup_rows(&m1);
    dm_ungroup_cols(&m1);
    print_matrix (&m1);
*/

    // get bitvector from matrix text
    bitvector_create (&b1, 6);
    bitvector_create (&b2, 10);

    printf ("get bitvector row, invalid size\n"
            "returns %d (should be -1)\n",
            dm_bitvector_row(&b1, &m1, 0));
    printf ("get bitvector row, correct size\n"
            "returns %d (should be 0)\n",
            dm_bitvector_row(&b2, &m1, 0));

    printf ("bitvector of row 0\n");
    user_bitvector_print (&b2);

    printf ("get bitvector col, correct size\n"
            "returns %d (should be 0)\n",
            dm_bitvector_col(&b1, &m1, 8));

    printf ("bitvector of col 8\n");
    user_bitvector_print (&b1);

    bitvector_free (&b2);
    bitvector_free (&b1);

    printf ("count test\n");
    for (int i = 0; i < dm_nrows (&m1); i++)
        printf ("ones in row %d: %d\n", i, dm_ones_in_row (&m1, i));
    for (int i = 0; i < dm_ncols (&m1); i++)
        printf ("ones in col %d: %d\n", i, dm_ones_in_col (&m1, i));

    printf ("iterator test\n");

    dm_row_iterator_t   mx;
    dm_col_iterator_t   my;

    for (int i = 0; i < dm_nrows (&m1); i++) {
        printf ("iterator row: %d\n", i);
        dm_create_row_iterator (&mx, &m1, i);
        int                 r;
        while ((r = dm_row_next (&mx)) != -1)
            printf (" next: %d\n", r);
        printf ("\n\n");
    }

    for (int i = 0; i < dm_ncols (&m1); i++) {
        printf ("iterator col: %d\n", i);
        dm_create_col_iterator (&my, &m1, i);
        int                 r;
        while ((r = dm_col_next (&my)) != -1)
            printf (" next: %d\n", r);
        printf ("\n\n");
    }

    printf ("projection test\n");
    int                 s0[10];
    int                 src[10];
    int                 prj[2];
    int                 tgt[10];

    // initialize
    for (int i = 0; i < 10; i++) {
        s0[i] = -i;
        src[i] = i;
    }

    // do projection
    int                 prj_n = dm_project_vector (&m1, 0, src, prj);

    // print projection
    printf ("projection:");
    for (int i = 0; i < prj_n; i++) {
        printf (" %d", prj[i]);
    }
    printf ("\n");

    printf ("expansion test\n");

    // do expansion
    int                 exp_n = dm_expand_vector (&m1, 0, s0, prj, tgt);
    (void)exp_n;

    // print expansion
    printf ("expansion:");
    for (int i = 0; i < 10; i++) {
        printf (" %d", tgt[i]);
    }
    printf ("\n");

    // subsumption
    printf ("subsumption test:\n");
    dm_swap_rows (&m1, 0, 3);
    dm_swap_rows (&m1, 1, 2);
    dm_flatten (&m1);
    print_matrix (&m1);
    dm_subsume_rows (&m1, &m2, &m3, &subsume_rows, NULL);
    printf ("after subsumption:\n");
    print_matrix (&m1);
    printf ("\n");

    printf ("after ungrouping:\n");
    dm_ungroup_rows (&m1);
    print_matrix (&m1);
    printf ("\n");

    printf ("column sort test:\n");
    dm_flatten (&m1);
    printf ("max col first:\n");
    dm_sort_cols (&m1, &m2, &m3, &max_col_first);
    print_matrix (&m1);
    printf ("min col first:\n");
    dm_sort_cols (&m1, &m2, &m3, &min_col_first);
    print_matrix (&m1);

    printf ("nub columns test:\n");
    dm_set (&m1, 0, 1);
    dm_set (&m1, 3, 1);
    dm_set (&m1, 3, 4);
    dm_set (&m1, 3, 5);
    dm_copy(&m1, &m2);
    dm_copy(&m1, &m3);
    dm_sort_cols (&m1, &m2, &m3, &max_col_first);
    // dm_flatten(&m1);
    printf ("max col first:\n");
    print_matrix (&m1);
    printf ("nub columns:\n");
    dm_subsume_cols (&m1, &m2, &m3, &subsume_cols);
    dm_subsume_rows (&m1, &m2, &m3, &subsume_rows, NULL);
    print_matrix (&m1);
    printf ("column permutation:\n");
    dm_print_perm (&(m1.col_perm));

    printf ("optimize columns:\n");
    dm_optimize (&m1, &m2, &m3);
    print_matrix (&m1);

    printf ("ungroup columns:\n");
    dm_ungroup_cols (&m1);
    print_matrix (&m1);


    printf ("all permutations:\n");
    dm_set (&m1, 0, 9);
    dm_nub_cols(&m1, &m2, &m3, &eq_cols);
    print_matrix (&m1);
    dm_all_perm (&m1, &m2, &m3);

    dm_free (&m2);
    dm_free (&m1);

    return 0;
}
