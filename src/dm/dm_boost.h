#ifndef DM_BOOST_H
#define DM_BOOST_H

#ifdef __cplusplus
extern "C" {
#endif

#include <dm/dm.h>

typedef enum {
    BOOST_CM,
    BOOST_SLOAN,
    BOOST_KING
} boost_reorder_t;

extern void boost_ordering(const matrix_t*, int* row_perm, int* col_perm, const boost_reorder_t, const int total);

#ifdef __cplusplus
}
#endif

#endif
