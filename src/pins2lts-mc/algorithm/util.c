
#include <hre/config.h>

#include <pins2lts-mc/algorithm/reach.h>

ref_t *
get_parent_ref (wctx_t *ctx, ref_t ref)
{
    alg_shared_t       *shared = ctx->run->shared;
    return &shared->parent_ref[ref];
}
