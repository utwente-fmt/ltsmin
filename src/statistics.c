
#include <math.h>

#include <statistics.h>

void
statistics_record (statistics_t *stats, double x) {
    stats->k++;
    if (1 == stats->k) {
        stats->Mk = x;
        stats->Qk = 0;
    } else {
        double d = x - stats->Mk; // is actually xk - M_{k-1},
                                  // as Mk was not yet updated
        stats->Qk += (stats->k-1)*d*d/stats->k;
        stats->Mk += d/stats->k;
    }
}

double
statistics_stdev (statistics_t *stats)
{
    return sqrt (statistics_stdvar(stats));
}

size_t
statistics_nsamples (statistics_t *stats)
{
    return stats->k;
}

double
statistics_variance (statistics_t *stats)
{
    return stats->Qk / (stats->k - 1);
}

double
statistics_stdvar (statistics_t *stats)
{
    return stats->Qk / stats->k;
}

double
statistics_mean (statistics_t *stats)
{
    return stats->Mk;
}

void
statistics_init (statistics_t *stats)
{
    stats->k = 0;
}

