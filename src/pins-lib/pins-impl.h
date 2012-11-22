#ifndef SPEC_GREYBOX_H
#define SPEC_GREYBOX_H

#if defined(MCRL)
#include <pins-lib/mcrl-pins.h>
#endif
#if defined(MCRL2)
#include <pins-lib/mcrl2-pins.h>
#endif
#if defined(PBES)
#include <pins-lib/pbes-pins.h>
#endif
#if defined(ETF)
#include <pins-lib/etf-pins.h>
#endif
#if defined(DIVINE)
#include <pins-lib/dve-pins.h>
#endif
#if defined(SPINJA)
#include <pins-lib/prom-pins.h>
#endif
#if defined(OPAAL)
#include <pins-lib/opaal-pins.h>
#endif

#if defined(MCRL)
#define SPEC_POPT_OPTIONS { NULL, 0, POPT_ARG_INCLUDE_TABLE, mcrl_options, 0, "mCRL options", NULL }
#define SPEC_MT_SAFE 0
#define SPEC_REL_PERF 1
#endif
#if defined(MCRL2)
#define SPEC_POPT_OPTIONS { NULL, 0, POPT_ARG_INCLUDE_TABLE, mcrl2_options, 0, "mCRL2 options", NULL }
#define SPEC_MT_SAFE 0
#define SPEC_REL_PERF 1
#endif
#if defined(PBES)
#define SPEC_POPT_OPTIONS { NULL, 0 , POPT_ARG_INCLUDE_TABLE, pbes_options , 0 , "PBES options", NULL }
#define SPEC_MT_SAFE 0
#define SPEC_REL_PERF 1
#endif
#if defined(ETF)
#define SPEC_POPT_OPTIONS { NULL, 0, POPT_ARG_INCLUDE_TABLE, etf_options, 0, "ETF options", NULL }
#define SPEC_MT_SAFE 1
#define SPEC_REL_PERF 100
#endif
#if defined(DIVINE)
#define SPEC_POPT_OPTIONS { NULL, 0, POPT_ARG_INCLUDE_TABLE, dve_options, 0, "DiVinE options", NULL }
#define SPEC_MT_SAFE 1
#define SPEC_REL_PERF 100
#endif
#if defined(SPINJA)
#define SPEC_POPT_OPTIONS { NULL, 0, POPT_ARG_INCLUDE_TABLE, prom_options, 0, "Promela options", NULL }
#define SPEC_MT_SAFE 1
#define SPEC_REL_PERF 100
#endif
#if defined(OPAAL)
#define SPEC_POPT_OPTIONS { NULL, 0, POPT_ARG_INCLUDE_TABLE, opaal_options, 0, "Opaal options", NULL }
#define SPEC_MT_SAFE 1
#define SPEC_REL_PERF 10
#endif

#endif
