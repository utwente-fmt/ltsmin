#include "dm.h"

#ifdef DMDEBUG
#define DMDBG(x) x
#else
#define DMDBG(x)
#endif

int
dm_create_header (matrix_header_t *p, int size)
{
    int                 i;
    p->size = size;
    p->data = (header_entry_t *)malloc (sizeof (header_entry_t) * size);
    p->count = (int *)malloc (sizeof (int) * size);
    // TODO: null ptr exception
    for (i = 0; i < p->size; i++) {
        p->data[i].becomes = p->data[i].at = p->data[i].group = i;
        p->count[i] = 0;
    }

    return (p->data != NULL);
}

void
dm_free_header (matrix_header_t *p)
{
    if (p->data != NULL) {
        free (p->data);
        p->size = 0;
        p->data = NULL;
    }
    if (p->count != NULL) {
        free (p->count);
        p->count = NULL;
    }
}

int
dm_copy_header (matrix_header_t *src, matrix_header_t *tgt)
{
    tgt->size = src->size;
    tgt->data =
        (header_entry_t *)malloc (sizeof (header_entry_t) * tgt->size);
    // TODO: null ptr exception
    memcpy (tgt->data, src->data, sizeof (header_entry_t) * tgt->size);
    tgt->count = (int *)malloc (sizeof (int) * tgt->size);
    memcpy (tgt->count, src->count, sizeof (int) * tgt->size);
    return (tgt->data != NULL);
}


int
dm_create_permutation_group (permutation_group_t *o, int size, int *data)
{
    // initialize
    o->size = 0;
    o->data_size = size;
    o->fixed_size = data != NULL;
    if (!o->fixed_size) {
        o->data = (int *)malloc (sizeof (permutation_group_t) * size);
    } else {
        o->data = data;
    }
    return (o->data != NULL);
}

void
dm_free_permutation_group (permutation_group_t *o)
{
    // free
    if (!o->fixed_size && o->data != NULL)
        free (o->data);
    o->size = 0;
    o->data_size = 0;
    o->data = NULL;
}

int
dm_add_to_permutation_group (permutation_group_t *o, int idx)
{
    // enough memory?
    if (o->size >= o->data_size) {
        // can realloc?
        if (o->fixed_size)
            return 0;

        // realloc
        int                 new_size = o->size + 20;
        int                *new_data = (int *)realloc (o->data, new_size);
        if (new_data != NULL) {
            o->data_size = new_size;
            o->data = new_data;
        } else {
            return 0;
        }
    }
    // add index to end
    o->data[o->size++] = idx;
    return 1;
}

int
dm_close_group (permutation_group_t *o)
{
    // add last index again
    if (o->size == 0)
        return 0;

    dm_add_to_permutation_group (o, o->data[o->size - 1]);
    return 1;
}

int
dm_create (matrix_t *m, const int rows, const int cols)
{
    DMDBG (printf ("rows, cols: %d, %d\n", rows, cols);)

    m->rows = rows;
    m->cols = cols;
    m->row_perm.data = NULL;
    m->col_perm.data = NULL;
    m->bits.data = NULL;

    // calculate the number of bits needed per row, dword aligned
    size_t              row_size =
        (cols % 32) ? cols - (cols % 32) + 32 : cols;
    m->bits_per_row = row_size;
    if (!bitvector_create (&(m->bits), rows * row_size)) {
        dm_free (m);
        return 0;
    }
    // create row header
    if (!dm_create_header (&(m->row_perm), rows)) {
        dm_free (m);
        return 0;
    }
    // create column header
    if (!dm_create_header (&(m->col_perm), cols)) {
        dm_free (m);
        return 0;
    }

    return 1;
}

void
dm_free (matrix_t *m)
{
    // free memory
    m->rows = 0;
    m->cols = 0;

    // free bits for matrix
    bitvector_free (&(m->bits));

    // free row header
    dm_free_header (&(m->row_perm));

    // free column header
    dm_free_header (&(m->col_perm));

    return;
}

int
dm_nrows (const matrix_t *m)
{
    return m->rows;
}

int
dm_ncols (const matrix_t *m)
{
    return m->cols;
}

