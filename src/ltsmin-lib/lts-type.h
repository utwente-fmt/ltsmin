// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#ifndef LTS_TYPE_H
#define LTS_TYPE_H

#include <stdio.h>

/**
\file lts-type.h
\brief This data type stores signatures of labeled transition systems.

- How long is the state vector?
- Which elements of the state vector are visible and what are their types?
- How many labels of which type are there on every edge?
- How many defined state labels are there?
- What are the types used in the LTS?
*/

/**
 * opaque type lts_type_t
 */
typedef struct lts_type_s *lts_type_t;

/**
 * enumeration of datatype representations
 */
typedef enum {
/**
A direct type to deal with any type that can be represented as a 32 bit integer.
*/
LTStypeDirect,

/**
A range type is used for an integer type from a (small) range of values.
*/
LTStypeRange,

/**
A chunk type is a type with a possibly infinite domain where each value is
serialized.

Because it is not guaranteed that all values can be enumerated, unused values
may be garbage collected and the order of the numbering may be changed.
*/
LTStypeChunk,

/**
An enumerated type is a finite chunk type.

It is not allowed to remove values and it is not allowed to change the
numbering.
*/
LTStypeEnum,

/**
The Boolean type.

Only two values allowed 0, and 1.
*/
LTStypeBool,

/**
The trilean type.

Only three values allowed 0, 1, and 2.
Where 0 and 1 are Boolean values and 2 indicates maybe.
*/
LTStypeTrilean,

/**
The signed 32 bit integer type.

Allows arithmetic operators.
*/
LTStypeSInt32,

} data_format_t;

#define DATA_FORMAT_SIZE LTStypeSInt32 - LTStypeDirect + 1

/// Return the string representation of the data format of \p typeno.
extern const char* data_format_string(lts_type_t  t,int typeno);

/// Return the string representation of \p format.
extern const char* data_format_string_generic(data_format_t format);

/// Create a new empty lts type.
extern lts_type_t lts_type_create();

/// Clone an lts type
extern lts_type_t lts_type_clone(lts_type_t);

/// Create a new lts type by permuting the state vector of an existing type.
extern lts_type_t lts_type_permute(lts_type_t t,int *pi);

/// Destroy an lts type.
extern void lts_type_destroy(lts_type_t *t);

/// Print the lts type to the output stream;
extern void lts_type_printf(void* l, lts_type_t t);

/**
 * Set the length of the state vector in the specified LTS structure
 * descriptor.
 * It is allowed to specify a LARGER state vector, for example to grow
 * the state vector incrementally.
 * It is allowed to specify a length of 0, which will remove any information
 * about the state vector from the LTS structure descriptor.
 * It is NOT allowed to shrink the state vector.
 * @param t The LTS structure descriptor
 * @param length The length of the state vector
 */
extern void lts_type_set_state_length(lts_type_t  t,int length);

/**
 * Get the length of the state vector of the specified LTS structure
 * descriptor.
 * @param t The LTS structure descriptor
 * @return The length of the state vector of the LTS structure descriptor.
 */
extern int lts_type_get_state_length(lts_type_t  t);

/**
 * Sets the name of the specified variable slot in the state vector to the
 * specified name.
 * @param t The LTS structure descriptor in question.
 * @param idx The index of the variable slot of which the name will be set.
 * @param name The new name of the specified variable slot.
 * @pre 0 <= idx < lts_type_get_state_length(t)
 */
extern void lts_type_set_state_name(lts_type_t  t,int idx,const char* name);

/// Get the name of a state slot.
extern char* lts_type_get_state_name(lts_type_t  t,int idx);

/// Set the type of a state slot by name.
extern void lts_type_set_state_type(lts_type_t  t,int idx,const char* name);

/// Get the type of a state slot.
extern char* lts_type_get_state_type(lts_type_t  t,int idx);

/// Set the type of a state slot by type number.
extern void lts_type_set_state_typeno(lts_type_t  t,int idx,int typeno);

/// Get the type number of a state slot.
extern int lts_type_get_state_typeno(lts_type_t  t,int idx);

extern void lts_type_set_state_label_count(lts_type_t  t,int count);
extern int lts_type_get_state_label_count(lts_type_t  t);
extern void lts_type_set_state_label_name(lts_type_t  t,int label,const char*name);
extern void lts_type_set_state_label_type(lts_type_t  t,int label,const char*name);
extern void lts_type_set_state_label_typeno(lts_type_t  t,int label,int typeno);
extern char* lts_type_get_state_label_name(lts_type_t  t,int label);
extern char* lts_type_get_state_label_type(lts_type_t  t,int label);
extern int lts_type_get_state_label_typeno(lts_type_t  t,int label);
extern int lts_type_find_state_label_prefix(lts_type_t  t, const char *prefix);
extern int lts_type_find_state_label(lts_type_t  t, const char *name);

extern void lts_type_set_edge_label_count(lts_type_t  t,int count);
extern int lts_type_get_edge_label_count(lts_type_t  t);
extern void lts_type_set_edge_label_name(lts_type_t  t,int label,const char*name);
extern void lts_type_set_edge_label_type(lts_type_t  t,int label,const char*name);
extern void lts_type_set_edge_label_typeno(lts_type_t  t,int label,int typeno);
extern char* lts_type_get_edge_label_name(lts_type_t  t,int label);
extern char* lts_type_get_edge_label_type(lts_type_t  t,int label);
extern int lts_type_get_edge_label_typeno(lts_type_t  t,int label);
extern int lts_type_find_edge_label_prefix(lts_type_t  t, const char *prefix);
extern int lts_type_find_edge_label(lts_type_t  t, const char *name) ;

/**
 * Get the number of data types used.
 */
extern int lts_type_get_type_count(lts_type_t  t);
/**
\deprecated Use lts_type_put_type instead.
\brief Add a new type with format LTStypeChunk.
*/
extern int lts_type_add_type(lts_type_t  t,const char *name,int* is_new);
/**
Get the string representation of a type.
*/
extern char* lts_type_get_type(lts_type_t  t,int typeno);
/**
 * Test if a type is defined in the give LTS type.
 */
extern int lts_type_has_type(lts_type_t  t,const char *name);
/**
 * Get the representation of a type.
 */
extern data_format_t lts_type_get_format(lts_type_t  t,int typeno);

/**
 * Set the representation of a type.
 */
extern void lts_type_set_format(lts_type_t  t,int typeno,data_format_t format);

/** Get the maximum of a range type.
 */
extern int lts_type_get_max(lts_type_t  t,int typeno);
/** Get the minimum of a range type.
 */
extern int lts_type_get_min(lts_type_t  t,int typeno);
/** Set the range of a range type.
 */
extern void lts_type_set_range(lts_type_t  t,int typeno,int min,int max);
/**
Add a type with a given representation to the LTS type.
*/
extern int lts_type_put_type(lts_type_t  t,const char *name,data_format_t format,int* is_new);
/**
Find a type with a certain name
*/
extern int lts_type_find_type_prefix(lts_type_t  t, const char *prefix);
/**
Find a type with a name that starts with prefix
*/
extern int lts_type_find_type(lts_type_t  t, const char *name);

/**
\brief Validate the given LTS type.

If an inconsistent type is found the program aborts.
*/
extern void lts_type_validate(lts_type_t t);

#endif

