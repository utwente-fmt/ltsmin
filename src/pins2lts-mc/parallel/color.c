#include <hre/config.h>

#include <pins2lts-mc/parallel/color.h>


nndfs_color_t
nn_get_color (bitvector_t *set, ref_t ref)
{
    return (nndfs_color_t){ .nn = bitvector_get2 (set, ref<<1) };
}

int
nn_set_color (bitvector_t *set, ref_t ref, nndfs_color_t color)
{
    return bitvector_isset_or_set2 (set, ref<<1, color.nn);
}

int
nn_color_eq (const nndfs_color_t a, const nndfs_color_t b)
{
    return a.nn == b.nn;
};


int
ndfs_has_color (bitvector_t *set, ref_t ref, ndfs_color_t color)
{
    return bitvector_is_set (set, (ref<<1)|color.n);
}

int
ndfs_try_color (bitvector_t *set, ref_t ref, ndfs_color_t color)
{
    return bitvector_isset_or_set (set, (ref<<1)|color.n);
}
