#include <hre/config.h>

#include <float.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <stdlib.h>
#include <time.h>

#include <dm/dm.h>

#include <hre/user.h>
#include <util-lib/util.h>
#include <ltsmin-lib/ltsmin-standard.h>

#ifdef DMDEBUG
#define DMDBG(x) x
#else
#define DMDBG(x)
#endif

void
dm_clear_header(matrix_header_t* p)
{
    for (int i = 0; i < p->size; i++) {
        p->data[i].becomes = p->data[i].at = p->data[i].group = i;
        p->count[i] = 0;
        p->min[i] = -1;
        p->max[i] = -1;
    }
}

void
dm_create_header(matrix_header_t* p, int size)
{
    p->size = size;
    p->data = RTmalloc(sizeof(header_entry_t) * size);
    p->count = RTmallocZero(sizeof(int) * size);
    p->min = RTmalloc(sizeof(int) * size);
    memset(p->min, -1, sizeof(int) * size);
    p->max = RTmalloc(sizeof(int) * size);
    memset(p->max, -1, sizeof(int) * size);
    for (int i = 0; i < p->size; i++) {
        p->data[i].becomes = p->data[i].at = p->data[i].group = i;
    }
}

void
dm_free_header(matrix_header_t* p)
{
    if (p->data != NULL) {
        RTfree(p->data);
        p->size = 0;
        p->data = NULL;
    }
    if (p->count != NULL) {
        RTfree(p->count);
        p->count = NULL;
    }
    if (p->min != NULL) {
        RTfree(p->min);
        p->min = NULL;
    }
    if (p->max != NULL) {
        RTfree(p->max);
        p->max = NULL;
    }
}

static void
invalidate_min_max(matrix_header_t* p)
{
    memset(p->min, -1, sizeof(int) * p->size);
    memset(p->max, -1, sizeof(int) * p->size);
}

static void
copy_header_(const matrix_header_t* src, matrix_header_t* tgt)
{
    if (tgt->data == NULL && tgt->count == NULL && tgt->min == NULL && tgt->max == NULL) {
        tgt->size = src->size;
        tgt->data = RTmalloc(sizeof(header_entry_t) * tgt->size);
        tgt->count = RTmalloc(sizeof(int) * tgt->size);
        tgt->min = RTmalloc(sizeof(int) * tgt->size);
        tgt->max = RTmalloc(sizeof(int) * tgt->size);
        memcpy(tgt->count, src->count, sizeof(int) * tgt->size);
        memcpy(tgt->min, src->min, sizeof(int) * tgt->size);
        memcpy(tgt->max, src->max, sizeof(int) * tgt->size);
    }
    if (src->size == tgt->size) {
        memcpy(tgt->data, src->data, sizeof(header_entry_t) * tgt->size);
    }
}

void
dm_copy_row_info(const matrix_t* src, matrix_t* tgt)
{
    copy_header_(&(src->row_perm), &(tgt->row_perm));

    invalidate_min_max(&(tgt->col_perm));

    if (tgt->rows != src->rows) {
        for (int i = 0; i < dm_ncols(tgt); i++) {
            const int b = tgt->col_perm.data[i].becomes;
            tgt->col_perm.count[b] = 0;
            for (int j = 0; j < dm_nrows(src); j++) {
                if (dm_is_set(tgt, j, i)) tgt->col_perm.count[b]++;
            }
        }
    }
    tgt->rows = src->rows;
}

void
dm_copy_col_info(const matrix_t* src, matrix_t* tgt)
{
    copy_header_(&(src->col_perm), &(tgt->col_perm));

    invalidate_min_max(&(tgt->row_perm));

    if (tgt->cols != src->cols) {
        for (int i = 0; i < dm_nrows(tgt); i++) {
            const int b = tgt->row_perm.data[i].becomes;
            tgt->row_perm.count[b] = 0;
            for (int j = 0; j < dm_ncols(src); j++) {
                if (dm_is_set(tgt, i, j)) tgt->row_perm.count[b]++;
            }
        }
    }
    tgt->cols = src->cols;
}

void
dm_create_permutation_group(permutation_group_t* o, const int size, int* const data)
{
    // initialize
    o->size = 0;
    o->data_size = size;
    o->fixed_size = data != NULL;
    if (!o->fixed_size) {
        o->data = RTmalloc(sizeof(int) * size);
    } else {
        o->data = data;
    }
}

void
dm_free_permutation_group(permutation_group_t* o)
{
    // free
    if (!o->fixed_size && o->data != NULL) RTfree(o->data);
    o->size = 0;
    o->data_size = 0;
    o->data = NULL;
}

void
dm_add_to_permutation_group(permutation_group_t* o, int idx)
{
    // enough memory?
    if (o->size >= o->data_size) {
        // can realloc?
        if (o->fixed_size) {
            Warning(info, "group not large enough");
            HREexit(LTSMIN_EXIT_FAILURE);
        }

        // realloc
        int new_size = o->size + 20;
        int *new_data = RTrealloc(o->data, sizeof(int) * new_size);
        o->data_size = new_size;
        o->data = new_data;
    }
    // add index to end
    o->data[o->size++] = idx;
}

void
dm_close_group(permutation_group_t* o)
{
    // add last index again
    if (o->size == 0) {
        Warning(info, "group not large enough");
        HREexit(LTSMIN_EXIT_FAILURE);
    }

    dm_add_to_permutation_group(o, o->data[o->size - 1]);
}

void
dm_clear(matrix_t* m)
{
    bitvector_clear(&(m->bits));
    dm_clear_header(&(m->row_perm));
    dm_clear_header(&(m->col_perm));
}

void
dm_fill(matrix_t* m)
{
    for (int i = 0; i < dm_nrows(m); i++) {
        for (int j = 0; j < dm_ncols(m); j++) {
            dm_set(m, i, j);
        }
    }
}

void
dm_create(matrix_t* m, const int rows, const int cols)
{
    DMDBG (printf ("rows, cols: %d, %d\n", rows, cols));

    m->rows = rows;
    m->cols = cols;
    m->row_perm.data = NULL;
    m->col_perm.data = NULL;
    m->bits.data = NULL;

    // calculate the number of bits needed per row, dword aligned
    size_t row_size = (cols % 32) ? cols - (cols % 32) + 32 : cols;
    m->bits_per_row = row_size;
    bitvector_create(&(m->bits), rows * row_size);

    // create row header
    dm_create_header(&(m->row_perm), rows);

    // create column header
    dm_create_header(&(m->col_perm), cols);
}

