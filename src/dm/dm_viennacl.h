#ifndef DM_VIENNACL_H
#define DM_VIENNACL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <dm/dm.h>

typedef enum {
    VIENNACL_CM,
    VIENNACL_ACM,
    VIENNACL_GPS,
    VIENNACL_NONE
} viennacl_reorder_t;

extern void viennacl_reorder(const matrix_t*, int* row_perm, int* col_perm, viennacl_reorder_t, const int total, const int metrics);

#ifdef __cplusplus
}
#endif

#endif
