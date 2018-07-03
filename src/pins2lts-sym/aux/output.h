#ifndef AUX_OUTPUT_H
#define AUX_OUTPUT_H

#include <vset-lib/vector_set.h>

extern void do_output (char *etf_output, vset_t visited);

extern void do_dd_output (char *file);

extern void stats_and_progress_report (vset_t current, vset_t visited, int level);

extern void final_stat_reporting (vset_t visited);

void final_final_stats_reporting ();

#endif //AUX_OUTPUT_H
