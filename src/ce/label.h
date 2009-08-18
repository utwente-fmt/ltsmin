/*
 * $Log: label.h,v $
 * Revision 1.1  2002/02/08 12:14:41  sccblom
 * Just saving.
 *
 */

#ifndef LABEL_H
#define LABEL_H


extern int getlabelindex(char *label, int create);

extern char *getlabelstring(int label);

extern int getlabelcount();

#endif
