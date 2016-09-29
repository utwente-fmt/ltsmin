#ifndef SPEC_GREYBOX_H
#define SPEC_GREYBOX_H

#if defined(MAPA)
#include <pins-lib/modules/mapa-pins.h>
#endif
#if defined(MCRL)
#include <pins-lib/modules/mcrl-pins.h>
#endif
#if defined(MCRL2)
#include <pins-lib/modules/mcrl2-pins.h>
#endif
#if defined(LTSMIN_PBES)
#include <pins-lib/modules/pbes-pins.h>
#endif
#if defined(ETF)
#include <pins-lib/modules/etf-pins.h>
#endif
#if defined(DIVINE)
#include <pins-lib/modules/dve-pins.h>
#endif
#if defined(SPINS)
#include <pins-lib/modules/prom-pins.h>
#endif
#if defined(OPAAL)
#include <pins-lib/modules/opaal-pins.h>
#endif
#if defined(PNML)
#include <pins-lib/modules/pnml-pins.h>
#endif
#if defined(PROB)
#include <pins-lib/modules/prob-pins.h>
#endif
#if defined(PINS_DLL)
#include <pins-lib/modules/dlopen-pins.h>
#endif


#if defined(MAPA)
#define SPEC_POPT_OPTIONS { NULL, 0, POPT_ARG_INCLUDE_TABLE, mapa_options, 0, "Scoop options", NULL }
#define SPEC_MT_SAFE 0
#define SPEC_REL_PERF 1
#define SPEC_MAYBE_AND_FALSE_IS_FALSE 1
#endif
#if defined(MCRL)
#define SPEC_POPT_OPTIONS { NULL, 0, POPT_ARG_INCLUDE_TABLE, mcrl_options, 0, "mCRL options", NULL }
#define SPEC_MT_SAFE 0
#define SPEC_REL_PERF 1
#define SPEC_MAYBE_AND_FALSE_IS_FALSE 1
#endif
#if defined(MCRL2)
#define SPEC_POPT_OPTIONS { NULL, 0, POPT_ARG_INCLUDE_TABLE, mcrl2_options, 0, "mCRL2 options", NULL }
#define SPEC_MT_SAFE 0
#define SPEC_REL_PERF 1
#define SPEC_MAYBE_AND_FALSE_IS_FALSE 1
#endif
#if defined(LTSMIN_PBES)
#define SPEC_POPT_OPTIONS { NULL, 0 , POPT_ARG_INCLUDE_TABLE, pbes_options , 0 , "PBES options", NULL }
#define SPEC_MT_SAFE 0
#define SPEC_REL_PERF 1
#define SPEC_MAYBE_AND_FALSE_IS_FALSE 1
#endif
#if defined(ETF)
#define SPEC_POPT_OPTIONS { NULL, 0, POPT_ARG_INCLUDE_TABLE, etf_options, 0, "ETF options", NULL }
#define SPEC_MT_SAFE 1
#define SPEC_REL_PERF 100
#define SPEC_MAYBE_AND_FALSE_IS_FALSE 0
#endif
#if defined(DIVINE)
#define SPEC_POPT_OPTIONS { NULL, 0, POPT_ARG_INCLUDE_TABLE, dve_options, 0, "DiVinE options", NULL }
#define SPEC_MT_SAFE 1
#define SPEC_REL_PERF 100
#define SPEC_MAYBE_AND_FALSE_IS_FALSE 0
#endif
#if defined(SPINS)
#define SPEC_POPT_OPTIONS { NULL, 0, POPT_ARG_INCLUDE_TABLE, prom_options, 0, "Promela options", NULL }
#define SPEC_MT_SAFE 1
#define SPEC_REL_PERF 100
#define SPEC_MAYBE_AND_FALSE_IS_FALSE 0
#endif
#if defined(OPAAL)
#define SPEC_POPT_OPTIONS { NULL, 0, POPT_ARG_INCLUDE_TABLE, opaal_options, 0, "Opaal options", NULL }
#define SPEC_MT_SAFE 1
#define SPEC_REL_PERF 10
#define SPEC_MAYBE_AND_FALSE_IS_FALSE 0
#endif
#if defined(PNML)
#define SPEC_POPT_OPTIONS { NULL, 0, POPT_ARG_INCLUDE_TABLE, pnml_options, 0, "PNML options", NULL }
#define SPEC_MT_SAFE 1
#define SPEC_REL_PERF 100
#define SPEC_MAYBE_AND_FALSE_IS_FALSE 1
#endif
#if defined(PROB)
#define SPEC_POPT_OPTIONS { NULL, 0, POPT_ARG_INCLUDE_TABLE, prob_options, 0, "ProB options", NULL }
#define SPEC_MT_SAFE 1
#define SPEC_REL_PERF 1
#define SPEC_MAYBE_AND_FALSE_IS_FALSE 1
#endif
#if defined(PINS_DLL)
#define SPEC_POPT_OPTIONS { NULL, 0, POPT_ARG_INCLUDE_TABLE, pins_plugin_options, 0, "PINS plugin options", NULL }
#define SPEC_MT_SAFE 0
#define SPEC_REL_PERF 10
#define SPEC_MAYBE_AND_FALSE_IS_FALSE 0
#endif

#endif
