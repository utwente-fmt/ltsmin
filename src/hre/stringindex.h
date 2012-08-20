// -*- tab-width:4 ; indent-tabs-mode:nil -*-

/**
@file stringindex.h
@brief Indexed set of strings.
*/

#ifndef STRING_INDEX_H
#define STRING_INDEX_H

#include <util-lib/dynamic-array.h>

/// String index handle.
typedef struct stringindex *string_index_t;

/// Create an index.
extern string_index_t SIcreate();

/// Destroy an index.
extern void SIdestroy(string_index_t *si);

/// Constant returned if lookup fails.
#define SI_INDEX_FAILED (-1)

/// Find string if present.
extern int SIlookup(string_index_t si,const char*str);

/// Find chunk if present.
extern int SIlookupC(string_index_t si,const char*str,int len);

/// Insert if not present.
extern int SIput(string_index_t si,const char*str);

/// Insert if not present.
extern int SIputC(string_index_t si,const char*str,int len);

/// Insert if not present at a specific position.
extern void SIputAt(string_index_t si,const char*str,int pos);

/// Insert if not present at a specific position.
extern void SIputCAt(string_index_t si,const char*str,int len,int pos);

/// Delete the element str.
extern void SIdelete(string_index_t si,const char*str);

/// Delete the element str.
extern void SIdeleteC(string_index_t si,const char*str,int len);

/// Delete all elements.
extern void SIreset(string_index_t si);

/// Get the string.
extern char* SIget(string_index_t si,int i);

/// Get the string.
extern char* SIgetC(string_index_t si,int i,int*len);

/// Get the size of the defined range.
extern int SIgetRange(string_index_t si);

/// Get a count of the number of elements.
extern int SIgetCount(string_index_t si);

/// Get the underlying array manager for data elements.
extern array_manager_t SImanager(string_index_t si);
#endif

