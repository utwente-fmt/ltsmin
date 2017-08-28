#include <hre/config.h>

#include <hre/user.h>
#include <dm/dm.h>
#include <dm/dm_viennacl.h>
#include <ltsmin-lib/ltsmin-standard.h>

#include <map>
#include <vector>

#include <float.h>

#include <viennacl/misc/bandwidth_reduction.hpp>

static std::vector<std::map<int, double> >
reorder_matrix(std::vector<std::map<int, double> > const & matrix, std::vector<int> const & r)
{
    std::vector < std::map<int, double> > matrix2(r.size());
    for (std::size_t i = 0; i < r.size(); i++)
        for (std::map<int, double>::const_iterator it = matrix[i].begin(); it != matrix[i].end(); it++)
            matrix2[static_cast<std::size_t>(r[i])][r[static_cast<std::size_t>(it->first)]] = it->second;
    return matrix2;
}

static void
bandwidth(std::vector<std::map<int, double> > const & matrix, int & max, double & tot)
{
    max = 0;
    tot = 0;
    for (int i = 0; i < (int) matrix.size(); i++) {
        if (matrix[i].size() == 0) continue;
        int d = 0;
        for (std::map<int, double>::const_iterator it = matrix[i].begin(); it != matrix[i].end(); it++) {
            const int col_idx = it->first;
            d = std::max(d, abs(i - col_idx));
        }
        max = std::max(max, d);
        tot += d + 1;
    }
}

static double
span(std::vector<std::map<int, double> > const & matrix)
{
    double span = 0;
    for (std::size_t i = 0; i < matrix.size(); i++) {
        if (matrix[i].size() == 0) continue;
        int min_index = static_cast<int>(matrix.size());
        int max_index = 0;
        for (std::map<int, double>::const_iterator it = matrix[i].begin(); it != matrix[i].end(); it++) {
            if (it->first > max_index) max_index = it->first;
            if (it->first < min_index) min_index = it->first;
        }
        span += ((max_index - min_index) + 1);
    }

    return span;
}

static void
wavefront(std::vector<std::map<int, double> > const & matrix, double & avg, double & rms)
{
    avg = 0;
    rms = 0;

    for (std::size_t i = 0; i < matrix.size(); i++) {
        std::vector<bool> rows_active(matrix.size(), false);
        rows_active[i] = true;
        int wavefront = 1;
        for (std::size_t j = 0; j <= i; j++) {
            for (std::map<int, double>::const_iterator it = matrix[j].begin(); it != matrix[j].end(); it++) {
                std::size_t col_idx = static_cast<std::size_t>(it->first);
                if (col_idx >= i && !rows_active[col_idx]) {
                    rows_active[col_idx] = true;
                    wavefront++;
                }
            }
        }
        avg += wavefront;
        rms += (wavefront * (double) wavefront);
    }

    avg /= matrix.size();
    rms /= matrix.size();
    rms = std::sqrt(rms);
}

template<typename IndexT>
static void
print_matrix_stats(std::vector<std::map<int, double> > const & matrix, std::vector<IndexT> const & r, const int metrics)
{
    if (log_active(infoLong) || metrics) {
        log_t l = infoLong;
        if (metrics) l = info;

        Warning(infoLong, "Computing graph stats");
        int bw;
        double profile;
        double spn;
        double avg_wavefront;
        double rms_wavefront;
        std::vector < std::map<int, double> > m2 = reorder_matrix(matrix, r);

        bandwidth(m2, bw, profile);
        spn = span(m2);
        wavefront(m2, avg_wavefront, rms_wavefront);

        Warning(l, "bandwidth: %d", bw);
        Warning(l, "profile: %.*g", DBL_DIG, profile);
        Warning(l, "span: %.*g", DBL_DIG, spn);
        Warning(l, "average wavefront: %.*g", (int) std::ceil(std::log10(matrix.size() * matrix.size())), avg_wavefront);
        Warning(l, "RMS wavefront: %.*g", (int) std::ceil(std::log10(matrix.size() * matrix.size())), rms_wavefront);
    }
}

void
viennacl_reorder(const matrix_t* m, int* row_perm, int* col_perm, viennacl_reorder_t alg, const int total, const int metrics)
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
            size += dm_ones_in_row((matrix_t*) m, i);
        }
    }

    std::vector < std::map<int, double> > matrix(size);

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
        for (int i = 0; i < dm_nrows(m); i++) {
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
    switch (alg) {
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
    case VIENNACL_NONE:
        p = std::vector<int>(matrix.size());
        for (unsigned int i = 0; i < matrix.size(); i++) {
            p[i] = i;
        }
        break;
    default:
        Warning(error, "Unsupported ViennaCL ordering");
        HREexit(LTSMIN_EXIT_FAILURE);
    }

    print_matrix_stats(matrix, p, metrics);

    Warning(infoLong, "Parsing ordering");

    std::vector<int> ip(p.size());
    for (int i = 0; i < (int) p.size(); i++) {
        if (p[i] >= size) Abort("Got invalid permutation from ViennaCL"
            "This may happen when the graph representation of the matrix "
            "has disconnected sub graphs. This needs to be fixed in ViennaCL.");
        ip[p[i]] = i;
    }

    int r = 0, c = 0;
    for (std::vector<int>::iterator i = ip.begin(); i != ip.end(); ++i) {
        if (*i < dm_nrows(m) + dm_ncols(m)) {
            if (*i < dm_nrows(m)) row_perm[r++] = *i;
            else col_perm[c++] = *i - dm_nrows(m);
        }
    }
}
