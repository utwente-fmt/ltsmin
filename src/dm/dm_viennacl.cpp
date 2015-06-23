#include <hre/config.h>

#include <hre/user.h>
#include <dm/dm.h>
#include <dm/dm_viennacl.h>
#include <ltsmin-lib/ltsmin-standard.h>

#include <map>
#include <vector>

#include <viennacl/misc/bandwidth_reduction.hpp>

void
viennacl_reorder(const matrix_t* m, int* row_perm, int* col_perm, viennacl_reorder_t alg, const int total)
{
    /* We first create a new matrix required by ViennaCL.
     * This matrix represents a directed adjacency graph
     * We thus we need to set two non-zeros, for every
     * two adjacent vertices. */

    int size = dm_nrows(m) + dm_ncols(m);

    if (total) {
        /* Compute number of extra vertices.
         * This will be the number of non-zeros in the matrix. */
        for (int i = 0; i < dm_nrows(m); i++) {
            size += dm_ones_in_row((matrix_t*)m, i);
        }
    }

    std::vector< std::map<int, double> > matrix(size);

    Warning(infoLong, "Creating bipartite graph: G");
    for (int i = 0; i < dm_nrows(m); i++) {
        for (int j = 0; j < dm_ncols(m); j++) {
            if (dm_is_set(m, i, j)) {
                matrix[i][dm_nrows(m) + j] = 1.0;
                matrix[dm_nrows(m) + j][i] = 1.0;
            }
        }
    }

    if (total) {
        Warning(infoLong, "Creating total graph of G: T(G)");

        // add edges for every adjacent vertex in G
        int offset = dm_nrows(m) + dm_ncols(m);
        for (int i = 0; i < dm_nrows(m); i++) {
            for (int j = 0; j < dm_ncols(m); j++) {
                if (dm_is_set(m, i, j)) {
                    matrix[i][dm_nrows(m) + j] = 1.0;
                    matrix[dm_nrows(m) + j][i] = 1.0;

                    matrix[dm_nrows(m) + j][offset] = 1.0;
                    matrix[offset][dm_nrows(m) + j] = 1.0;
                    offset++;
                }
            }
        }

        // add the edges for incident edges in G, by iterating over rows.
        offset = dm_nrows(m) + dm_ncols(m);
        for (int i = 0; i <  dm_nrows(m); i++) {
            for (int j = 0, k = -1; j < dm_ncols(m); j++) {
                if (dm_is_set(m, i, j)) {
                    if (k > -1) {
                        matrix[k][offset] = 1.0;
                        matrix[offset][k] = 1.0;
                    }
                    k = offset;
                    offset++;
                }
            }
        }

        /* Now we need to add edges by iterating over columns.
         * But this is a bit harder, because we don't know the
         * vertex numbers on-the-fly. So we precompute them
         * and put them in a map. */
        std::map<int, int> offsets;
        for (int i = 0, c = 0; i < dm_nrows(m); i++) {
            for (int j = 0; j < dm_ncols(m); j++) {
                if (dm_is_set(m, i, j)) {
                    offsets[i * dm_ncols(m) + j] = c++ + dm_nrows(m) + dm_ncols(m);
                }
            }
        }

        // add the edges for incident edges in G, by iterating over columns.
        for (int i = 0; i < dm_ncols(m); i++) {
            for (int j = 0, k = -1; j < dm_nrows(m); j++) {
                if (dm_is_set(m, j, i)) {
                    if (k > -1) {
                        matrix[offsets[k * dm_ncols(m) + i]][offsets[j * dm_ncols(m) + i]] = 1.0;
                        matrix[offsets[j * dm_ncols(m) + i]][offsets[k * dm_ncols(m) + i]] = 1.0;
                    }
                    k = j;
                }
            }
        }
    }

    std::vector<int> p;
    switch(alg) {
        case VIENNACL_CM:
            Warning(infoLong, "Computing Cuthill McKee ordering");
            p = viennacl::reorder(matrix, viennacl::cuthill_mckee_tag());
            break;
        case VIENNACL_ACM:
            Warning(infoLong, "Computing advanced Cuthill McKee ordering");
            p = viennacl::reorder(matrix, viennacl::advanced_cuthill_mckee_tag());
            break;
        case VIENNACL_GPS:
            Warning(infoLong, "Computing Gibbs Poole Stockmeyer ordering");
            p = viennacl::reorder(matrix, viennacl::gibbs_poole_stockmeyer_tag());
            break;
        default:
            Warning(error, "Unsupported ViennaCL ordering");
            HREexit(LTSMIN_EXIT_FAILURE);
    }

    Warning(infoLong, "Parsing ordering");

    std::vector<int> ip(p.size());
    for (int i = 0; i < (int) p.size(); i++) {
        ip[p[i]] = i;
    }

    int r = 0, c = 0;
    for(std::vector<int>::iterator i = ip.begin(); i != ip.end(); ++i) {
        if (*i < dm_nrows(m) + dm_ncols(m)) {
            if (*i < dm_nrows(m)) row_perm[r++] = *i;
            else col_perm[c++] = *i - dm_nrows(m);
        }
    }
}
