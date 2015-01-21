#ifndef CHUNK_SUPPORT_H
#define CHUNK_SUPPORT_H

#include<stdint.h>
#include<string.h>

/**
\file chunk_support.h
\defgroup chunk_support Chunk Support

A chunk is a pair of a length and a pointer to a piece of memory of at least that size.
A packed chunk is a length followed by the data.
*/
//@{

/**
Define a type for chunk lengths.
*/
typedef uint32_t chunk_len;

/** Chunk as a length,pointer structure.
 */
typedef struct {
	chunk_len len;
	char *data;
} chunk;

/** Chunk as a length,data packed structure.
*/
typedef struct {
	chunk_len len;
	char data[];
} pchunk;

/**
Convert a standard C string to a chunk.
*/
#define chunk_str(s) ((chunk){(chunk_len)strlen(s),((char*)s)})

/**
Wrap a length and a pointer as a chunk.
*/
#define chunk_ld(l,d) ((chunk){l,d})

/**
\brief Copy the given binary source chunk and encode it as a string chunk.

Any printable, non-escape character is copied.
The escape caracter is encoded as two escape characters.
Any non-printable character is encoded as the escape character followed by the 
character in hex. (E.g. with escape ' (char)0 becomes '00).
*/
extern void chunk_encode_copy(chunk dst,chunk src,char escape);

/**
\brief Copy the given string chunk and decode it.

This function shortens the destination chunk if necessary.
*/
extern void chunk_decode_copy(chunk dst,chunk src,char escape);

/**
\brief Copy the chunk to a string.
If all characters are printable and non-white space, the characters are copied verbatim.
If all characters are printable, but there is white space then a quoted form is used.
Otherwise, the results is \#hex ... hex\#. The empty string is "".
*/
extern void chunk2string(chunk src,size_t  dst_size,char*dst);

/**
\brief Decode a string to a chunk.
*/
extern void string2chunk(char*src,chunk *dst);

//@}

#endif


