#ifndef GROUPS_H
#define GROUPS_H


#include "Dtaudlts.h"

int dlts_elim_tauscc_groups(dlts_t lts); 
// detects and collapses the strongly connected components of lts
// formed with edges labelled lts->tau

#endif