void
dm_free(matrix_t* m)
{
    // free memory
    m->rows = 0;
    m->cols = 0;

    // free bits for matrix
    bitvector_free(&(m->bits));

    // free row header
    dm_free_header(&(m->row_perm));

    // free column header
    dm_free_header(&(m->col_perm));
}

int
dm_nrows(const matrix_t* m)
{
    return m->rows;
}

int
dm_ncols(const matrix_t* m)
{
    return m->cols;
}

void
dm_set(matrix_t* m, int row, int col)
{
    // get permutation
    int rowp = m->row_perm.data[row].becomes;
    int colp = m->col_perm.data[col].becomes;

    // calculate bit number
    size_t bitnr = (size_t) rowp * (m->bits_per_row) + colp;

    // counts
    if (!bitvector_is_set(&(m->bits), bitnr)) {
        m->row_perm.count[rowp]++;
        m->col_perm.count[colp]++;

        // if cache is valid; update min and max
        if (m->row_perm.min[rowp] != -1 || dm_ones_in_row(m, row) == 0) {
            if (colp < m->row_perm.min[rowp]) m->row_perm.min[rowp] = colp;
        }
        if (m->col_perm.min[colp] != -1 || dm_ones_in_col(m, col) == 0) {
            if (rowp < m->col_perm.min[colp]) m->col_perm.min[colp] = rowp;
        }
        if (m->row_perm.max[rowp] != -1 || dm_ones_in_row(m, row) == 0) {
            if (colp > m->row_perm.max[rowp]) m->row_perm.max[rowp] = colp;
        }
        if (m->col_perm.max[colp] != -1 || dm_ones_in_col(m, col) == 0) {
            if (rowp > m->col_perm.max[colp]) m->col_perm.max[colp] = rowp;
        }
    }

    bitvector_set(&(m->bits), bitnr);
}

void
dm_unset(matrix_t* m, int row, int col)
{
    // get permutation
    int rowp = m->row_perm.data[row].becomes;
    int colp = m->col_perm.data[col].becomes;

    // calculate bit number
    size_t bitnr = (size_t) rowp * (m->bits_per_row) + colp;

    // counts
    if (bitvector_is_set(&(m->bits), bitnr)) {
        m->row_perm.count[rowp]--;
        m->col_perm.count[colp]--;

        // if cache is valid; update mins and maxs
        if (m->row_perm.min[rowp] != -1 && colp == m->row_perm.min[rowp]) {
            if (dm_ones_in_row(m, row) > 1) {
                for (int i = col + 1; i < dm_ncols(m); i++) {
                    if (dm_is_set(m, row, i)) {
                        m->row_perm.min[rowp] = i;
                        break;
                    }
                }
            } else m->row_perm.min[rowp] = -1;
        }
        if (m->col_perm.min[colp] != -1 && rowp == m->col_perm.min[colp]) {
            if (dm_ones_in_row(m, row) > 1) {
                for (int i = row + 1; i < dm_nrows(m); i++) {
                    if (dm_is_set(m, i, col)) {
                        m->col_perm.min[colp] = i;
                        break;
                    }
                }
            } else m->col_perm.min[colp] = -1;
        }
        if (m->row_perm.max[rowp] != -1 && colp == m->row_perm.max[rowp]) {
            if (dm_ones_in_col(m, col) > 1) {
                for (int i = col - 1; i >= 0; i--) {
                    if (dm_is_set(m, row, i)) {
                        m->row_perm.max[rowp] = i;
                        break;
                    }
                }
            } else m->row_perm.max[rowp] = -1;
        }
        if (m->col_perm.max[colp] != -1 && rowp == m->col_perm.max[colp]) {
            if (dm_ones_in_col(m, col) > 1) {
                for (int i = row - 1; i >= 0; i--) {
                    if (dm_is_set(m, i, col)) {
                        m->col_perm.max[colp] = i;
                        break;
                    }
                }
            } else m->col_perm.max[colp] = -1;
        }
    }

    bitvector_unset(&(m->bits), bitnr);
}

int
dm_is_set(const matrix_t* m, int row, int col)
{
    // get permutation
    int rowp = m->row_perm.data[row].becomes;
    int colp = m->col_perm.data[col].becomes;

    // calculate bit number
    size_t bitnr = (size_t) rowp * (m->bits_per_row) + colp;
    return bitvector_is_set(&(m->bits), bitnr);
}

static inline void
set_header_(matrix_header_t* p, int idx, int val)
{
    p->data[val].at = idx;
    p->data[idx].becomes = val;
}

static void
apply_permutation_group(matrix_header_t* p, const permutation_group_t* o)
{
    // no work to do
    if (o->size == 0) return;

    // store first
    int last = o->data[0];
    header_entry_t tmp = p->data[last];

    // permute rest group
    for (int i = 1; i < o->size; i++) {
        int current = o->data[i];
        if (current == last) {
            // end group
            set_header_(p, last, tmp.becomes);

            i++;
            if (i < o->size) {
                last = o->data[i];
                tmp = p->data[last];
            }
        } else {
            set_header_(p, last, p->data[current].becomes);
            last = current;
        }
    }
    set_header_(p, last, tmp.becomes);
}

void
dm_create_permutation_groups(permutation_group_t** g, int* num_groups, const int* p, const int size)
{
    permutation_group_t* groups = (permutation_group_t*) RTmalloc(sizeof(permutation_group_t) * size);
    *num_groups = 0;
    int rot[size];
    int done[size];
    int num_done = 0;
    memset(done, 0, sizeof(int[size]));
    for (int i = 0; i < size && num_done < size; i++) {
        int c = 0;
        int cur = p[i];
        while (!done[cur]) {
            rot[c++] = cur;
            done[cur] = 1;
            num_done++;
            cur = p[cur];
        }
        if (c > 0) {
            dm_create_permutation_group(&groups[*num_groups], c, NULL);
            for (int i = 0; i < c; i++) {
                dm_add_to_permutation_group(&groups[*num_groups], rot[i]);
            }
            (*num_groups)++;
        }
    }
    groups = (permutation_group_t*) RTrealloc(groups, sizeof(permutation_group_t) * *num_groups);
    *g = groups;
}

