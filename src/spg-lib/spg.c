/*
 * spg.c
 *
 *  Created on: 30 Oct 2012
 *      Author: kant
 */

#include <limits.h>
#if HAVE_PROFILER
#include <gperftools/profiler.h>
#endif

#include <hre/runtime.h>
#include <hre/user.h>
#include <spg-lib/spg.h>


/**
 * \brief Creates a new parity game.
 */
parity_game* spg_create(const vdom_t domain, int state_length, int num_groups, int min_priority, int max_priority)
{
    Print(infoLong, "spg_create state_length=%d, num_groups=%d, max_priority=%d", state_length, num_groups, max_priority);
    parity_game* result = (parity_game*)RTmalloc(sizeof(parity_game));
    result->domain = domain;
    result->state_length = state_length;
    result->src = (int*)RTmalloc(state_length * sizeof(int));
    result->v = vset_create(domain, -1, NULL, -1, NULL);
    for(int i=0; i<2; i++) {
        result->v_player[i] = vset_create(domain, -1, NULL, -1, NULL);
    }
    result->min_priority = min_priority;
    result->max_priority = max_priority;
    result->v_priority = (vset_t*)RTmalloc((max_priority+1) * sizeof(vset_t));
    result->v_priority_swapfile = RTmalloc((max_priority+1) * sizeof(tmp_file_t *));
    for(int i=min_priority; i<=max_priority; i++) {
        result->v_priority[i] = vset_create(domain, -1, NULL, -1, NULL);
    }
    result->num_groups = num_groups;
    result->e = (vrel_t*)RTmalloc(num_groups * sizeof(vrel_t));
    return result;
}


/**
 * Clears the parity game from memory.
 */
void spg_destroy(parity_game* g)
{
    RTfree(g->src);
    vset_destroy(g->v);
    for(int i=0; i<2; i++) {
        vset_destroy(g->v_player[i]);
    }
    RTfree(g->e);
    for(int i = g->min_priority; i<g->max_priority; i++) {
        vset_destroy(g->v_priority[i]);
    }
    RTfree(g->v_priority);
    RTfree(g->v_priority_swapfile);
    RTfree(g);
}


/**
 *
 */
void spg_save(FILE* f, parity_game* g)
{
    fprintf(f,"symbolic parity game\n");
    fprintf(f,"state_length=%d\n", g->state_length);
    fprintf(f,"num_groups=%d\n", g->num_groups);
    fprintf(f,"min_priority=%d\n", g->min_priority);
    fprintf(f,"max_priority=%d\n", g->max_priority);
    fprintf(f,"init=");
    for(int i=0; i<g->state_length; i++) {
        fprintf(f,((i<g->state_length-1)?"%d ":"%d"), g->src[i]);
    }
    fprintf(f,"\n");
    for(int i=0; i<g->num_groups; i++) {
        fprintf(f,"rel proj e[%d]\n", i);
        vrel_save_proj(f, g->e[i]);
    }
    for(int i=0; i<g->num_groups; i++) {
        fprintf(f,"rel e[%d]\n", i);
        vrel_save(f, g->e[i]);
    }
    fprintf(f,"set v\n");
    vset_save(f, g->v);
    for(int i=0; i<2; i++) {
        fprintf(f,"set v_player[%d]\n", i);
        vset_save(f, g->v_player[i]);
    }
    for(int i=g->min_priority; i<=g->max_priority; i++) {
        fprintf(f,"set v_priority[%d]\n", i);
        vset_save(f, g->v_priority[i]);
    }
}


parity_game* spg_load(FILE* f, vset_implementation_t impl)
{
    int state_length = 0;
    int num_groups = 0;
    int min_priority = 0;
    int max_priority = 0;
    int res = fscanf(f,"symbolic parity game\n");
    //if (res <= 0) {
    //    Abort("Wrong file format: %d.", res)
    //}
    res &= fscanf(f,"state_length=%d\n", &state_length);
    res &= fscanf(f,"num_groups=%d\n", &num_groups);
    res &= fscanf(f,"min_priority=%d\n", &min_priority);
    res &= fscanf(f,"max_priority=%d\n", &max_priority);
    Print(infoShort, "Loading symbolic parity game. "
            "state_length=%d, num_groups=%d, min_priority=%d, max_priority=%d",
            state_length, num_groups, min_priority, max_priority);
    vdom_t domain = vdom_create_domain(state_length, impl);
    parity_game* result = spg_create(domain, state_length, num_groups, min_priority, max_priority);
    res &= fscanf(f,"init=");
    for(int i=0; i<state_length; i++) {
        res &= fscanf(f,((i<state_length-1)?"%d ":"%d"), &(result->src[i]));
    }
    res &= fscanf(f,"\n");
    int val;
    for(int i=0; i<num_groups; i++) {
        res &= fscanf(f,"rel proj e[%d]\n", &val);
        //Print(infoLong, "Reading e[%d] proj.", i);
        result->e[i] = vrel_load_proj(f, domain);
    }
    for(int i=0; i<num_groups; i++) {
        res &= fscanf(f,"rel e[%d]\n", &val);
        //Print(infoLong, "Reading e[%d].", i);
        vrel_load(f, result->e[i]);
    }
    res &= fscanf(f,"set v\n");
    //Print(infoLong, "Reading v.");
    vset_t v = vset_load(f, domain);
    vset_copy(result->v, v);
    vset_destroy(v);
    for(int i=0; i<2; i++) {
        res &= fscanf(f,"set v_player[%d]\n", &val);
        //Print(infoLong, "Reading v_player[%d].", i);
        v = vset_load(f, domain);
        vset_copy(result->v_player[i], v);
        vset_destroy(v);
    }
    for(int i=min_priority; i<=max_priority; i++) {
        res &= fscanf(f,"set v_priority[%d]\n", &val);
        //Print(infoLong, "Reading v_priority[%d].", i);
        v = vset_load(f, domain);
        vset_copy(result->v_priority[i], v);
        vset_destroy(v);
    }
    Print(infoShort, "Done loading symbolic parity game.");
    return result;
}


/**
 * \brief Creates a deep copy of g.
 */
parity_game* spg_copy(const parity_game* g)
{
    parity_game* result = spg_create(g->domain, g->state_length, g->num_groups, g->min_priority, g->max_priority);
    for(int i=0; i<g->state_length; i++) {
        result->src[i] = g->src[i];
    }
    vset_copy(result->v, g->v);
    for(int i=0; i<2; i++) {
        vset_copy(result->v_player[i], g->v_player[i]);
    }
    for(int i=g->min_priority; i<=g->max_priority; i++) {
        vset_copy(result->v_priority[i], g->v_priority[i]);
    }
    for(int i=0; i<g->num_groups; i++) {
        result->e[i] = g->e[i];
    }
    return result;
}
