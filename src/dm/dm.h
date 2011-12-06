#ifndef DM_H
#define DM_H

#include "bitvector.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int                 becomes;
    int                 at;
    int                 group;
} header_entry_t;

typedef struct {
    int                 size;
    header_entry_t     *data;
    int                *count;
} matrix_header_t;

extern int          dm_create_header (matrix_header_t *, int);
extern int          dm_copy_header (const matrix_header_t *, matrix_header_t *);
extern void         dm_free_header (matrix_header_t *);

typedef struct {
    int                 size;
    int                 data_size;
    int                 fixed_size;
    int                *data;
} permutation_group_t;

extern int          dm_create_permutation_group (permutation_group_t *,
                                                 int, int *);
extern void         dm_free_permutation_group (permutation_group_t *);
extern int          dm_add_to_permutation_group (permutation_group_t *,
                                                 int);
extern int          dm_close_group (permutation_group_t *);
extern int          dm_apply_permutation_group (matrix_header_t *,
                                                const permutation_group_t *);

typedef struct matrix {
    int                 rows;
    int                 cols;
    int                 bits_per_row;
    matrix_header_t     row_perm;
    matrix_header_t     col_perm;
    bitvector_t         bits;
} matrix_t;

typedef int         (*dm_comparator_fn) (matrix_t *, int, int);

extern int          dm_create (matrix_t *, const int, const int);
extern void         dm_free (matrix_t *);

extern int          dm_nrows (const matrix_t *);
extern int          dm_ncols (const matrix_t *);

extern void         dm_set (matrix_t *, int, int);
extern void         dm_unset (matrix_t *, int, int);
extern int          dm_is_set (const matrix_t *, int, int);

extern int          dm_permute_rows (matrix_t *, const permutation_group_t *);
extern int          dm_permute_cols (matrix_t *, const permutation_group_t *);

extern int          dm_swap_rows (matrix_t *, int, int);
extern int          dm_swap_cols (matrix_t *, int, int);

extern int          dm_copy (const matrix_t *, matrix_t *);

extern int          dm_make_sparse (matrix_t *);
extern int          dm_flatten (matrix_t *);

extern int          dm_sort_rows (matrix_t *, const dm_comparator_fn);
extern int          dm_sort_cols (matrix_t *, const dm_comparator_fn);

extern int          dm_nub_rows (matrix_t *);
extern int          dm_nub_cols (matrix_t *);

extern int          dm_subsume_rows (matrix_t *);
extern int          dm_subsume_cols (matrix_t *);

// NOTE: rewrite nub & subsume to dm_group_rows/cols(matrix_t *, dm_group_fn)?
extern int          dm_ungroup_rows (matrix_t *);
extern int          dm_ungroup_cols (matrix_t *);

extern int          dm_print (FILE *, const matrix_t *);

extern int          dm_anneal (matrix_t *);
extern int          dm_optimize (matrix_t *);
extern int          dm_all_perm (matrix_t *);

/**
 * return the matrix as index table per row/col
 * result[row/col] = pointer to struct (count, index0, .. , index_[count])
 */
extern int**        dm_rows_to_idx_table(const matrix_t*);
extern int**        dm_cols_to_idx_table(const matrix_t*);

typedef struct dm_row_iterator {
    matrix_t           *m;
    int                 row;
    int                 col;
} dm_row_iterator_t;

typedef struct dm_col_iterator {
    matrix_t           *m;
    int                 row;
    int                 col;
} dm_col_iterator_t;


extern int          dm_create_col_iterator (dm_col_iterator_t *,
                                            matrix_t *, int);
extern int          dm_create_row_iterator (dm_row_iterator_t *,
                                            matrix_t *, int);
extern int          dm_col_next (dm_col_iterator_t *);
extern int          dm_row_next (dm_row_iterator_t *);

extern int          dm_ones_in_row (matrix_t *, int row);
extern int          dm_ones_in_col (matrix_t *, int col);

extern int          dm_project_vector (matrix_t *, int row, int *src,
                                       int *dst);
extern int          dm_expand_vector (matrix_t *, int row, int *s0, int *src,
                                      int *dst);

extern void         dm_print_perm (const matrix_header_t *p);

/**
 * dm_bitvector_row/col
 *  Copy a row/column of the matrix to a bitvector
 *  (with the current permutation applied)
 *  1) bitvector of the size (rows/cols) of the matrix;
 *     it should be initialized by bitvector_create or bitvector_copy
 *  2) the matrix
 *  3) the row/column of the matrix
 *
 * return value:
 *  0: bitvector copied, no errors
 * -1: error
 */
extern int          dm_bitvector_row(bitvector_t *, const matrix_t *, int);
extern int          dm_bitvector_col(bitvector_t *, const matrix_t *, int);

#ifdef __cplusplus
}
#endif
#endif                          // DM_H