void
dm_print_perm(const matrix_header_t* p)
{
    for (int i = 0; i < p->size; i++) {
        header_entry_t u = p->data[i];
        printf("i %d, becomes %d,  at %d, group %d\n", i, u.becomes, u.at, u.group);
    }
}

void
dm_permute_rows(matrix_t* m, const permutation_group_t* o)
{
    invalidate_min_max(&(m->col_perm));

    return apply_permutation_group(&(m->row_perm), o);
}

void
dm_permute_cols(matrix_t* m, const permutation_group_t* o)
{
    invalidate_min_max(&(m->row_perm));

    return apply_permutation_group(&(m->col_perm), o);
}

void
dm_swap_rows(matrix_t* m, const int rowa, const int rowb)
{
    // swap header
    permutation_group_t o;
    int d[2];
    dm_create_permutation_group(&o, 2, d);
    dm_add_to_permutation_group(&o, rowa);
    dm_add_to_permutation_group(&o, rowb);
    dm_permute_rows(m, &o);
    dm_free_permutation_group(&o);
}

void
dm_swap_cols(matrix_t* m, const int cola, const int colb)
{
    // swap header
    permutation_group_t o;
    int d[2];
    dm_create_permutation_group(&o, 2, d);
    dm_add_to_permutation_group(&o, cola);
    dm_add_to_permutation_group(&o, colb);
    dm_permute_cols(m, &o);
    dm_free_permutation_group(&o);
}

void
dm_copy(const matrix_t* src, matrix_t* tgt)
{
    tgt->rows = src->rows;
    tgt->cols = src->cols;
    tgt->bits_per_row = src->bits_per_row;
    tgt->row_perm.data = NULL;
    tgt->row_perm.count = NULL;
    tgt->row_perm.min = NULL;
    tgt->row_perm.max = NULL;
    tgt->col_perm.data = NULL;
    tgt->col_perm.count = NULL;
    tgt->col_perm.min = NULL;
    tgt->col_perm.max = NULL;
    tgt->bits.data = NULL;

    copy_header_(&(src->row_perm), &(tgt->row_perm));

    copy_header_(&(src->col_perm), &(tgt->col_perm));

    if (src->rows != 0 && src->cols != 0) {
        bitvector_copy(&(tgt->bits), &(src->bits));
    } else {
        bitvector_create(&(tgt->bits), 0);
    }
}

void
dm_flatten(matrix_t* m)
{
    matrix_t m_new;
    dm_create(&m_new, dm_nrows(m), dm_ncols(m));

    for (int i = 0; i < dm_nrows(m); i++) {
        for (int j = 0; j < dm_ncols(m); j++) {
            if (dm_is_set(m, i, j)) dm_set(&m_new, i, j);
        }
    }

    dm_free(m);
    *m = m_new;
}

void
dm_print(FILE* f, const matrix_t* m)
{
    fprintf(f, "      ");
    for (int j = 0; j < dm_ncols(m); j += 10)
        fprintf(f, "0         ");
    fprintf(f, "\n");
    for (int i = 0; i < dm_nrows(m); i++) {
        fprintf(f, "%4d: ", i);
        for (int j = 0; j < dm_ncols(m); j++) {
            fprintf(f, "%c", (char) (dm_is_set(m, i, j) ? '+' : '-'));
        }
        fprintf(f, "\n");
    }
}

void
dm_print_combined(FILE* f, const matrix_t* r, const matrix_t* mayw, const matrix_t* mustw)
{
    fprintf(f, "      0");
    for (int j = 0; j + 10 < dm_ncols(r); j += 10)
        fprintf(f, "%10d", j + 10);
    fprintf(f, " \n");
    for (int i = 0; i < dm_nrows(r); i++) {
        fprintf(f, "%4d: ", i);
        for (int j = 0; j < dm_ncols(r); j++) {
            if (dm_is_set(r, i, j) && (dm_is_set(mayw, i, j))) {
                fprintf(f, "+");
            } else if (dm_is_set(r, i, j)) {
                fprintf(f, "r");
            } else if (dm_is_set(mustw, i, j)) {
                fprintf(f, "w");
            } else if (dm_is_set(mayw, i, j)) {
                fprintf(f, "W");
            } else {
                fprintf(f, "-");
            }
        }
        fprintf(f, "\n");
    }
}

static void
sift_down_(matrix_t* m, int root, int bottom, dm_comparator_fn cmp, void
(*dm_swap_fn)(matrix_t*, int, int))
{
    while ((root * 2 + 1) <= bottom) {
        int child = root * 2 + 1;

        if (child + 1 <= bottom && cmp(m, child, child + 1) < 0) child++;

        if (cmp(m, root, child) < 0) {
            dm_swap_fn(m, root, child);
            root = child;
        } else {
            return;
        }
    }
}

static void
sort_(matrix_t* m, dm_comparator_fn cmp, int size, void
(*dm_swap_fn)(matrix_t*, int, int))
{
    // heapsort
    for (int i = (size / 2) - 1; i >= 0; i--) {
        sift_down_(m, i, size - 1, cmp, dm_swap_fn);
    }

    for (int i = size - 1; i >= 0; i--) {
        dm_swap_fn(m, i, 0);
        sift_down_(m, 0, i - 1, cmp, dm_swap_fn);
    }
}

void
dm_sort_rows(matrix_t* m, dm_comparator_fn cmp)
{
    sort_(m, cmp, dm_nrows(m), dm_swap_rows);
}

void
dm_sort_cols(matrix_t* m, dm_comparator_fn cmp)
{
    sort_(m, cmp, dm_ncols(m), dm_swap_cols);
}

static int
last_in_group_(matrix_header_t* h, int group)
{
    int start = group;
    while (h->data[start].group != group) {
        start = h->data[start].group;
    }

    return start;
}

static void
merge_group_(matrix_header_t* h, int groupa, int groupb)
{
    int la = last_in_group_(h, groupa);
    int lb = last_in_group_(h, groupb);

    // merge
    h->data[la].group = groupb;
    h->data[lb].group = groupa;
}

// remove groupedrow from the group
static void
unmerge_group_(matrix_header_t* h, int groupedrow)
{
    int last = last_in_group_(h, groupedrow);
    int next = h->data[groupedrow].group;

    h->data[last].group = next;
    h->data[groupedrow].group = groupedrow;
}

static void
uncount_row_(matrix_t* m, int row)
{
    // get permutation
    int rowp = m->row_perm.data[row].becomes;

    for (int i = 0; i < dm_ncols(m); i++) {
        if (dm_is_set(m, row, i)) {
            int colp = m->col_perm.data[i].becomes;

            m->row_perm.count[rowp]--;
            m->col_perm.count[colp]--;
        }
    }
}

