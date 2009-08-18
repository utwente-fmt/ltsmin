/*
 * $Log: label.c,v $
 * Revision 1.1  2002/02/08 12:14:41  sccblom
 * Just saving.
 *
 */

#include "label.h"
#include "messages.h"
#include <string.h>

#define LABEL_BLOCK 10

static int label_max=0;
static int label_next=0;
static char** labels=(char**)0;


int getlabelindex(char *label, int create){
	int i;
	for(i=0;i<label_next;i++){
		if (!strcmp(label,labels[i])) {
			return i;
		}
	}
	if (!create) return -1;
	if (label_max==label_next) {
		label_max+=LABEL_BLOCK;
		labels=(char**)realloc(labels,label_max*sizeof(char*));
		if (!labels) Fatal(1,1,"Out of memory in getlabelindex");
	}
	labels[label_next]=strdup(label);
	if (!labels[label_next]) Fatal(1,1,"Out of memory in getlabelindex");
	return label_next++;
}

char *getlabelstring(int label){
	return labels[label];
}

int getlabelcount(){
	return label_next; 
}

