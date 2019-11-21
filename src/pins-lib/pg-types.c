/*
 * pg-types.c
 *
 *  Created on: 30 Oct 2012
 *      Author: kant
 */
#include <hre/config.h>

#include <hre/user.h>
#include <pins-lib/pg-types.h>

const char* pg_player_print(pg_player_enum_t player)
{
    switch(player)
    {
        case PG_AND:    return "and";
        case PG_OR:     return "or";
        default: Abort("Unknown player: %d", player);
    }
}