static void
count_row_(matrix_t* m, int row)
{
    // get permutation
    int rowp = m->row_perm.data[row].becomes;

    for (int i = 0; i < dm_ncols(m); i++) {
        if (dm_is_set(m, row, i)) {
            int colp = m->col_perm.data[i].becomes;

            m->row_perm.count[rowp]++;
            m->col_perm.count[colp]++;
        }
    }
}

// merge rowa and rowb, remove rowb from the matrix
static void
merge_rows_(matrix_t* m, int rowa, int rowb)
{
    int rows = dm_nrows(m);
    permutation_group_t o;
    int d[rows];

    // make sure rowb > rowa
    if (rowa == rowb) HREabort(LTSMIN_EXIT_FAILURE);

    if (rowb < rowa) {
        // in this case, rowa will move 1 row up
        rowa--;
    }
    // create permutation
    dm_create_permutation_group(&o, rows, d);

    // create proper permutation group
    for (int j = rowb; j < rows; j++) {
        dm_add_to_permutation_group(&o, j);
    }

    dm_permute_rows(m, &o);
    dm_free_permutation_group(&o);

    // merge the groups (last row in matrix is now rowb)
    merge_group_(&(m->row_perm), m->row_perm.data[rows - 1].becomes, m->row_perm.data[rowa].becomes);

    // update matrix counts
    uncount_row_(m, rows - 1);

    // remove the last row from the matrix
    m->rows--;
}

// unmerge the first item in the group of this row and put it right after
// this row
static void
unmerge_row_(matrix_t* m, int row)
{
    int org_row = m->row_perm.data[row].becomes;
    int org_group = m->row_perm.data[org_row].group;
    int next_at = m->row_perm.data[org_group].at;

    // if this row is ungrouped, return
    if (org_row == org_group) return;

    // remove this row from the group
    unmerge_group_(&(m->row_perm), org_row);

    int perm_size = m->row_perm.size;
    permutation_group_t o;
    int d[perm_size];

    // create permutation
    dm_create_permutation_group(&o, perm_size, d);

    // create proper permutation group
    int j;
    for (j = next_at; j > row; j--) {
        dm_add_to_permutation_group(&o, j);
    }

    dm_permute_rows(m, &o);
    dm_free_permutation_group(&o);

    // update matrix counts
    count_row_(m, j + 1);

    // increase matrix size
    m->rows++;
}

static void
uncount_col_(matrix_t* m, int col)
{
    // get permutation
    int colp = m->col_perm.data[col].becomes;

    for (int i = 0; i < dm_nrows(m); i++) {
        if (dm_is_set(m, i, col)) {
            int rowp = m->row_perm.data[i].becomes;

            m->col_perm.count[colp]--;
            m->row_perm.count[rowp]--;
        }
    }
}

static void
count_col_(matrix_t* m, int col)
{
    // get permutation
    int colp = m->col_perm.data[col].becomes;

    for (int i = 0; i < dm_nrows(m); i++) {
        if (dm_is_set(m, i, col)) {
            int rowp = m->row_perm.data[i].becomes;

            m->col_perm.count[colp]++;
            m->row_perm.count[rowp]++;
        }
    }
}

// merge cola and colb, remove colb from the matrix
static void
merge_cols_(matrix_t* m, int cola, int colb)
{
    int cols = dm_ncols(m);
    permutation_group_t o;
    int d[cols];

    // make sure colb > cola
    if (cola == colb) HREabort(LTSMIN_EXIT_FAILURE);

    if (colb < cola) {
        // in this case, cola will move 1 col up
        cola--;
    }
    // create permutation
    dm_create_permutation_group(&o, cols, d);

    // create proper permutation group
    for (int j = colb; j < cols; j++) {
        dm_add_to_permutation_group(&o, j);
    }

    dm_permute_cols(m, &o);
    dm_free_permutation_group(&o);

    // merge the groups (last col in matrix is now colb)
    merge_group_(&(m->col_perm), m->col_perm.data[cols - 1].becomes, m->col_perm.data[cola].becomes);

    // update matrix counts
    uncount_col_(m, cols - 1);

    // remove the last col from the matrix
    m->cols--;
}

// unmerge the first item in the group of this col and put it right after
// this col
static void
unmerge_col_(matrix_t* m, int col)
{
    int org_col = m->col_perm.data[col].becomes;
    int org_group = m->col_perm.data[org_col].group;
    int next_at = m->col_perm.data[org_group].at;

    // if this col is ungrouped, return
    if (org_col == org_group) return;

    // remove this col from the group
    unmerge_group_(&(m->col_perm), org_col);

    int perm_size = m->col_perm.size;
    permutation_group_t o;
    int d[perm_size];

    // create permutation
    dm_create_permutation_group(&o, perm_size, d);

    // create proper permutation group
    int j;
    for (j = next_at; j > col; j--) {
        dm_add_to_permutation_group(&o, j);
    }

    dm_permute_cols(m, &o);
    dm_free_permutation_group(&o);

    // update matrix counts
    count_col_(m, j + 1);

    // increase matrix size
    m->cols++;
}

void
dm_nub_rows(matrix_t* m, const dm_nub_rows_fn fn, void* context)
{
    for (int i = 0; i < dm_nrows(m); i++) {
        for (int j = i + 1; j < dm_nrows(m); j++) {
            if (fn(m, i, j, context)) {
                merge_rows_(m, i, j);
                // now row j is removed, don't increment it in the for
                // loop
                j--;
            }
        }
    }
}

void
dm_subsume_rows(matrix_t* m, const dm_subsume_rows_fn fn, void* context)
{
    for (int i = 0; i < dm_nrows(m); i++) {

        int row_i_removed = 0;
        for (int j = i + 1; j < dm_nrows(m); j++) {
            // is row j subsumed by row i?
            if (fn(m, i, j, context)) {
                merge_rows_(m, i, j);
                // now row j is removed, don't increment it in the for
                // loop
                j--;
            } else {
                // is row i subsumed by row j?
                if (fn(m, j, i, context)) {
                    merge_rows_(m, j, i);
                    // now row i is removed, don't increment it in the for
                    // loop
                    row_i_removed = 1;
                }
            }

        }

        if (row_i_removed) i--;
    }
}

