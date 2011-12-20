/*
 * $Log: set.h,v $
 * Revision 1.6  2003/05/20 15:12:55  sccblom
 * Added weak bisimulation and trace equivalence.
 *
 * Stefan.
 *
 * Revision 1.5  2003/04/22 15:33:27  sccblom
 * Distributed branching bisimulation v1.
 *
 * Revision 1.4  2002/12/05 15:27:43  sccblom
 * Fixed a number of bugs for branching bisimulation
 * Small improvements for single threaded tool.
 *
 * Revision 1.3  2002/05/15 12:21:59  sccblom
 * Added tex subdirectory and MPI prototype.
 *
 * Revision 1.2  2002/02/12 13:33:36  sccblom
 * First test version.
 *
 * Revision 1.1  2002/02/08 17:42:15  sccblom
 * Just saving.
 *
 */

#ifndef SET_H
#define SET_H

#include <stdio.h>

#define EMPTY_SET 0

#define UNDEFINED_SET (-1)

extern void SetPrint(FILE *f,int set);

extern void SetPrintIndex(FILE *f,int set,char **index);

extern void SetClear(int tag);

extern void SetFree();

extern int SetInsert(int set,int label,int dest);

extern int SetUnion(int set1,int set2);

extern int SetGetTag(int set);

extern void SetSetTag(int set,int tag);

extern int SetGetSize(int set);

extern void SetGetSet(int set,int*data);

extern int SetGetLabel(int set);

extern int SetGetDest(int set);

extern int SetGetParent(int set);

extern unsigned int SetGetHash(int set);

#endif