int
dm_set (matrix_t *m, int row, int col)
{
    // get permutation
    int                 rowp = m->row_perm.data[row].becomes;
    int                 colp = m->col_perm.data[col].becomes;

    // calculate bit number
    int                 bitnr = rowp * (m->bits_per_row) + colp;

    // counts
    if (!bitvector_is_set (&(m->bits), bitnr)) {
        m->row_perm.count[rowp]++;
        m->col_perm.count[colp]++;
    }

    return bitvector_set (&(m->bits), bitnr);
}

int
dm_unset (matrix_t *m, int row, int col)
{
    // get permutation
    int                 rowp = m->row_perm.data[row].becomes;
    int                 colp = m->col_perm.data[col].becomes;

    // calculate bit number
    int                 bitnr = rowp * (m->bits_per_row) + colp;

    // counts
    if (bitvector_is_set (&(m->bits), bitnr)) {
        m->row_perm.count[rowp]--;
        m->col_perm.count[colp]--;
    }

    return bitvector_unset (&(m->bits), bitnr);
}


int
dm_is_set (matrix_t *m, int row, int col)
{
    // get permutation
    int                 rowp = m->row_perm.data[row].becomes;
    int                 colp = m->col_perm.data[col].becomes;

    // calculate bit number
    int                 bitnr = rowp * (m->bits_per_row) + colp;
    return bitvector_is_set (&(m->bits), bitnr);
}

inline void
_dm_set_header (matrix_header_t *p, int idx, int val)
{
    p->data[val].at = idx;
    // p->data[u.right_at].group = idx;
    p->data[idx].becomes = val;
}

int
dm_apply_permutation_group (matrix_header_t *p, permutation_group_t *o)
{
    int                 i,
                        last;
    header_entry_t      tmp;

    // no work to do
    if (o->size == 0)
        return 0;

    // store first 
    last = o->data[0];
    tmp = p->data[last];

    // permute rest group
    for (i = 1; i < o->size; i++) {
        int                 current = o->data[i];
        if (current == last) {
            // end group
            _dm_set_header (p, last, tmp.becomes);

            i++;
            if (i < o->size) {
                last = o->data[i];
                tmp = p->data[last];
            }
        } else {
            _dm_set_header (p, last, p->data[current].becomes);
            last = current;
        }
    }
    _dm_set_header (p, last, tmp.becomes);
    return 1;
}

#ifdef DMDEBUG
int
dm_print_perm (matrix_header_t *p)
{
    int                 i;
    for (i = 0; i < p->size; i++) {
        header_entry_t      u;
        u = p->data[i];
        printf ("i %d, becomes %d,  at %d, group %d\n", i, u.becomes, u.at,
                u.group);
    }
    return 1;
}
#endif                          // DMDBG

int
dm_permute_rows (matrix_t *m, permutation_group_t *o)
{
    return dm_apply_permutation_group (&(m->row_perm), o);
}

int
dm_permute_cols (matrix_t *m, permutation_group_t *o)
{
    return dm_apply_permutation_group (&(m->col_perm), o);
}


int
dm_swap_rows (matrix_t *m, int rowa, int rowb)
{
    // swap header
    permutation_group_t o;
    int                 d[2];
    dm_create_permutation_group (&o, 2, d);
    dm_add_to_permutation_group (&o, rowa);
    dm_add_to_permutation_group (&o, rowb);
    dm_permute_rows (m, &o);
    dm_free_permutation_group (&o);
    return 1;
}

int
dm_swap_cols (matrix_t *m, int cola, int colb)
{
    // swap header
    permutation_group_t o;
    int                 d[2];
    dm_create_permutation_group (&o, 2, d);
    dm_add_to_permutation_group (&o, cola);
    dm_add_to_permutation_group (&o, colb);
    dm_permute_cols (m, &o);
    dm_free_permutation_group (&o);
    return 1;
}

int
dm_copy (matrix_t *src, matrix_t *tgt)
{
    tgt->rows = src->rows;
    tgt->cols = src->cols;
    tgt->bits_per_row = src->bits_per_row;
    tgt->row_perm.data = NULL;
    tgt->col_perm.data = NULL;
    tgt->bits.data = NULL;

    if (!dm_copy_header (&(src->row_perm), &(tgt->row_perm))) {
        dm_free (tgt);
        return 0;
    }


    if (!dm_copy_header (&(src->col_perm), &(tgt->col_perm))) {
        dm_free (tgt);
        return 0;
    }

    if (!bitvector_copy (&(src->bits), &(tgt->bits))) {
        dm_free (tgt);
        return 0;
    }

    return 1;
}

