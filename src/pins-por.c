#include <config.h>
#include <stdlib.h>
#include <limits.h>
#include <assert.h>

#include <dm/dm.h>
#include <greybox.h>
#include <runtime.h>
#include <unix.h>

/*
 * SHARED
 */
model_t
GBaddPOR (model_t model, const int has_ltl)
{
    return model;
}
