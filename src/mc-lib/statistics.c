#include <hre/config.h>

#include <math.h>

#include <hre/user.h>
#include <mc-lib/statistics.h>

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

void
statistics_unrecord (statistics_t *stats, double x) {
    if (0 == stats->k) {
        Abort ("Too many unrecords: statistics_unrecord");
    } else if (1 == stats->k) {
        stats->Qk = 0;
        stats->Mk = 0;
    } else {
        double d = stats->Mk * stats->k - x;
        stats->Mk = d / (stats->k - 1);
        d = (x - stats->Mk);  // is now: xk - M_{k-1}
        stats->Qk = (stats->Qk - (stats->k - 1)*d*d)/stats->k;
    }
    stats->k--;
}

void
statistics_union (statistics_t *out, statistics_t *s1, statistics_t *s2)
{
    size_t k1 = s1->k;
    size_t k2 = s2->k;
    out->k = k1 + k2;
    double Mk1 = s1->Mk;
    double Mk2 = s2->Mk;
    out->Mk = (k1 * Mk1 + k2 * Mk2) / out->k;
    out->Qk = (s1->Qk + Mk1*Mk1 + s2->Qk + Mk2*Mk2) - out->Mk*out->Mk;
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
    stats->Qk = 0;
    stats->Mk = 0;
}

