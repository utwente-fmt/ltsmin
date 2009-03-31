#ifndef ETF_UTIL_H
#define ETF_UTIL_H

/**
\file etf-util.h
\brief Enumerated Table Format Utilities.
*/

#include "lts-type.h"
#include "treedbs.h"
#include "chunk_support.h"

/**
Opaque type for ETF models.
*/
typedef struct etf_model_s *etf_model_t;

extern etf_model_t etf_parse(const char *file);

extern lts_type_t etf_type(etf_model_t model);

extern void etf_get_initial(etf_model_t model,int* state);

extern int etf_trans_section_count(etf_model_t model);

extern int etf_map_section_count(etf_model_t model);

extern treedbs_t etf_patterns(etf_model_t model);

extern treedbs_t etf_trans_section(etf_model_t model,int section);

extern treedbs_t etf_get_map(etf_model_t model,int map);

extern chunk etf_get_value(etf_model_t model,int type_no,int idx);

extern int etf_get_value_count(etf_model_t model,int type_no);

extern void etf_ode_add(etf_model_t model);

#endif