void
dm_nub_cols(matrix_t* m, const dm_nub_cols_fn fn, void* context)
{
    for (int i = 0; i < dm_ncols(m); i++) {
        for (int j = i + 1; j < dm_ncols(m); j++) {
            if (fn(m, i, j, context)) {
                merge_cols_(m, i, j);
                // now row j is removed, don't increment it in the for loop
                j--;
            }
        }
    }
}

void
dm_subsume_cols(matrix_t* m, const dm_subsume_cols_fn fn, void* context)
{
    for (int i = 0; i < dm_ncols(m); i++) {
        int col_i_removed;
        for (int j = i + 1; j < dm_ncols(m); j++) {
            // is col i subsumed by row j?
            if (fn(m, i, j, context)) {
                merge_cols_(m, i, j);
                // now col j is removed, don't increment it in the for loop
                col_i_removed = 1;
            } else {
                // is col j subsumed by row i?
                if (fn(m, j, i, context)) {
                    merge_cols_(m, j, i);
                    // now col j is removed, don't increment it in the for loop
                    j--;
                }
            }

        }
        if (col_i_removed) i--;
    }
}

void
dm_ungroup_rows(matrix_t* m)
{
    for (int i = 0; i < dm_nrows(m); i++) {
        unmerge_row_(m, i);
    }
}

void
dm_ungroup_cols(matrix_t* m)
{
    for (int i = 0; i < dm_ncols(m); i++) {
        unmerge_col_(m, i);
    }
}

int
dm_first(const matrix_t* const m, const int row)
{
    const int rowp = m->row_perm.data[row].becomes;
    if (m->row_perm.min[rowp] == -1) {
        if (dm_ones_in_row(m, row) > 0) {
            for (int i = 0; i < dm_ncols(m); i++) {
                if (dm_is_set(m, row, i)) {
                    m->row_perm.min[rowp] = i;
                    break;
                }
            }
        } else m->row_perm.min[rowp] = -1;
    }

    return m->row_perm.min[rowp];
}

int
dm_last(const matrix_t* const m, const int row)
{
    const int rowp = m->row_perm.data[row].becomes;
    if (m->row_perm.max[rowp] == -1) {
        if (dm_ones_in_row(m, row) > 0) {
            for (int i = dm_ncols(m) - 1; i >= 0; i--) {
                if (dm_is_set(m, row, i)) {
                    m->row_perm.max[rowp] = i;
                    break;
                }
            }
        } else m->row_perm.max[rowp] = -1;
    }

    return m->row_perm.max[rowp];
}

int
dm_top(const matrix_t* const m, const int col)
{
    const int colp = m->col_perm.data[col].becomes;
    if (m->col_perm.min[colp] == -1) {
        if (dm_ones_in_col(m, col) > 0) {
            for (int i = 0; i < dm_nrows(m); i++) {
                if (dm_is_set(m, i, col)) {
                    m->col_perm.min[colp] = i;
                    break;
                }
            }
        } else m->col_perm.min[colp] = -1;
    }

    return m->col_perm.min[colp];
}

int
dm_bottom(const matrix_t* const m, const int col)
{
    const int colp = m->col_perm.data[col].becomes;
    if (m->col_perm.max[colp] == -1) {
        if (dm_ones_in_col(m, col) > 0) {
            for (int i = dm_nrows(m) - 1; i >= 0; i--) {
                if (dm_is_set(m, i, col)) {
                    m->col_perm.max[colp] = i;
                    break;
                }
            }
        } else m->col_perm.max[colp] = -1;
    }

    return m->col_perm.max[colp];
}

static int sig_show = 0;
static int sig_stop = 0;

static void
catch_sig(int sig)
{
    if (sig == SIGINT) sig_show = 1;
    if (sig == SIGTSTP) sig_stop = 1;
}

static void
print_progress(const matrix_t* const m, dm_cost_t cost, long double num_perms, rt_timer_t timer)
{
    switch (cost) {
    case DM_EVENT_SPAN: {
        int sig_digs = ceil(log10(dm_ncols(m) * (double) dm_nrows(m)));
        const double cost = dm_event_span(m, NULL);
        Warning(info, "Current costs: %.*g (%.*g)", sig_digs, cost, sig_digs, cost / (dm_ncols(m) * (double) dm_nrows(m)));
    } break;
    case DM_WEIGHTED_EVENT_SPAN: {
        int sig_digs = ceil(log10(dm_ncols(m)));
        const double cost = dm_weighted_event_span(m, NULL);
        Warning(info, "Current costs: %.*g", sig_digs, cost);
    } break;
    default:
        Warning(error, "unsupported cost function");
        HREabort(LTSMIN_EXIT_FAILURE);
    }

    Warning(infoLong,
        "Anneal time: %.1f seconds, total permutations: %Lg, %.1Lf permutations/second",
        RTrealTime(timer), num_perms, num_perms / RTrealTime(timer));
    sig_show = 0;
}

// Simulated annealing routine taken from Skiena's Algorithm Design Manual
static int COOL_STEPS = 500;
static int TEMP_STEPS = 1000;
static double INIT_TEMP = 1;
static double COOL_FRAC = 0.97;
static double E = 2.71828;
static double K = 0.01;

