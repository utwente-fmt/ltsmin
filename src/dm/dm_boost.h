#ifndef DM_BOOST_H
#define DM_BOOST_H

#ifdef __cplusplus
extern "C" {
#endif

#include <popt.h>

#include <dm/dm.h>

extern struct poptOption boost_options[];

typedef enum {
    BOOST_CM,
    BOOST_SLOAN,
    BOOST_KING,
    BOOST_NONE
} boost_reorder_t;

extern void boost_ordering(const matrix_t*, int* row_perm, int* col_perm, const boost_reorder_t, const int total, const int metrics);

#ifdef __cplusplus
}
#endif

#endif