int
dm_flatten (matrix_t *m)
{
    matrix_t            m_new;
    int                 i,
                        j;
    dm_create (&m_new, dm_nrows (m), dm_ncols (m));


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

int
dm_print (FILE * f, matrix_t *m)
{
    int                 i,
                        j;
    for (i = 0; i < dm_nrows (m); i++) {
        for (j = 0; j < dm_ncols (m); j++) {
            fprintf (f, "%c", (char)(dm_is_set (m, i, j) ? '1' : '0'));
        }
        fprintf (f, "\n");
    }
    return 1;
}

void
_dm_sift_down (matrix_t *m, int root, int bottom, dm_comparator_fn cmp,
               int (*dm_swap_fn) (matrix_t *, int, int))
{
    int                 child;

    while ((root * 2 + 1) <= bottom) {
        child = root * 2 + 1;

        if (child + 1 <= bottom && cmp (m, child, child + 1) < 0)
            child++;

        if (cmp (m, root, child) < 0) {
            dm_swap_fn (m, root, child);
            root = child;
        } else {
            return;
        }
    }
}

int
_dm_sort (matrix_t *m, dm_comparator_fn cmp, int size,
          int (*dm_swap_fn) (matrix_t *, int, int))
{
    // heapsort
    int                 i;

    for (i = (size / 2) - 1; i >= 0; i--)
        _dm_sift_down (m, i, size - 1, cmp, dm_swap_fn);

    for (i = size - 1; i >= 0; i--) {
        dm_swap_fn (m, i, 0);
        _dm_sift_down (m, 0, i - 1, cmp, dm_swap_fn);
    }
    return 1;
}

int
dm_sort_rows (matrix_t *m, dm_comparator_fn cmp)
{
    return _dm_sort (m, cmp, dm_nrows (m), dm_swap_rows);
}

int
dm_sort_cols (matrix_t *m, dm_comparator_fn cmp)
{
    return _dm_sort (m, cmp, dm_ncols (m), dm_swap_cols);
}

int
_dm_eq_rows (matrix_t *m, int rowa, int rowb)
{
    if (dm_ones_in_row (m, rowa) != dm_ones_in_row (m, rowb))
        return 0;

    int                 i;
    for (i = 0; i < dm_ncols (m); i++) {
        int                 a = dm_is_set (m, rowa, i);
        int                 b = dm_is_set (m, rowb, i);
        if (a != b)
            return 0;                  // unequal
    }
    return 1;                          // equal
}

int
_dm_subsume_rows (matrix_t *m, int rowa, int rowb)
{
    int                 i;
    for (i = 0; i < dm_ncols (m); i++) {
        int                 a = dm_is_set (m, rowa, i);
        int                 b = dm_is_set (m, rowb, i);
        if (a > b)
            return 0;                  // unequal
    }
    return 1;                          // equal
}

int
_dm_eq_cols (matrix_t *m, int cola, int colb)
{
    if (dm_ones_in_col (m, cola) != dm_ones_in_col (m, colb))
        return 0;

    int                 i;
    for (i = 0; i < dm_nrows (m); i++) {
        int                 a = dm_is_set (m, i, cola);
        int                 b = dm_is_set (m, i, colb);
        if (a != b)
            return 0;                  // unequal
    }
    return 1;                          // equal
}

int
_dm_subsume_cols (matrix_t *m, int cola, int colb)
{
    int                 i;
    for (i = 0; i < dm_nrows (m); i++) {
        int                 a = dm_is_set (m, i, cola);
        int                 b = dm_is_set (m, i, colb);
        if (a > b)
            return 0;                  // unequal
    }
    return 1;                          // equal
}

int
_dm_last_in_group (matrix_header_t *h, int group)
{
    int                 start = group;
    while (h->data[start].group != group)
        start = h->data[start].group;

    return start;
}

void
_dm_merge_group (matrix_header_t *h, int groupa, int groupb)
{
    int                 la = _dm_last_in_group (h, groupa);
    int                 lb = _dm_last_in_group (h, groupb);

    // merge
    h->data[la].group = groupb;
    h->data[lb].group = groupa;
}


// remove groupedrow from the group
void
_dm_unmerge_group (matrix_header_t *h, int groupedrow)
{
    int                 last = _dm_last_in_group (h, groupedrow);
    int                 next = h->data[groupedrow].group;

    h->data[last].group = next;
    h->data[groupedrow].group = groupedrow;
}

void
_dm_uncount_row (matrix_t *m, int row)
{
    // get permutation
    int                 rowp = m->row_perm.data[row].becomes;

    int                 i;
    for (i = 0; i < dm_ncols (m); i++) {
        if (dm_is_set (m, row, i)) {
            int                 colp = m->col_perm.data[i].becomes;

            m->row_perm.count[rowp]--;
            m->col_perm.count[colp]--;
        }
    }
}

void
_dm_count_row (matrix_t *m, int row)
{
    // get permutation
    int                 rowp = m->row_perm.data[row].becomes;

    int                 i;
    for (i = 0; i < dm_ncols (m); i++) {
        if (dm_is_set (m, row, i)) {
            int                 colp = m->col_perm.data[i].becomes;

            m->row_perm.count[rowp]++;
            m->col_perm.count[colp]++;
        }
    }
}

// merge rowa and rowb, remove rowb from the matrix
int
_dm_merge_rows (matrix_t *m, int rowa, int rowb)
{
    int                 rows = dm_nrows (m);
    permutation_group_t o;
    int                 d[rows];

    int                 j;

    // make sure rowb > rowa
    if (rowa == rowb)
        return 0;
    if (rowb < rowa) {
        // in this case, rowa will move 1 row up
        rowa--;
    }
    // create permutation
    dm_create_permutation_group (&o, rows, d);

    // create proper permutation group
    for (j = rowb; j < rows; j++) {
        dm_add_to_permutation_group (&o, j);
    }

    dm_permute_rows (m, &o);
    dm_free_permutation_group (&o);

    // merge the groups (last row in matrix is now rowb)
    _dm_merge_group (&(m->row_perm), m->row_perm.data[rows - 1].becomes,
                     m->row_perm.data[rowa].becomes);

    // update matrix counts
    _dm_uncount_row (m, rows - 1);

    // remove the last row from the matrix
    m->rows--;

    return 1;
}

// unmerge the first item in the group of this row and put it right after
// this row
int
_dm_unmerge_row (matrix_t *m, int row)
{
    int                 org_row = m->row_perm.data[row].becomes;
    int                 org_group = m->row_perm.data[org_row].group;
    int                 next_at = m->row_perm.data[org_group].at;

    // if this row is ungrouped, return
    if (org_row == org_group)
        return 1;

    // remove this row from the group
    _dm_unmerge_group (&(m->row_perm), org_row);

    int                 perm_size = m->row_perm.size;
    permutation_group_t o;
    int                 d[perm_size];

    int                 j;

    // create permutation
    dm_create_permutation_group (&o, perm_size, d);

    // create proper permutation group
    for (j = next_at; j > row; j--) {
        dm_add_to_permutation_group (&o, j);
    }

    dm_permute_rows (m, &o);
    dm_free_permutation_group (&o);

    // update matrix counts
    _dm_count_row (m, j + 1);

    // increase matrix size
    m->rows++;

    return 1;
}

void
_dm_uncount_col (matrix_t *m, int col)
{
    // get permutation
    int                 colp = m->col_perm.data[col].becomes;

    int                 i;
    for (i = 0; i < dm_nrows (m); i++) {
        if (dm_is_set (m, i, col)) {
            int                 rowp = m->row_perm.data[i].becomes;

            m->col_perm.count[colp]--;
            m->row_perm.count[rowp]--;
        }
    }
}

void
_dm_count_col (matrix_t *m, int col)
{
    // get permutation
    int                 colp = m->col_perm.data[col].becomes;

    int                 i;
    for (i = 0; i < dm_nrows (m); i++) {
        if (dm_is_set (m, i, col)) {
            int                 rowp = m->row_perm.data[i].becomes;

            m->col_perm.count[colp]++;
            m->row_perm.count[rowp]++;
        }
    }
}


// merge cola and colb, remove colb from the matrix
int
_dm_merge_cols (matrix_t *m, int cola, int colb)
{
    int                 cols = dm_ncols (m);
    permutation_group_t o;
    int                 d[cols];

    int                 j;

    // make sure colb > cola
    if (cola == colb)
        return 0;
    if (colb < cola) {
        // in this case, cola will move 1 col up
        cola--;
    }
    // create permutation
    dm_create_permutation_group (&o, cols, d);

    // create proper permutation group
    for (j = colb; j < cols; j++) {
        dm_add_to_permutation_group (&o, j);
    }

    dm_permute_cols (m, &o);
    dm_free_permutation_group (&o);

    // merge the groups (last col in matrix is now colb)
    _dm_merge_group (&(m->col_perm), m->col_perm.data[cols - 1].becomes,
                     m->col_perm.data[cola].becomes);

    // update matrix counts
    _dm_uncount_col (m, cols - 1);

    // remove the last col from the matrix
    m->cols--;

    return 1;
}

// unmerge the first item in the group of this col and put it right after
// this col
int
_dm_unmerge_col (matrix_t *m, int col)
{
    int                 org_col = m->col_perm.data[col].becomes;
    int                 org_group = m->col_perm.data[org_col].group;
    int                 next_at = m->col_perm.data[org_group].at;

    // if this col is ungrouped, return
    if (org_col == org_group)
        return 1;

    // remove this col from the group
    _dm_unmerge_group (&(m->col_perm), org_col);

    int                 perm_size = m->col_perm.size;
    permutation_group_t o;
    int                 d[perm_size];

    int                 j;

    // create permutation
    dm_create_permutation_group (&o, perm_size, d);

    // create proper permutation group
    for (j = next_at; j > col; j--) {
        dm_add_to_permutation_group (&o, j);
    }

    dm_permute_cols (m, &o);
    dm_free_permutation_group (&o);

    // update matrix counts
    _dm_count_col (m, j + 1);

    // increase matrix size
    m->cols++;

    return 1;
}

int
dm_nub_rows (matrix_t *m)
{
    int                 i,
                        j;
    for (i = 0; i < dm_nrows (m); i++) {
        for (j = i + 1; j < dm_nrows (m); j++) {
            if (_dm_eq_rows (m, i, j)) {
                _dm_merge_rows (m, i, j);
                // now row j is removed, don't increment it in the for
                // loop
                j--;
            }
        }
    }
    return 1;
}

int
dm_subsume_rows (matrix_t *m)
{
    int                 i,
                        j;
    for (i = 0; i < dm_nrows (m); i++) {
        for (j = i + 1; j < dm_nrows (m); j++) {
            // is row i subsumed by row j?
            if (_dm_subsume_rows (m, i, j)) {
                _dm_merge_rows (m, j, i);
                // now row j is removed, don't increment it in the for
                // loop
                j--;
            } else {
                // is row j subsumed by row i?
                if (_dm_subsume_rows (m, j, i)) {
                    _dm_merge_rows (m, i, j);
                    // now row j is removed, don't increment it in the for 
                    // loop
                    j--;
                }
            }

        }
    }
    return 1;
}

int
dm_nub_cols (matrix_t *m)
{
    int                 i,
                        j;
    for (i = 0; i < dm_ncols (m); i++) {
        for (j = i + 1; j < dm_ncols (m); j++) {
            if (_dm_eq_cols (m, i, j)) {
                _dm_merge_cols (m, i, j);
                // now row j is removed, don't increment it in the for
                // loop
                j--;
            }
        }
    }
    return 1;
}

int
dm_subsume_cols (matrix_t *m)
{
    int                 i,
                        j;
    for (i = 0; i < dm_ncols (m); i++) {
        for (j = i + 1; j < dm_ncols (m); j++) {
            // is col i subsumed by row j?
            if (_dm_subsume_cols (m, i, j)) {
                _dm_merge_cols (m, j, i);
                // now col j is removed, don't increment it in the for
                // loop
                j--;
            } else {
                // is col j subsumed by row i?
                if (_dm_subsume_cols (m, j, i)) {
                    _dm_merge_cols (m, i, j);
                    // now col j is removed, don't increment it in the for 
                    // loop
                    j--;
                }
            }

        }
    }
    return 1;
}

int
dm_ungroup_rows (matrix_t *m)
{
    int                 i;
    for (i = 0; i < dm_nrows (m); i++) {
        _dm_unmerge_row (m, i);
    }

    return 1;
}

int
dm_ungroup_cols (matrix_t *m)
{
    int                 i;
    for (i = 0; i < dm_ncols (m); i++) {
        _dm_unmerge_col (m, i);
    }

    return 1;
}

int
_dm_first (matrix_t *m, int row)
{
    int                 i;
    for (i = 0; i < dm_ncols (m); i++) {
        if (dm_is_set (m, row, i))
            return i;
    }
    return -1;
}
int
_dm_last (matrix_t *m, int row)
{
    int                 i;
    for (i = dm_ncols (m) - 1; i >= 0; i--) {
        if (dm_is_set (m, row, i))
            return i;
    }
    return -1;
}

inline int
_dm_row_costs (matrix_t *m, int row)
{
    return (_dm_last (m, row) - _dm_first (m, row) + 1);
}

int
_dm_cost (matrix_t *m)
{
    int                 i,
                        result;
    result = 0;
    for (i = 0; i < dm_nrows (m); i++)
        result += _dm_row_costs (m, i);
    return result;
}

int
dm_optimize (matrix_t *m)
{
    int                 d_rot[dm_ncols (m)];
    permutation_group_t rot;

    int                 best_i,
                        best_j,
                        min,
                        last_min;
    int                 i,
                        j,
                        c,
                        k,
                        d;
    last_min = 0;
    min = _dm_cost (m);
    while (last_min != min) {
        last_min = min;
        // find best rotation
        for (i = 0; i < dm_ncols (m); i++) {
            for (j = 0; j < dm_ncols (m); j++) {
                if (i != j) {
                    d = i < j ? 1 : -1;
                    // rotate
                    dm_create_permutation_group (&rot, dm_ncols (m),
                                                 d_rot);

                    for (k = i; k != j; k += d)
                        dm_add_to_permutation_group (&rot, k);
                    dm_add_to_permutation_group (&rot, j);

                    dm_permute_cols (m, &rot);
                    // dm_print(stdout, m);
                    dm_free_permutation_group (&rot);

                    // calculate new costs
                    c = _dm_cost (m);
                    if (c < min) {
                        min = c;
                        best_i = i;
                        best_j = j;
                    }
                    // printf("rotate %d-%d, costs %d\n", i, j,
                    // _dm_cost(m));

                    // unrotate
                    dm_create_permutation_group (&rot, dm_ncols (m),
                                                 d_rot);

                    for (k = j; k != i; k += -d)
                        dm_add_to_permutation_group (&rot, k);
                    dm_add_to_permutation_group (&rot, i);

                    dm_permute_cols (m, &rot);
                    dm_free_permutation_group (&rot);

                    // dm_print(stdout, m);
                    // printf("\n\n");
                }
            }
        }

        DMDBG (printf
               ("best rotation: %d-%d, costs %d\n", best_i, best_j, min);)
            // apply it 
            if (best_i != best_j) {
            d = best_i < best_j ? 1 : -1;
            // rotate
            dm_create_permutation_group (&rot, dm_ncols (m), d_rot);

            for (k = best_i; k != best_j; k += d)
                dm_add_to_permutation_group (&rot, k);
            dm_add_to_permutation_group (&rot, best_j);

            dm_permute_cols (m, &rot);
            dm_free_permutation_group (&rot);

            DMDBG (dm_print (stdout, m);)
        }

        best_i = best_j = 0;
    }
    return 1;
}


inline void
_dm_swap (int *i, int x, int y)
{
    int                 tmp = i[x];
    i[x] = i[y];
    i[y] = tmp;
}

void
_current_all_perm (int *p, int size)
{
    int                 i;
    for (i = 0; i < size; i++) {
        printf ("%d ", p[i]);
    }
    printf ("\n");
}

int
dm_all_perm (matrix_t *m)
{
    // http://www.freewebz.com/permute/soda_submit.html
    int                 len = dm_ncols (m);
    int                 perm[len];
    int                 best_perm[len];
    int                 min,
                        last_min;

    min = _dm_cost (m);

    int                 i,
                        j;

    for (i = 0; i < len; i++) {
        perm[i] = best_perm[i] = i;
    }

    while (1) {
        last_min = _dm_cost (m);
        if (last_min < min) {
            memcpy (best_perm, perm, len * sizeof (int));
            min = last_min;
        }
        // debug
        DMDBG (_current_all_perm ((int *)&perm, len);)
        DMDBG (dm_print (stdout, m);)
        DMDBG (printf ("costs: %d\n", last_min);)

        int                 key = len - 1;
        int                 newkey = key;

        // The key value is the first value from the end which
        // is smaller than the value to its immediate right
        while (key > 0 && (perm[key] <= perm[key - 1]))
            key--;
        key--;

        // If key < 0 the data is in reverse sorted order, 
        // which is the last permutation.
        if (key < 0)
            break;

        // perm[key+1] is greater than perm[key] because of how key 
        // was found. If no other is greater, perm[key+1] is used
        while ((newkey > key) && (perm[newkey] <= perm[key]))
            newkey--;

        _dm_swap ((int *)&perm, key, newkey);
        dm_swap_cols (m, key, newkey);


        i = len - 1;
        key++;

        // The tail must end in sorted order to produce the
        // next permutation.

        while (i > key) {
            _dm_swap ((int *)&perm, i, key);
            dm_swap_cols (m, i, key);
            key++;
            i--;
        }
    }

    // permutation:
    DMDBG (printf ("best: %d = ", min);)
    DMDBG (_current_all_perm ((int *)&best_perm, len);)
    DMDBG (printf ("current:");)
    DMDBG (_current_all_perm ((int *)&perm, len);)

    // now iterate over best, find in current and swap
    for (i = 0; i < len - 1; i++) {
        for (j = i; j < len; j++) {
            if (best_perm[i] == perm[j]) {
                DMDBG (printf ("swap %d, %d\n", i, j);)
                _dm_swap ((int *)&perm, i, j);
                dm_swap_cols (m, i, j);
                break;
            }
        }
    }
    DMDBG (printf ("current:");)
    DMDBG (_current_all_perm ((int *)&perm, len);)
    DMDBG (printf ("cost: %d ", _dm_cost (m));)

    return 1;
}

int
dm_create_col_iterator (dm_col_iterator_t *ix, matrix_t *m, int col)
{
    ix->m = m;
    ix->row = 0;
    ix->col = col;
    if (!dm_is_set (m, 0, col))
        dm_col_next (ix);
    return 1;
}

int
dm_create_row_iterator (dm_row_iterator_t *ix, matrix_t *m, int row)
{
    ix->m = m;
    ix->row = row;
    ix->col = 0;
    if (!dm_is_set (m, row, 0))
        dm_row_next (ix);
    return 1;
}

int
dm_col_next (dm_col_iterator_t *ix)
{
    int                 result = ix->row;
    if (result != -1) {
        // advance iterator
        ix->row = -1;
        for (int i = result + 1; i < dm_nrows (ix->m); i++) {
            if (dm_is_set (ix->m, i, ix->col)) {
                ix->row = i;
                break;
            }
        }
    }
    return result;

}

int
dm_row_next (dm_row_iterator_t *ix)
{
    int                 result = ix->col;
    if (result != -1) {
        // advance iterator
        ix->col = -1;
        for (int i = result + 1; i < dm_ncols (ix->m); i++) {
            if (dm_is_set (ix->m, ix->row, i)) {
                ix->col = i;
                break;
            }
        }
    }
    return result;
}

int
dm_ones_in_col (matrix_t *m, int col)
{
    int                 colp = m->col_perm.data[col].becomes;
    return m->col_perm.count[colp];
}

int
dm_ones_in_row (matrix_t *m, int row)
{
    int                 rowp = m->row_perm.data[row].becomes;
    return m->row_perm.count[rowp];
}

int
dm_project_vector (matrix_t *m, int row, int *src, int *tgt)
{
    int                 k = 0;

    // iterate over matrix, copy src to tgt when matrix is set
    for (int i = 0; i < dm_ncols (m); i++) {
        if (dm_is_set (m, row, i)) {
            tgt[k++] = src[i];
        }
    }

    // return lenght of tgt
    return k;
}

int
dm_expand_vector (matrix_t *m, int row, int *s0, int *src, int *tgt)
{
    int                 k = 0;
    for (int i = 0; i < dm_ncols (m); i++) {
        if (dm_is_set (m, row, i)) {
            // copy from source
            tgt[i] = src[k++];
        } else {
            // copy initial state
            tgt[i] = s0[i];
        }
    }
    // return number of copied items from src
    return k;
}
