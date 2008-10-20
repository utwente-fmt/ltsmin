
/**
 * @file generichash.h
 * @brief Generic hash function code.
 *
 * This code was written by Bob Jenkins in 1996 bob_jenkins@burtleburtle.net and
 * put in the public domain. We will need to rewrite it to C99 standard with doxygen
 * annotations.
 *
 * CVS version $Id: generichash.h,v 1.1 2007/11/07 16:48:07 sccblom Exp $
 */

#ifndef GENERIC_HASH_H
#define GENERIC_HASH_H

typedef  unsigned long  long ub8;   /* unsigned 8-byte quantities */
typedef  unsigned long  int  ub4;   /* unsigned 4-byte quantities */
typedef  unsigned       char ub1;   /* unsigned 1-byte quantities */

/** 4 byte hash of byte string */
extern ub4 hash_4_1( register ub1 *k, register ub4 length, register ub4 initval);

/** 4 byte hash of 4 byte word string */
extern ub4 hash_4_4( register ub4 *k, register ub4 length, register ub4 initval);

/** 8 byte hash of byte string */
extern ub8 hash_8_1( register ub1 *k, register ub8 length, register ub8 initval);

/** 8 byte hash of 8 byte word string */
extern ub8 hash_8_8( register ub8 *k, register ub8 length, register ub8 initval);

#endif

