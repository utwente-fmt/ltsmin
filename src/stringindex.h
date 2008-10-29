/**
@file stringindex.h
@brief Indexed set of strings.
*/

#ifndef STRING_INDEX_H
#define STRING_INDEX_H

#include "config.h"

/// String index handle.
typedef struct stringindex *string_index_t;

/// Create an index.
extern string_index_t SIcreate();

/// Destroy an index.
extern void SIdestroy(string_index_t *si);

/// Constant returned if lookup fails.
#define SI_INDEX_FAILED (-1)

/// Find if present.
extern int SIlookup(string_index_t si,const char*str);

/// Insert if not present.
extern int SIput(string_index_t si,const char*str);

/// Insert if not present at a specific position.
extern void SIputAt(string_index_t si,const char*str,int pos);

/// Delete the element str.
extern void SIdelete(string_index_t si,const char*str);

/// Delete all elements.
extern void SIreset(string_index_t si);

/// Get the string.
extern char* SIget(string_index_t si,int i);

/// Get the size of the defined range.
extern int SIgetSize(string_index_t si);

#endif

