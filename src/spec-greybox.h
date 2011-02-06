#ifndef SPEC_GREYBOX_H
#define SPEC_GREYBOX_H

#if defined(MCRL)
#include <mcrl-greybox.h>
#endif
#if defined(MCRL2)
#include <mcrl2-greybox.h>
#endif
#if defined(NIPS)
#include <nips-greybox.h>
#endif
#if defined(ETF)
#include <etf-greybox.h>
#endif
#if defined(DIVINE)
#include <dve-greybox.h>
#endif
#if defined(DIVINE2)
#include <dve2-greybox.h>
#endif
#if defined(SPINJA)
#include <spinja-greybox.h>
#endif

#if defined(MCRL)
#define SPEC_POPT_OPTIONS { NULL, 0, POPT_ARG_INCLUDE_TABLE, mcrl_options, 0, "mCRL options", NULL }
#endif
#if defined(MCRL2)
#define SPEC_POPT_OPTIONS { NULL, 0, POPT_ARG_INCLUDE_TABLE, mcrl2_options, 0, "mCRL2 options", NULL }
#endif
#if defined(NIPS)
#define SPEC_POPT_OPTIONS { NULL, 0, POPT_ARG_INCLUDE_TABLE, nips_options, 0, "NIPS options", NULL }
#endif
#if defined(ETF)
#define SPEC_POPT_OPTIONS { NULL, 0, POPT_ARG_INCLUDE_TABLE, etf_options, 0, "ETF options", NULL }
#endif
#if defined(DIVINE)
#define SPEC_POPT_OPTIONS { NULL, 0, POPT_ARG_INCLUDE_TABLE, dve_options, 0, "DiVinE options", NULL }
#endif
#if defined(DIVINE2)
#define SPEC_POPT_OPTIONS { NULL, 0, POPT_ARG_INCLUDE_TABLE, dve2_options, 0, "DiVinE 2 options", NULL }
#endif
#if defined(SPINJA)
#define SPEC_POPT_OPTIONS { NULL, 0, POPT_ARG_INCLUDE_TABLE, spinja_options, 0, "SPINJA options", NULL }
#endif

#endif
