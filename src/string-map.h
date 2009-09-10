#ifndef POLICY_H
#define POLICY_H

/**
\file string-map.h
\brief Create and call string to string maps.
*/

typedef struct string_string_map *string_map_t;

/**
\brief Create a map that from Shell Wild.
 */
extern string_map_t SSMcreateSWP(const char* swp_spec);

/**
\brief Find a match in the Pattern Matching Policy.
*/
extern char* SSMcall(string_map_t map,const char*input);

#endif

