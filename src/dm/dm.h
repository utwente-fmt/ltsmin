#ifndef DM_H
#define DM_H

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifdef LTSMIN_CONFIG_INCLUDED
#include <dm/bitvector.h>
#else
#include <ltsmin/bitvector.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int becomes;
    int at;
    int group;
} header_entry_t;

typedef struct {
    int size;
    header_entry_t* data;
    // cache for count, min and max
    int* count;
    int* min;
    int* max;
} matrix_header_t;

extern void dm_create_header(matrix_header_t* p, const int size);
extern void dm_free_header(matrix_header_t* p);

typedef struct {
    int size;
    int data_size;
    int fixed_size;
    int* data;
} permutation_group_t;

extern void dm_create_permutation_group(permutation_group_t* o, const int size, int* const data);
extern void dm_free_permutation_group(permutation_group_t* o);
extern void dm_add_to_permutation_group(permutation_group_t* o, const int);
extern void dm_close_group(permutation_group_t* o);

extern void dm_create_permutation_groups(permutation_group_t** ps, int* num_groups, const int* p, const int size);

typedef struct matrix {
    int rows;
    int cols;
    int bits_per_row;
    matrix_header_t row_perm;
    matrix_header_t col_perm;
    bitvector_t bits;
} matrix_t;

extern void dm_copy_row_info(const matrix_t* src, matrix_t* tgt);
extern void dm_copy_col_info(const matrix_t* src, matrix_t* tgt);

typedef int (*dm_comparator_fn)(matrix_t* m, const int a, const int b);
typedef int (*dm_subsume_rows_fn)(matrix_t* m, const int a, const int b, void* context);
typedef int (*dm_nub_rows_fn)(matrix_t* m, const int a, const int b, void* context);
typedef int (*dm_subsume_cols_fn)(matrix_t* m, const int a, const int b, void* context);
typedef int (*dm_nub_cols_fn)(matrix_t* m, const int, const int, void* context);

extern void dm_create(matrix_t* m, const int rows, const int cols);
extern void dm_free(matrix_t* m);

extern int dm_nrows(const matrix_t* m);
extern int dm_ncols(const matrix_t* m);

extern void dm_set(matrix_t* m, const int row, const int col);
extern void dm_unset(matrix_t* m, const int row, const int col);
extern int dm_is_set(const matrix_t* m, const int row, const int col);

typedef void (*dm_permute_fn)(matrix_t* m, const permutation_group_t* o);
extern void dm_permute_rows(matrix_t* m, const permutation_group_t* o);
extern void dm_permute_cols(matrix_t* m, const permutation_group_t* o);

extern void dm_swap_rows(matrix_t* m, const int rowa, const int rowb);
extern void dm_swap_cols(matrix_t* m, const int rowa, const int rowb);

extern void dm_copy(const matrix_t* src, matrix_t* tgt);

extern void dm_make_sparse(matrix_t* m);
extern void dm_flatten(matrix_t* m);

extern void dm_sort_rows(matrix_t* m, const dm_comparator_fn cmp);
extern void dm_sort_cols(matrix_t* m, const dm_comparator_fn cmp);

extern void dm_nub_rows(matrix_t* m, const dm_nub_rows_fn fn, void* context);
extern void dm_nub_cols(matrix_t* m, const dm_nub_cols_fn fn, void* context);

extern void dm_subsume_rows(matrix_t* m, const dm_subsume_rows_fn fn, void* context);
extern void dm_subsume_cols(matrix_t* m, const dm_subsume_cols_fn fn, void* context);

extern void dm_ungroup_rows(matrix_t* m);
extern void dm_ungroup_cols(matrix_t* m);

extern void dm_print(FILE* f, const matrix_t* m);
extern void dm_print_combined(FILE* f, const matrix_t* r, const matrix_t* mayw, const matrix_t* mustw);

typedef enum { DM_EVENT_SPAN, DM_WEIGHTED_EVENT_SPAN } dm_cost_t;

extern void dm_anneal(matrix_t* m, dm_cost_t cost, const int timeout);
extern void dm_optimize(matrix_t* m);
extern void dm_all_perm(matrix_t* m);
extern void dm_FORCE(matrix_t* m);

extern void dm_horizontal_flip(matrix_t* m);
extern void dm_vertical_flip(matrix_t* m);

extern int dm_first(const matrix_t* const m, const int row);
extern int dm_last(const matrix_t* const m, const int row);

extern int dm_top(const matrix_t* const m, const int col);
extern int dm_bottom(const matrix_t* const m, const int col);

/**
 * return the matrix as index table per row/col
 * result[row/col] = pointer to struct (count, index0, .. , index_[count])
 */
extern int** dm_rows_to_idx_table(const matrix_t* m);
extern int** dm_cols_to_idx_table(const matrix_t* m);

typedef struct dm_row_iterator {
    matrix_t* m;
    int row;
    int col;
} dm_row_iterator_t;

typedef struct dm_col_iterator {
    matrix_t* m;
    int row;
    int col;
} dm_col_iterator_t;

extern void dm_create_col_iterator(dm_col_iterator_t* ix, matrix_t* m, int col);
extern void dm_create_row_iterator(dm_row_iterator_t* ix, matrix_t* m, int row);
extern int dm_col_next(dm_col_iterator_t* it);
extern int dm_row_next(dm_row_iterator_t* it);

extern int dm_ones_in_row(const matrix_t* m, const int row);
extern int dm_ones_in_col(const matrix_t* m, const int col);

extern int dm_project_vector(matrix_t* m, int row, int* src, int* dst);
extern int dm_expand_vector(matrix_t*, int row, int* s0, int* src, int* dst);

extern void dm_print_perm(const matrix_header_t* p);

extern void dm_clear(matrix_t* m);

extern void dm_fill(matrix_t* m);

/**
 * Applies logical or to each element of both matrices into tgt.
 */
extern void dm_apply_or(matrix_t* tgt, const matrix_t* src);

/**
 * Applies logical equivalence (i.e. a == b).
 *
 * return value:
 *  1: when equals
 *  0: when not equal
 */
extern int dm_equals(const matrix_t* a, const matrix_t* b);

/**
 * Applies exclusive or (xor) to each element of both matrices into tgt.
 */
extern void dm_apply_xor(matrix_t* tgt, const matrix_t* src);

/**
 * Returns whether every element in m is 0.
 *
 * return value:
 *  1: when empty
 *  0: otherwise
 */
extern int dm_is_empty(const matrix_t* m);

/**
 * Applies the product bv * m = tgt;
 */
extern void dm_prod(bitvector_t* tgt, const bitvector_t* bv, const matrix_t* m);

/**
 * dm_row/col_union
 *  Union of a row/column of the matrix to a bitvector
 *  (with the current permutation applied)
 *  1) bitvector of the size (rows/cols) of the matrix;
 *     it should be initialized by bitvector_create or bitvector_copy
 *  2) the matrix
 *  3) the row/column of the matrix
 */
extern void dm_row_union(bitvector_t* bv, const matrix_t* m, int row);
extern void dm_col_union(bitvector_t* bv, const matrix_t* m, int col);

extern void dm_row_spans(const matrix_t* const m, int* const spans);

extern double dm_event_span(const matrix_t* const m, int* const spans);
extern double dm_weighted_event_span(const matrix_t* const m, int* const spans);

/**
 * Returns whether header a and header b are equal.
 */
extern int dm_equal_header(const matrix_header_t* const a, const matrix_header_t* const b);

#ifdef __cplusplus
}
#endif
#endif                          // DM_H