void
dm_anneal(matrix_t* m, dm_cost_t cost, const int timeout)
{

    typedef double (*dm_cost_fn)(const matrix_t* const m, int* const vec);
    dm_cost_fn fn;

    switch (cost) {
    case DM_EVENT_SPAN:
        fn = dm_event_span;
        break;
    case DM_WEIGHTED_EVENT_SPAN:
        fn = dm_weighted_event_span;
        break;
    default:
        Warning(error, "unsupported cost function");
        HREabort(LTSMIN_EXIT_FAILURE);
    }

    double cur_cost = fn(m, NULL);

    double temp = INIT_TEMP;

    srandom(time(NULL));

    signal(SIGINT, catch_sig);
    signal(SIGTSTP, catch_sig);
    Warning(info, "Press ctrl+c to show current costs.");
    Warning(info, "Press ctrl+z to stop and use current permutation.");

    rt_timer_t timer = RTcreateTimer();
    RTstartTimer(timer);

    long double num_perms = 0;

    for (int cool_step = 0; cool_step < COOL_STEPS && !sig_stop && (timeout < 0 || RTrealTime(timer) < timeout); cool_step++) {
        double start_cost = cur_cost;
        temp *= COOL_FRAC;

        for (int temp_step = 0; temp_step < TEMP_STEPS && !sig_stop && (timeout < 0 || RTrealTime(timer) < timeout); temp_step++) {
            int i = random() % dm_ncols(m);
            int j = random() % dm_ncols(m);

            if (i != j) {
                int d_rot[dm_ncols(m)];
                permutation_group_t rot;
                int d = i < j ? 1 : -1;

                dm_create_permutation_group(&rot, dm_ncols(m), d_rot);
                for (int k = i; k != j; k += d) {
                    dm_add_to_permutation_group(&rot, k);
                }
                dm_add_to_permutation_group(&rot, j);
                dm_permute_cols(m, &rot);
                dm_free_permutation_group(&rot);
            }

            const double cost = fn(m, NULL);
            double delta = cost - cur_cost;

            if (delta < 0) {
                cur_cost += delta;
            } else {
                double rand = random();
                double flip = rand / LONG_MAX;
                double exp = (-delta / cur_cost) / (K * temp);
                double merit = pow(E, exp);

                if (merit > flip) {
                    cur_cost += delta;
                } else if (i != j) {
                    int d_rot[dm_ncols(m)];
                    permutation_group_t rot;
                    int d = i < j ? 1 : -1;

                    dm_create_permutation_group(&rot, dm_ncols(m), d_rot);
                    for (int k = j; k != i; k += -d) {
                        dm_add_to_permutation_group(&rot, k);
                    }
                    dm_add_to_permutation_group(&rot, i);
                    dm_permute_cols(m, &rot);
                    dm_free_permutation_group(&rot);
                }
            }
            num_perms++;
            if (sig_show) print_progress(m, cost, num_perms, timer);
        }

        if (cur_cost - start_cost < 0.0) temp /= COOL_FRAC;
    }

    if (log_active(infoLong)) {
        Warning(infoLong, "Annealing done");
        print_progress(m, cost, num_perms, timer);
    }

    RTdeleteTimer(timer);
    sig_stop = 0;

    signal(SIGINT, SIG_DFL);
    signal(SIGTSTP, SIG_DFL);
}

int
est_first(matrix_t* m, int row, int i, int j, int first)
{
    // what would first be if column i is rotated to position j
    if (first == -1) return -1; // still no first
    if (i < first) {
        if (j < first) return first; // permutation happens left of first
        else return first - 1; // first is in segment rotated to the left
    }
    if (i > first) {
        if (j <= first) {
            if (dm_is_set(m, row, i)) return j; // set position i is moved left of first
            else return first + 1; // first is in segment rotated to the right
        } else return first; // permutation happens right of first
    }
    // i==first
    if (j < first) return j; // first is moved to the left to position j
    for (int k = i + 1; k <= j; k++)
        if (dm_is_set(m, row, k)) return k - 1; // first is moved to the right, k is the new first, but moves one left
    return j; // first is moved to the right to pos j, no k found, so j is new first
}

int
est_last(matrix_t* m, int row, int i, int j, int last)
{
    // what would first be if column i is rotated to position j
    if (last == -1) return -1; // still no last
    if (i > last) {
        if (j > last) return last; // permutation happens right of last
        else return last + 1; // last is in segment rotated to the right
    }
    if (i < last) {
        if (j >= last) {
            if (dm_is_set(m, row, i)) return j; // set position i is moved right of last
            else return last - 1; // last is in segment rotated to the left
        } else return last; // permutation happens left of last
    }
    // i==last
    if (j > last) return j; // last is moved to the right to position j
    for (int k = i - 1; k >= j; k--) {
        if (dm_is_set(m, row, k)) return k + 1; // last is moved to the left, k is the new last, but moves one right
    }
    return j; // last is moved to the left to pos j, no k found, so j is new last
}

int
estimate_cost(matrix_t* m, int i, int j, int firsts[], int lasts[])
{
    int ec = 0;
    for (int row = 0; row < dm_nrows(m); row++) {
        const int ef = est_first(m, row, i, j, firsts[row]);
        const int el = est_last(m, row, i, j, lasts[row]);
        ec += (el - ef + 1);
    }
    return ec;
}

void
dm_optimize(matrix_t* m)
{
    int d_rot[dm_ncols(m)];
    permutation_group_t rot;

    int best_i = 0, best_j = 0, min = dm_event_span(m, NULL), last_min = 0;

    int firsts[dm_nrows(m)];
    int lasts[dm_nrows(m)];

    while (last_min != min) {
        last_min = min;

        // initialize first and last integers per row
        for (int i = 0; i < dm_nrows(m); i++) {
            firsts[i] = dm_first(m, i);
            lasts[i] = dm_last(m, i);
        }

        // find best rotation
        for (int i = 0; i < dm_ncols(m); i++) {
            for (int j = 0; j < dm_ncols(m); j++) {
                if (i != j) {
                    const int c = estimate_cost(m, i, j, firsts, lasts);
                    if (c < min) {
                        min = c;
                        best_i = i;
                        best_j = j;
                    }
                }
            }
        }

        // rotate
        if (best_i != best_j) {
            const int d = best_i < best_j ? 1 : -1;
            dm_create_permutation_group(&rot, dm_ncols(m), d_rot);
            for (int k = best_i; k != best_j; k += d) {
                dm_add_to_permutation_group(&rot, k);
            }
            dm_add_to_permutation_group(&rot, best_j);
            dm_permute_cols(m, &rot);
            dm_free_permutation_group(&rot);

            DMDBG (printf("best rotation: %d-%d, costs %d\n", best_i, best_j, min));
            DMDBG (dm_print (stdout, m));

            best_i = best_j = 0;
        }
    }
    DMDBG (printf ("cost: %f ", dm_event_span(m, NULL)));
}

static inline void
swap_(int* i, int x, int y)
{
    int tmp = i[x];
    i[x] = i[y];
    i[y] = tmp;
}

static void
#ifdef __GNUC__
__attribute__ ((unused))
#endif
current_all_perm_(int* p, int size)
{
    for (int i = 0; i < size; i++) {
        printf("%d ", p[i]);
    }
    printf("\n");
}

