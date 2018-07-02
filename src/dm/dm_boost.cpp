#include <hre/config.h>

#include <hre/user.h>
#include <dm/dm.h>
#include <dm/dm_boost.h>
#include <ltsmin-lib/ltsmin-standard.h>

#include <popt.h>

#include <vector>
#include <iostream>

#include <boost/config.hpp>
#include <boost/graph/graph_traits.hpp>
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/cuthill_mckee_ordering.hpp>
#include <boost/graph/properties.hpp>
#include <boost/graph/bandwidth.hpp>
#include <boost/graph/profile.hpp>
#include <boost/graph/wavefront.hpp>
#include <boost/graph/sloan_ordering.hpp>
#include <boost/graph/minimum_degree_ordering.hpp>
#include <boost/graph/king_ordering.hpp>

static int sloan_w1 = 1;
static int sloan_w2 = 16;

struct poptOption boost_options[] = {
    { "sloan-w1", 0, POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &sloan_w1, 0, "use <W1> as weight 1 for the Sloan algorithm", "<W1>"},
    { "sloan-w2", 0, POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &sloan_w2, 0, "use <W2> as weight 2 for the Sloan algorithm", "<W2>"},
    POPT_TABLEEND
};

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
        Warning(lerror, "Got invalid permutation from boost. "
                "This may happen when the graph representation of the matrix "
                "has disconnected sub graphs. This needs to be fixed in boost. "
                "A bug report has been filed here: "
                "https://svn.boost.org/trac/boost/ticket/11355.");
        HREexit(LTSMIN_EXIT_FAILURE);
    }
}

template<typename Graph, typename VertexIndexMap>
static typename graph_traits<Graph>::vertices_size_type
span(Graph g, VertexIndexMap index)
{
    typedef typename graph_traits<Graph>::vertices_size_type vertices_size_type;
    vertices_size_type span = 0;
    typename graph_traits<Graph>::vertex_iterator i, end;
    for (boost::tie(i, end) = vertices(g); i != end; ++i){
        if (out_degree(*i, g) == 0) continue;
        int min = num_vertices(g);
        int max = 0;
        typename graph_traits<Graph>::out_edge_iterator e, end;
        for (boost::tie(e, end) = out_edges(*i, g); e != end; ++e) {
            int i = get(index, target(*e, g));
            if (i < min) min = i;
            if (i > max) max = i;
        }
        span += ((max - min) + 1);
    }
    return span;
}

template<typename Vertex, typename Graph>
static void
graph_stats(std::vector<Vertex> inv_perm, Graph g, typename property_map<Graph, vertex_index_t>::type index_map, const int metrics)
{
    if (log_active(infoLong) || metrics) {

        log_t l = infoLong;
        if (metrics) l = info;

        Warning(infoLong, "Computing graph stats");
        std::vector<int> perm(num_vertices(g));

        for (unsigned int c = 0; c != num_vertices(g); ++c) {
            perm[index_map[inv_perm[c]]] = c;
        }

        Warning(l, "bandwidth: %zu", bandwidth(g, make_iterator_property_map(&perm[0], index_map, perm[0])));
        Warning(l, "profile: %zu", profile(g, make_iterator_property_map(&perm[0], index_map, perm[0])));
        Warning(l, "span: %zu", span(g, make_iterator_property_map(&perm[0], index_map, perm[0])));
        Warning(l, "average wavefront: %.*g", (int) std::ceil(std::log10(num_vertices(g) * num_vertices(g))), aver_wavefront(g, make_iterator_property_map(&perm[0], index_map, perm[0])));
        Warning(l, "RMS wavefront: %.*g", (int) std::ceil(std::log10(num_vertices(g) * num_vertices(g))), rms_wavefront(g, make_iterator_property_map(&perm[0], index_map, perm[0])));
    }
}

void
boost_ordering(const matrix_t* m, int* row_perm, int* col_perm, const boost_reorder_t alg, const int total, const int metrics)
{
    switch(alg) {
        case BOOST_NONE: {
            typedef adjacency_list<vecS, vecS, undirectedS,
                property<vertex_color_t, default_color_type,
                    property<vertex_degree_t,int> > > Graph;

            typedef graph_traits<Graph>::vertex_descriptor Vertex;

            Graph g = create_graph<Graph>(m, total);

            property_map<Graph, vertex_index_t>::type index_map = get(vertex_index, g);

            std::vector<Vertex> inv_perm(num_vertices(g));

            for (unsigned int i = 0; i < num_vertices(g); i++) inv_perm[i] = i;

            graph_stats(inv_perm, g, index_map, metrics);
            parse_ordering<Vertex, Graph>(inv_perm, index_map, row_perm, col_perm, m);
        } break;
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
            graph_stats(inv_perm, g, index_map, metrics);

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
            sloan_ordering(g, inv_perm.begin(), get(vertex_color, g), make_degree_map(g), get(vertex_priority, g), sloan_w1, sloan_w2);
            graph_stats(inv_perm, g, index_map, metrics);

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
            graph_stats(inv_perm, g, index_map, metrics);

            parse_ordering<Vertex, Graph>(inv_perm, index_map, row_perm, col_perm, m);
        } break;
        default: {
            Warning(lerror, "Unsupported Boost ordering");
            HREexit(LTSMIN_EXIT_FAILURE);
        }
    }
}
