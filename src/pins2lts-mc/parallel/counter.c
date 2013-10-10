/**
 *
 */

#include <hre/config.h>

#include <hre/feedback.h>
#include <pins2lts-mc/parallel/counter.h>

void
work_add_results (work_counter_t *res, work_counter_t *cnt)
{
    res->explored += cnt->explored;
    res->trans += cnt->trans;
    res->level_cur += cnt->level_cur;
    res->level_max += cnt->level_max;
}

void
work_report (char *prefix, work_counter_t *cnt)
{
    Warning (info, "%s%zu levels %zu states %zu transitions",
             prefix, cnt->level_max, cnt->explored, cnt->trans);
}

void
work_report_estimate (char *prefix, work_counter_t *cnt)
{
    Warning (info, "%s~%zu levels ~%zu states ~%zu transitions",
             prefix, cnt->level_max, cnt->explored, cnt->trans);
}
