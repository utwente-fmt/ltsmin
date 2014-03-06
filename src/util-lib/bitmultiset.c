#include <hre/config.h>

#include <stdio.h>
#include <stdlib.h>

#include <util-lib/bitmultiset.h>

bms_t *
bms_create (size_t elements, size_t types)
{
    HREassert (types <= sizeof(char) * 8, "int multisets unimplemented");
    bms_t *bms = RTmallocZero (sizeof(bms_t));
    bms->set = RTmallocZero (elements);
    bms->lists = RTmallocZero (types * sizeof(ci_list)); // see bms_t
    bms->types = types;
    bms->elements = elements;
    for (size_t i = 0; i < types; i++)
        bms->lists[i] = RTmallocZero ((elements + 1) * sizeof(int));
    return bms;
}

void
bms_set_all (bms_t *bms, int set)
{
    int s = 1 << set;
    memset (bms->set, s, bms->elements);
}

void
bms_clear_all(bms_t *bms)
{
    bms->corrupt_stack = 0;
    bms_set_all (bms, 0);
    bms_clear_lists (bms);
}

void
bms_clear_lists(bms_t *bms)
{
    for (size_t i = 0; i < bms->types; i++) {
        bms_clear (bms, i);
    }
}

