/**
\file sigmin-types.h
\brief Global type definitions for signature minimization.
*/

#ifndef SIGMIN_TYPES_H
#define SIGMIN_TYPES_H

#include <stdint.h>

/**
\brief Type of a signature ID.

Performance is better if this is a 32 bit number.
But to be able to deal with very large LTSs, we will
have to change this to 64 bit soon.
*/
typedef uint32_t sig_id_t;

/**
\brief the undefined signature ID
*/
#define SIG_ID_NULL ((sig_id_t)-1)

#endif
