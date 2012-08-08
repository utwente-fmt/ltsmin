#ifndef POLICY_H
#define POLICY_H

/**
\file string-map.h
\brief Create and call string to string maps.
*/

/**
\brief Handle for a string to string mapping.
 */
typedef struct string_string_map *string_map_t;

/**
\brief Handle for a set of strings.
 */
typedef struct string_set *string_set_t;

/**
\brief Create a map that from a Shell Wild specification.
 */
extern string_map_t SSMcreateSWP(const char* swp_spec);

/**
\brief Find the mapped value for the given input.
*/
extern char* SSMcall(string_map_t map,const char*input);

/**
\brief Create a set from a Shell Wild specification.
 */
extern string_set_t SSMcreateSWPset(const char* swp_spec);

/**
\brief Test if the input is member of the set.
 */
extern int SSMmember(string_set_t set,const char*input);

#endif