void
dm_all_perm(matrix_t* m)
{

    // http://www.freewebz.com/permute/soda_submit.html
    int len = dm_ncols(m);
    int perm[len];
    int best_perm[len];

    int min = dm_event_span(m, NULL);

    for (int i = 0; i < len; i++) {
        perm[i] = best_perm[i] = i;
    }

    while (1) {
        const int last_min = dm_event_span(m, NULL);
        if (last_min < min) {
            memcpy(best_perm, perm, len * sizeof(int));
            min = last_min;
        }
        // debug
        DMDBG (current_all_perm_ (perm, len));
        DMDBG (printf ("costs: %d\n", last_min));

        int key = len - 1;
        int newkey = key;

        // The key value is the first value from the end which
        // is smaller than the value to its immediate right
        while (key > 0 && (perm[key] <= perm[key - 1])) {
            key--;
        }
        key--;

        // If key < 0 the data is in reverse sorted order, 
        // which is the last permutation.
        if (key < 0) break;

        // perm[key+1] is greater than perm[key] because of how key 
        // was found. If no other is greater, perm[key+1] is used
        while ((newkey > key) && (perm[newkey] <= perm[key])) {
            newkey--;
        }

        swap_(perm, key, newkey);
        dm_swap_cols(m, key, newkey);

        int i = len - 1;
        key++;

        // The tail must end in sorted order to produce the
        // next permutation.

        while (i > key) {
            swap_(perm, i, key);
            dm_swap_cols(m, i, key);
            key++;
            i--;
        }
    }

    // permutation:
    DMDBG (printf ("best: %d = ", min));
    DMDBG (current_all_perm_ (best_perm, len));
    DMDBG (printf ("current:"));
    DMDBG (current_all_perm_ (perm, len));

    // now iterate over best, find in current and swap
    for (int i = 0; i < len - 1; i++) {
        for (int j = i; j < len; j++) {
            if (best_perm[i] == perm[j]) {
                DMDBG (printf ("swap %d, %d\n", i, j));
                swap_(perm, i, j);
                dm_swap_cols(m, i, j);
                break;
            }
        }
    }
    DMDBG (printf ("current:"));
    DMDBG (current_all_perm_ (perm, len));
    DMDBG (printf ("cost: %f ", dm_event_span(m, NULL)));
}

void
dm_horizontal_flip(matrix_t* m)
{
    for (int i = 0; i < dm_nrows(m) / 2; i++) {
        dm_swap_rows(m, i, dm_nrows(m) - i - 1);
    }
}

void
dm_vertical_flip(matrix_t* m)
{
    for (int i = 0; i < dm_ncols(m) / 2; i++) {
        dm_swap_cols(m, i, dm_ncols(m) - i - 1);
    }
}

void
dm_create_col_iterator(dm_col_iterator_t* ix, matrix_t* m, int col)
{
    ix->m = m;
    ix->row = 0;
    ix->col = col;
    if (!dm_is_set(m, 0, col)) dm_col_next(ix);
}

void
dm_create_row_iterator(dm_row_iterator_t* ix, matrix_t* m, int row)
{
    ix->m = m;
    ix->row = row;
    ix->col = 0;
    if (!dm_is_set(m, row, 0)) dm_row_next(ix);
}

int
dm_col_next(dm_col_iterator_t* ix)
{
    int result = ix->row;
    if (result != -1) {
        // advance iterator
        ix->row = -1;
        for (int i = result + 1; i < dm_nrows(ix->m); i++) {
            if (dm_is_set(ix->m, i, ix->col)) {
                ix->row = i;
                break;
            }
        }
    }
    return result;
}

int
dm_row_next(dm_row_iterator_t* ix)
{
    int result = ix->col;
    if (result != -1) {
        // advance iterator
        ix->col = -1;
        for (int i = result + 1; i < dm_ncols(ix->m); i++) {
            if (dm_is_set(ix->m, ix->row, i)) {
                ix->col = i;
                break;
            }
        }
    }
    return result;
}

int
dm_ones_in_col(const matrix_t* m, int col)
{
    int colp = m->col_perm.data[col].becomes;
    return m->col_perm.count[colp];
}

int
dm_ones_in_row(const matrix_t* m, int row)
{
    int rowp = m->row_perm.data[row].becomes;
    return m->row_perm.count[rowp];
}

int
dm_project_vector(matrix_t* m, int row, int* src, int* tgt)
{
    int k = 0;

    // iterate over matrix, copy src to tgt when matrix is set
    for (int i = 0; i < dm_ncols(m); i++) {
        if (dm_is_set(m, row, i)) {
            tgt[k++] = src[i];
        }
    }

    // return lenght of tgt
    return k;
}

