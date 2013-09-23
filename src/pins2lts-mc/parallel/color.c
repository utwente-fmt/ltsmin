#include <hre/config.h>

#include <pins2lts-mc/parallel/color.h>
#include <pins2lts-mc/parallel/global.h>

static size_t               count_mask;
static size_t               count_bits;
static dbs_get_sat_f        get_sat_bit;
//static dbs_unset_sat_f    unset_sat_bit;
static dbs_try_set_sat_f    try_set_sat_bit;
static dbs_inc_sat_bits_f   inc_sat_bits;
static dbs_dec_sat_bits_f   dec_sat_bits;
static dbs_get_sat_bits_f   get_sat_bits;


void
setup_colors(size_t count_bits_,
             dbs_get_sat_f get_sat_bit_,
             dbs_try_set_sat_f try_set_sat_bit_,
             dbs_inc_sat_bits_f inc_sat_bits_,
             dbs_dec_sat_bits_f dec_sat_bits_,
             dbs_get_sat_bits_f get_sat_bits_)
{
    count_bits = count_bits_;
    count_mask = (1<<count_bits) - 1;
    get_sat_bit = get_sat_bit_;
    //unset_sat_bit;
    try_set_sat_bit = try_set_sat_bit_;
    inc_sat_bits = inc_sat_bits_;
    dec_sat_bits = dec_sat_bits_;
    get_sat_bits = get_sat_bits_;
}


nndfs_color_t
nn_get_color (bitvector_t *set, ref_t ref)
{ return (nndfs_color_t){ .nn = bitvector_get2 (set, ref<<1) };  }

int
nn_set_color (bitvector_t *set, ref_t ref, nndfs_color_t color)
{ return bitvector_isset_or_set2 (set, ref<<1, color.nn); }

int
nn_color_eq (const nndfs_color_t a, const nndfs_color_t b)
{ return a.nn == b.nn; };


int
ndfs_has_color (bitvector_t *set, ref_t ref, ndfs_color_t color)
{ return bitvector_is_set (set, (ref<<1)|color.n); }

int
ndfs_try_color (bitvector_t *set, ref_t ref, ndfs_color_t color)
{ return bitvector_isset_or_set (set, (ref<<1)|color.n); }



int
global_has_color (ref_t ref, global_color_t color, int rec_bits)
{
    return get_sat_bit(global->dbs, ref, rec_bits+count_bits+color.g);
}

int //RED and BLUE are independent
global_try_color (ref_t ref, global_color_t color, int rec_bits)
{
    return try_set_sat_bit(global->dbs, ref, rec_bits+count_bits+color.g);
}

uint32_t
inc_wip (ref_t ref)
{
    return inc_sat_bits(global->dbs, ref) & count_mask;
}

uint32_t
dec_wip (ref_t ref)
{
    return dec_sat_bits(global->dbs, ref) & count_mask;
}

uint32_t
get_wip (ref_t ref)
{
    return get_sat_bits(global->dbs, ref) & count_mask;
}
