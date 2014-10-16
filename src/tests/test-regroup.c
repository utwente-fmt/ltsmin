#include <hre/config.h>

#include <stdio.h>

#include <dm/dm.h>


int
resize_matrix (matrix_t *m, int rows, int cols)
{
    matrix_t            m_new;
    printf ("resize_matrix %d %d\n", rows, cols);
    dm_create (&m_new, rows, cols);

    // copy data
    int                 i,
                        j;
    for (i = 0; i < dm_nrows (m); i++) {
        for (j = 0; j < dm_ncols (m); j++) {
            if (dm_is_set (m, i, j))
                dm_set (&m_new, i, j);
        }
    }

    dm_free (m);
    *m = m_new;
    return 1;
}

matrix_t
read_matrix ()
{
    matrix_t            m;
    dm_create (&m, 100, 100);

    char                c;
    int                 row = -1;
    int                 col = -1;
    int                 max_row = 0,
        max_col = 0;

    while ((c = getchar ()) != EOF) {
        col++;
        if (col == 0)
            max_row = ++row + 1;

        if (c == '+' || c == '1') {
            if (col >= dm_ncols (&m))
                resize_matrix (&m, dm_nrows (&m), dm_ncols (&m) * 2);
            if (row >= dm_nrows (&m))
                resize_matrix (&m, dm_nrows (&m) * 2, dm_ncols (&m));
            dm_set (&m, row, col);
        }

        if (c == '-' || c == '0') { } ;
        if (c == '\n') {
            max_col = max_col < col ? col : max_col;
            col = -1;
        }
    }

    resize_matrix (&m, max_row, max_col);

    return m;
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
state_mapping (matrix_t *m)
{
    // optimized matrix row -> original matrix rows..
    int                 i,
                        sepi;
    printf ("[");
    for (i = 0, sepi = 0; i < dm_ncols (m); i++) {
        if (sepi)
            printf (",");
        sepi = 1;
        printf ("(%d,[", i);

        // TODO unify with transition mapping
        printf ("%d", m->col_perm.data[i].becomes);

        printf ("])");
    }
    printf ("]\n");
    return 1;
}

int
transition_mapping (matrix_t *m)
{
    // optimized matrix row -> original matrix rows..
    int                 i,
                        sepi,
                        sepj;
    printf ("[");
    for (i = 0, sepi = 0; i < dm_nrows (m); i++) {
        if (sepi)
            printf (",");
        sepi = 1;
        printf ("(%d,[", i);

        sepj = 0;
        int                 group = m->row_perm.data[i].becomes;
        int                 allg = group;
        do {
            if (sepj)
                printf (",");
            sepj = 1;
            printf ("%d", allg);
            allg = m->row_perm.data[allg].group;
        } while (allg != group);

        printf ("])");
    }
    printf ("]\n");
    return 1;
}

int
dependencies (matrix_t *m)
{
    int                 i,
                        j;
    int                 sepi,
                        sepj;
    printf ("[");
    for (i = 0, sepi = 0; i < dm_nrows (m); i++) {
        if (sepi)
            printf (",");
        sepi = 1;
        printf ("(%d,[", i);
        for (j = 0, sepj = 0; j < dm_ncols (m); j++) {
            if (dm_is_set (m, i, j)) {
                if (sepj)
                    printf (",");
                sepj = 1;
                printf ("%d", j);
            }
        }
        printf ("])");
    }
    printf ("]\n");
    return 1;
}


int
main (void)
{
    matrix_t            r = read_matrix ();
    matrix_t            mayw;
    matrix_t            mustw;
    dm_copy(&r, &mayw);
    dm_copy(&r, &mustw);
    printf ("matrix:\n");
    dm_print (stdout, &r);
    // nub
    dm_sort_rows (&r, &mayw, &mustw, &max_row_first);
    dm_nub_rows (&r, &mayw, &mustw, &eq_rows, NULL);
    // optimize
    dm_optimize (&r, &mayw, &mustw);
    // sort again
    dm_sort_rows (&r, &mayw, &mustw, &max_row_first);

    printf ("matrix:\n");
    dm_print (stdout, &r);
    printf ("state mapping:\n");
    state_mapping (&r);
    printf ("transition mapping:\n");
    transition_mapping (&r);
    printf ("dependencies:\n");
    dependencies (&r);

    return 0;
}