int
dm_expand_vector(matrix_t* m, int row, int* s0, int* src, int* tgt)
{
    int k = 0;
    for (int i = 0; i < dm_ncols(m); i++) {
        if (dm_is_set(m, row, i)) {
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

void
dm_row_union(bitvector_t* bv, const matrix_t* m, int row)
{
    // check size
    HREassert(bitvector_size(bv) == (size_t) dm_ncols(m));

    // copy row
    for (int i = 0; i < dm_ncols(m); i++) {
        if (dm_is_set(m, row, i)) bitvector_set(bv, i);
    }
}

void
dm_col_union(bitvector_t* bv, const matrix_t* m, int col)
{
    // check size
    HREassert(bitvector_size(bv) == (size_t) dm_ncols(m));

    // copy row
    for (int i = 0; i < dm_nrows(m); i++) {
        if (dm_is_set(m, i, col)) bitvector_set(bv, i);
    }
}

int**
dm_rows_to_idx_table(const matrix_t* m)
{
    int** result = NULL;
    // count memory needed
    int memcnt = 0;
    for (int i = 0; i < dm_nrows(m); i++) {
        memcnt++;
        for (int j = 0; j < dm_ncols(m); j++) {
            if (dm_is_set(m, i, j)) memcnt++;
        }
    }
    // allocate memory
    result = RTmalloc(dm_nrows(m) * sizeof(int*) + memcnt * sizeof(int));
    if (result) {
        // skip table header
        int* data = (int*) (result + dm_nrows(m));
        int* count;

        for (int i = 0; i < dm_nrows(m); i++) {
            count = data++;
            *count = 0;
            for (int j = 0; j < dm_ncols(m); j++) {
                if (dm_is_set(m, i, j)) {
                    *data++ = j;
                    (*count)++;
                }
            }
            // fill idx table
            result[i] = count;
        }
    }
    return result;
}

int**
dm_cols_to_idx_table(const matrix_t* m)
{
    int ** result = NULL;
    // count memory needed
    int memcnt = 0;
    for (int j = 0; j < dm_ncols(m); j++) {
        memcnt++;
        for (int i = 0; i < dm_nrows(m); i++) {
            if (dm_is_set(m, i, j)) memcnt++;
        }
    }
    // allocate memory
    result = RTmalloc(dm_ncols(m) * sizeof(int*) + memcnt * sizeof(int));
    if (result) {
        // skip table header
        int* data = (int*) (result + dm_ncols(m));
        int* count;

        for (int j = 0; j < dm_ncols(m); j++) {
            count = data++;
            *count = 0;
            for (int i = 0; i < dm_nrows(m); i++) {
                if (dm_is_set(m, i, j)) {
                    *data++ = i;
                    (*count)++;
                }
            }
            // fill idx table
            result[j] = count;
        }
    }
    return result;
}

void
dm_apply_or(matrix_t* a, const matrix_t* b)
{
    HREassert(dm_nrows(a) == dm_nrows(b) && dm_ncols(a) == dm_ncols(b))

    bitvector_union(&a->bits, &b->bits);
}

int
dm_equals(const matrix_t* a, const matrix_t* b)
{
    HREassert(dm_nrows(a) == dm_nrows(b) && dm_ncols(a) == dm_ncols(b))

    return bitvector_equal(&a->bits, &b->bits);
}

void
dm_apply_xor(matrix_t* a, const matrix_t* b)
{
    HREassert(dm_nrows(a) == dm_nrows(b) && dm_ncols(a) == dm_ncols(b))

    return bitvector_xor(&a->bits, &b->bits);
}

int
dm_is_empty(const matrix_t* m)
{
    return bitvector_is_empty(&m->bits);
}

void
dm_prod(bitvector_t* tgt, const bitvector_t* bv, const matrix_t* m)
{
    HREassert(bitvector_size(tgt) == (size_t) dm_ncols(m) && bitvector_size(bv) == (size_t) dm_nrows(m));

    for (size_t i = 0; i < bitvector_size(bv); i++) {
        dm_row_union(tgt, m, i);
    }
}

void
dm_row_spans(const matrix_t* const m, int* const spans)
{
    for (int i = 0; i < dm_nrows(m); i++) {
        const int f = dm_first(m, i);
        const int l = dm_last(m, i);
        if (f == -1 || l == -1) spans[i] = 0;
        else spans[i] = l - f + 1;
    }
}

double
dm_event_span(const matrix_t* const m, int* const spans)
{
    int* s = spans;
    if (s == NULL) {
        s = RTmalloc(sizeof(int[dm_nrows(m)]));
        dm_row_spans(m, s);
    }

    double res = 0;
    for (int i = 0; i < dm_nrows(m); i++) {
        res += s[i];
    }

    if (spans == NULL) {
        RTfree(s);
    }

    return res;
}

double
dm_weighted_event_span(const matrix_t* const m, int* const spans)
{
    int* s = spans;
    if (s == NULL) {
        s = RTmalloc(sizeof(int[dm_nrows(m)]));
        dm_row_spans(m, s);
    }

    double res = 0;
    for (int i = 0; i < dm_nrows(m); i++) {
        res += ((double) (dm_ncols(m) - dm_first(m, i)) / (dm_ncols(m) / 2.0)) * (double) s[i];
    }

    if (spans == NULL) {
        RTfree(s);
    }

    return res;
}

/////////////////////////////////////////////////////////////////////
// FORCE: A Fast and Easy-To-Implement Variable-Ordering Heuristic //
/////////////////////////////////////////////////////////////////////

/*
 * FORCE formula 3.1: Compute Center Of Gravity for each hyperedge/row.
 */
static void
FORCE_COGs(matrix_t* m, double* cogs)
{
    memset(cogs, 0, sizeof(double[dm_nrows(m)]));
    for (int i = 0; i < dm_nrows(m); i++) {
        for (int j = 0; j < dm_ncols(m); j++) {
            if (dm_is_set(m, i, j)) cogs[i] += j;
        }
        cogs[i] /= dm_ones_in_row(m, i);
    }
}

/* tentative locations */
struct tent_s {
    double weight;
    int column;
};

/*
 * Formula 3.2: Compute all tentative locations/rows
 */
static void
FORCE_tents(matrix_t* m, double* cogs, struct tent_s* tents)
{
    memset(tents, 0, sizeof(struct tent_s[dm_ncols(m)]));
    for (int i = 0; i < dm_ncols(m); i++) {
        tents[i].column = i;
        for (int j = 0; j < dm_nrows(m); j++) {
            if (dm_is_set(m, j, i)) tents[i].weight += cogs[j];
        }
        tents[i].weight /= dm_ones_in_col(m, i);
    }
}

/*
 * Compare tentative locations
 */
static int
compare_tents(const void *a, const void *b)
{
    const struct tent_s* aa = (const struct tent_s*) a;
    const struct tent_s* bb = (const struct tent_s*) b;

    if (aa->weight - bb->weight < 0.0) return -1;
    else return 1;
}

/*
 * FORCE algorithm to reduce event span.
 */
void
dm_FORCE(matrix_t* m)
{
    // piece of memory for COGs
    double cogs[dm_nrows(m)];

    // piece of memory for tentative locations
    struct tent_s tents[dm_ncols(m)];

    double old_span = 0.0;
    double new_span = dm_nrows(m) * (double) dm_ncols(m);

    // Paper says c*log10(dm_ncols(m)); we pick dm_ncols(m)
    int iterations = dm_ncols(m);
    do {
        old_span = new_span;

        // compute COGSs
        FORCE_COGs(m, cogs);

        // compute tentative locations
        FORCE_tents(m, cogs, tents);

        // sort tentative locations
        qsort(tents, dm_ncols(m), sizeof(struct tent_s), compare_tents);

        // create permutation
        int perm[dm_ncols(m)];
        for (int i = 0; i < dm_ncols(m); i++) {
            perm[i] = tents[i].column;
        }

        // create permutation groups and permute columns
        permutation_group_t* groups;
        int n;
        dm_create_permutation_groups(&groups, &n, perm, dm_ncols(m));
        for (int i = 0; i < n; i++) {
            dm_permute_cols(m, &groups[i]);
            dm_free_permutation_group(&groups[i]);
        }
        RTfree(groups);

        // compute gains
        new_span = dm_event_span(m, NULL);
        iterations--;
    } while (iterations > 0 && old_span > new_span);
    Warning(infoLong, "FORCE took %d iterations", dm_ncols(m) - iterations);
}
