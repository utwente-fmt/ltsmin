#include <hre/config.h>

#include <hre/user.h>
#include <dm/dm.h>
#include <dm/dm_boost.h>
#include <ltsmin-lib/ltsmin-standard.h>

#include <vector>
#include <iostream>

#include <boost/config.hpp>
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/cuthill_mckee_ordering.hpp>
#include <boost/graph/properties.hpp>
#include <boost/graph/bandwidth.hpp>
#include <boost/graph/profile.hpp>
#include <boost/graph/wavefront.hpp>
#include <boost/graph/sloan_ordering.hpp>
#include <boost/graph/minimum_degree_ordering.hpp>
#include <boost/graph/king_ordering.hpp>

using namespace boost;
using namespace std;

template<typename DMGraph>
static DMGraph
create_graph(const matrix_t* m, const int total)
{
    Warning(infoLong, "Creating bipartite graph: G");

    // size of the bipartite graph of m
    int size = dm_nrows(m) + dm_ncols(m);


    if (total) {
        /* Compute number of extra vertices.
         * This will be the number of non-zeros in the matrix. */
        for (int i = 0; i < dm_nrows(m); i++) {
            size += dm_ones_in_row((matrix_t*)m, i);
        }
    }

    // create the adjacency graph
    DMGraph g = DMGraph(size);

    // add edges for the bipartite graph
    for (int i = 0; i < dm_nrows(m); i++) {
        for (int j = 0; j < dm_ncols(m); j++) {
            if (dm_is_set(m, i, j)) add_edge(i, dm_nrows(m) + j, g);
        }
    }

    if (total) {
        Warning(infoLong, "Creating total graph of G: T(G)");

        // add edges for every adjacent vertex in G
        int offset = dm_nrows(m) + dm_ncols(m);
        for (int i = 0; i < dm_nrows(m); i++) {
            for (int j = 0; j < dm_ncols(m); j++) {
                if (dm_is_set(m, i, j)) {
                    add_edge(i, offset, g);
                    add_edge(dm_nrows(m) + j, offset, g);
                    offset++;
                }
            }
        }

        // add the edges for incident edges in G, by iterating over rows.
        offset = dm_nrows(m) + dm_ncols(m);
        for (int i = 0; i <  dm_nrows(m); i++) {
            for (int j = 0, k = -1; j < dm_ncols(m); j++) {
                if (dm_is_set(m, i, j)) {
                    if (k > -1) add_edge(k, offset, g);
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
                    if (k > -1) add_edge(offsets[k * dm_ncols(m) + i], offsets[j * dm_ncols(m) + i], g);
                    k = j;
                }
            }
        }
    }

    return g;
}

template<typename Vertex, typename Graph>
static void
parse_ordering(std::vector<Vertex> inv_perm, typename property_map<Graph, vertex_index_t>::type index_map,
               int* row_perm, int* col_perm, const matrix_t* m)
{
    Warning(infoLong, "Parsing ordering");
    int r = 0, c = 0;
    for (typename std::vector<Vertex>::const_iterator i=inv_perm.begin(); i != inv_perm.end(); ++i) {
        if (index_map[*i] < (unsigned int) dm_nrows(m) + dm_ncols(m)) {
            if (index_map[*i] < (unsigned int) dm_nrows(m)) row_perm[r++] = index_map[*i];
            else col_perm[c++] = index_map[*i] - dm_nrows(m);
        }
    }
    if (r != dm_nrows(m) || c != dm_ncols(m)) {
        Warning(error, "Got invalid permutation from boost. "
                "This may happen when the graph representation of the matrix "
                "has disconnected sub graphs. This needs to be fixed in boost. "
                "A bug report has been filed here: "
                "https://svn.boost.org/trac/boost/ticket/11355.");
        HREexit(LTSMIN_EXIT_FAILURE);
    }
}

void
boost_ordering(const matrix_t* m, int* row_perm, int* col_perm, const boost_reorder_t alg, const int total)
{
    switch(alg) {
        case BOOST_CM: {
            typedef adjacency_list<vecS, vecS, undirectedS,
                property<vertex_color_t, default_color_type,
                    property<vertex_degree_t,int> > > Graph;

            typedef graph_traits<Graph>::vertex_descriptor Vertex;

            Graph g = create_graph<Graph>(m, total);

            property_map<Graph, vertex_index_t>::type index_map = get(vertex_index, g);

            std::vector<Vertex> inv_perm(num_vertices(g));

            Warning(infoLong, "Computing Cuthill McKee ordering");
            cuthill_mckee_ordering(g, inv_perm.rbegin(), get(vertex_color, g), make_degree_map(g));

            parse_ordering<Vertex, Graph>(inv_perm, index_map, row_perm, col_perm, m);
        } break;
        case BOOST_SLOAN: {
            typedef adjacency_list<setS, vecS, undirectedS,
                    property<vertex_color_t, default_color_type,
                        property<vertex_degree_t, int,
                            property< vertex_priority_t, double > > > > Graph;

            typedef graph_traits<Graph>::vertex_descriptor Vertex;

            Graph g = create_graph<Graph>(m, total);

            property_map<Graph, vertex_index_t>::type index_map = get(vertex_index, g);

            std::vector<Vertex> inv_perm(num_vertices(g));

            Warning(infoLong, "Computing Sloan ordering");
            sloan_ordering(g, inv_perm.begin(), get(vertex_color, g), make_degree_map(g), get(vertex_priority, g));

            parse_ordering<Vertex, Graph>(inv_perm, index_map, row_perm, col_perm, m);
        } break;
        case BOOST_KING: {
            typedef adjacency_list<vecS, vecS, undirectedS,
                property<vertex_color_t, default_color_type,
                    property<vertex_degree_t,int> > > Graph;

            typedef graph_traits<Graph>::vertex_descriptor Vertex;

            Graph g = create_graph<Graph>(m, total);

            property_map<Graph, vertex_index_t>::type index_map = get(vertex_index, g);

            std::vector<Vertex> inv_perm(num_vertices(g));

            Warning(infoLong, "Computing King ordering");
            king_ordering(g, inv_perm.rbegin(), get(vertex_color, g), make_degree_map(g), index_map);

            parse_ordering<Vertex, Graph>(inv_perm, index_map, row_perm, col_perm, m);
        } break;
        default: {
            Warning(error, "Unsupported Boost ordering");
            HREexit(LTSMIN_EXIT_FAILURE);
        }
    }
}
