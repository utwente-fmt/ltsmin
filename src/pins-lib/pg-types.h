/*
 * pg-types.h
 *
 *  Created on: 29 Oct 2012
 *      Author: kant
 */

#ifndef PG_TYPES_H_
#define PG_TYPES_H_

typedef enum {
    PG_PRIORITY = 0,    // priority
    PG_PLAYER = 1       // player
} pg_label_enum_t;

typedef enum {
    PG_OR = 0,      // or, even, Eloise
    PG_AND = 1      // and, odd, Abelard
} pg_player_enum_t;

const char* pg_player_print(pg_player_enum_t player);

#endif /* PG_TYPES_H_ */
