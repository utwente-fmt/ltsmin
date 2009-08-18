/*
 * $Log: aut-io.h,v $
 * Revision 1.1  2002/02/08 12:14:40  sccblom
 * Just saving.
 *
 */

#ifndef AUT_IO_H
#define AUT_IO_H "$Id: aut-io.h,v 1.1 2002/02/08 12:14:40 sccblom Exp $"

#include <stdio.h>
#include "lts.h"

extern void readaut(FILE *file,lts_t lts);

extern void writeaut(FILE *file,lts_t lts);

#endif
