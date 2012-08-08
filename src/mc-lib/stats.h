#ifndef STATS_H
#define STATS_H

/**
\struct general struct for maintaining statistics on storage
*/
typedef struct stats_s {
    size_t              elts;
    size_t              nodes;
    size_t              misses;
    size_t              tests;
    size_t              rehashes;
} stats_t;

#endif
