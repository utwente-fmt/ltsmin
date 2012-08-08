#ifndef AT_MAP_H
#define AT_MAP_H

/**
\file at-map.h
\brief Create globally unique mappings from ATerms to integers and back using chunk tables.
*/

#include <aterm1.h>

#include <pins-lib/pins.h>

// / Handle to a map.
typedef struct at_map_s *at_map_t;

// / Pretty printing function as a parameter for the map.
typedef char *(*pretty_print_t) (void *context, ATerm t);

// / Parsing function as a parameter for the map.
typedef ATerm (*parse_t) (void *context, char *str);

/** \brief Create a map for a type in a grey box model.
 *
\param print A function that prints ATerms. The default is ATwriteToString, but if the language
module uses an internal encoding, a special print function from the encoding to the official
form is needed to avoid having to translate to/from internal form every time.
\param parse A parser that matches the print function. The default is ATreadFromString.
 */
extern at_map_t ATmapCreate (model_t model, int type_no, void *context,
                             pretty_print_t print, parse_t parse);

// / Translate a term to in integer.
extern int      ATfindIndex (at_map_t map, ATerm t);

// / Translate an integer to a term.
extern ATerm    ATfindTerm (at_map_t map, int idx);

#endif

