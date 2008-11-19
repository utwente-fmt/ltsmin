#ifndef AT_MAP_H
#define AT_MAP_H

/**
\file at-map.h
\brief Create globally unique mappings from ATerms to integers and back using chunk tables.
*/

#include "aterm1.h"
#include "greybox.h"

/// Handle to a map.
typedef struct at_map_s *at_map_t;

/** \brief Create a named map.
 *
 * Suggested names for maps are action for the action labels and leaf for the terms.
 */
extern at_map_t ATmapCreate(model_t model,int type_no);

/// Translate a term to in integer.
extern int ATfindIndex(at_map_t map,ATerm t);

/// Translate an integer to a term.
extern ATerm ATfindTerm(at_map_t map,int idx);

#endif

