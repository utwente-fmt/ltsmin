

#ifndef PAINT_H
#define PAINT_H


#include "Dtaudlts.h"

void taudlts_elim_mixed_transitions(taudlts_t t, taudlts_t tv, int* color);
void taudlts_init_paint_owncolor(taudlts_t t, int* color);
void taudlts_paintfwd(taudlts_t t, int* color);
void taudlts_decapitate(taudlts_t t, int* color, int* wscc, int* oscc);
void extreme_colours(taudlts_t t, taudlts_t tviz, int* wscc, int* oscc);

void dlts_elim_tauscc_colours(dlts_t lts); 
// detects and collapses the strongly connected components of lts
// formed with edges labelled lts->tau

#endif
