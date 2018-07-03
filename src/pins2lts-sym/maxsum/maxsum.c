#include <hre/config.h>

#include <limits.h>
#include <popt.h>

#include <dm/bitvector.h>
#include <hre/stringindex.h>
#include <hre/user.h>
#include <ltsmin-lib/lts-type.h>
#include <ltsmin-lib/ltsmin-grammar.h>
#include <ltsmin-lib/ltsmin-parse-env.h>
#include <ltsmin-lib/ltsmin-standard.h>
#include <ltsmin-lib/ltsmin-syntax.h>
#include <ltsmin-lib/ltsmin-tl.h>
#include <util-lib/util.h>
#include <vset-lib/vector_set.h>
#include <vset-lib/vdom_object.h>

#define WHOLE_DOMAIN (strcmp(file, "") == 0)

/*
 * Index that will contain all ltsmin_expr_t's,
 * representing formulas, or files containing
 * formulas
 */
static string_index_t maxsums = NULL;

static const char maxsum_long[] = "maxsum";

#define IF_LONG(long) if(((opt->longName)&&!strcmp(opt->longName,long)))

/*
 * The domain to compute the maximum sum over.
 */
typedef struct {
    int count; // number of variables to sum over.
    int *vars; // the var indices to sum over.
} domain_t ;

/*
 * Array that contains all the domains.
 */
static domain_t *domains;

/*
 * Adds all option values of --maxsum values to maxsums.
 */
static void
maxsum_popt(poptContext con, enum poptCallbackReason reason,
               const struct poptOption * opt, const char * arg, void * data)
{
    (void)con; (void)opt; (void)data;

    switch (reason) {
        case POPT_CALLBACK_REASON_PRE:
        case POPT_CALLBACK_REASON_POST:
            Abort("unexpected call to maxsum_popt")
        case POPT_CALLBACK_REASON_OPTION: {
            IF_LONG(maxsum_long) {
                
                if (maxsums == NULL) maxsums = SIcreate();

                const char* string;
                if (arg == NULL) string = "";
                else string = arg;
                
                if (SIlookup(maxsums, string) != SI_INDEX_FAILED) {
                    Abort("Option '--%s=%s' given multiple times with same value.", maxsum_long, string);
                } else SIput(maxsums, string);
            }
            return;
        }
    }
}

struct poptOption maxsum_options[] = {
    { NULL, 0 , POPT_ARG_CALLBACK , (void*)maxsum_popt , 0 , NULL , NULL },
    { maxsum_long, 0, POPT_ARG_STRING|POPT_ARGFLAG_OPTIONAL, NULL, 0,
        "Compute the maximum sum over all state variables V...V\'. "
        "Can be given multiple times. "
        "Can also be read from a file. "
        "If no value is given, all integer type state variables will be assumed.", "<V + ... + V'>" },
    POPT_TABLEEND
};

/*
 * Creates an ltsmin_parse_env_t,
 * adds the binary operator '+', 
 * and all integer type state variables.
 */
static ltsmin_parse_env_t
create_env(lts_type_t lts_type, int N)
{
    ltsmin_parse_env_t env = LTSminParseEnvCreate();

    for (int i = 0; i < N; i++) {
        const int typeno = lts_type_get_state_typeno(lts_type, i);
        const data_format_t dt = lts_type_get_format(lts_type, typeno);
        if (dt == LTStypeSInt32) {
            char *name = lts_type_get_state_name(lts_type, i);
            HREassert(name);
            const int idx = LTSminStateVarIndex(env, name);
            HREassert (i == idx, "Model has equally named state variables ('%s') at index %d and %d", name, i, idx);
        }
    }

    if (SIgetCount(env->state_vars) == 0) {
        Warning(info, "Warning: option '--maxsum' given but no integer type state variables found!");
    }

    LTSminBinaryOperator(env, PRED_ADD, PRED_NAME(PRED_ADD), 1);

    return env;
}

/**
 * Checks weather \p e is a valid expression,
 * e.g. if no duplicate state variables are given.
 * Also, sets the bit in \p vars corresponding
 * to the state variable index.
 */
static void
check_expr(ltsmin_expr_t e, ltsmin_parse_env_t env, int i, bitvector_t *vars)
{
    switch (e->node_type) {
        case BINARY_OP: {
            switch (e->token) {
                case PRED_ADD: {
                    check_expr(e->arg1, env, i, vars);
                    check_expr(e->arg2, env, i, vars);
                    break;
                }
                default: {
                    LTSminLogExpr(lerror, "Unsupported expression: ", e, env);
                    HREabort(LTSMIN_EXIT_FAILURE);
                }
            }
            break;
        }
        case SVAR: {
            if (bitvector_isset_or_set(vars, e->idx)) {
                LTSminLogExpr(lerror, "duplicate variable: ", e, env);
                HREabort(LTSMIN_EXIT_FAILURE);
            }
            break;
        }
        default: {
            LTSminLogExpr(lerror, "Unsupported expression: ", e, env);
            Abort("Note that only integer type state variables can be used.");
        }
    }
}

/*
 * Parses a single value given with the option --maxsum.
 */
static void
parse_maxsum(int i, ltsmin_parse_env_t env, bitvector_t *vars) {
    const char *file = SIget(maxsums, i);

    if (WHOLE_DOMAIN) bitvector_invert(vars);
    else {
        const stream_t stream = read_formula(file);

        ltsmin_parse_stream(TOKEN_EXPR,env,stream);

        ltsmin_expr_t expr = env->expr;

        check_expr(expr, env, i, vars);

        LTSminExprDestroy(expr, 1);
    }
}

/*
 * Initializes the domain of --maxsum option \p i.
 * All bits that are high in \p vars will be added
 * to the domain (to domains[i]).
 */
static void
init_domain(int i, int N, bitvector_t *vars)
{
    const int domain_size = bitvector_n_high(vars);
    Warning(info, "Computing maximum sum over %d state variables.", domain_size);

    if (domain_size == 0) {
        domains[i].count = -1;
        domains[i].vars = NULL;
    } else {
        domains[i].count = domain_size;
        domains[i].vars = RTmalloc(sizeof(int[domain_size]));

        for (int j = 0, k = 0; j < N; j++) {
            if (bitvector_is_set(vars , j)) domains[i].vars[k++] = j;
        }
    }
}

/*
 * Initializes the maxsum subsystem.
 * First, the parse environment is created.
 * Second, all domains are intialized.
 * Third, every maxsum is parsed.
 */
void
init_maxsum(lts_type_t lts_type)
{
    if (maxsums != NULL) {
        const int N = lts_type_get_state_length(lts_type);

        ltsmin_parse_env_t env = create_env(lts_type, N);

        bitvector_t vars;
        bitvector_create(&vars, N);

        domains = RTmalloc(sizeof(domain_t[SIgetCount(maxsums)]));

        for (int i = 0; i < SIgetCount(maxsums); i++) {
            parse_maxsum(i, env, &vars);

            init_domain(i, N, &vars);

            bitvector_clear(&vars);
        }

        LTSminParseEnvDestroy(env);
        bitvector_free(&vars);
    }
}

/*
 * Below is the implementation of the vset visitor API.
 */

/*
 * The user context for the visitor.
 */
typedef struct maxsum_info {
    long long maxsum; // the maximum sum of the current value.
    struct maxsum_info* down; // the context of the next value.
    struct maxsum_info* right; // the context of the next vector.
} maxsum_info_t;

/*
 * Initializes the user context.
 * First, the maximum sum is set to 0.
 * Then the parent's child context is set to
 * this context.
 */
static void
maxsum_init(void* context, void* parent, int succ)
{
    maxsum_info_t* ctx = (maxsum_info_t*) context;
    maxsum_info_t* p = (maxsum_info_t*) parent;

    ctx->maxsum = 0;

    if (succ) p->down = ctx;
    else p->right = ctx;
}

/*
 * Sets the maximum sum of the current value,
 * if there is a cache hit.
 */
static void
maxsum_pre(int terminal, int val, int cached, void* result, void* context)
{
    (void) val;
    maxsum_info_t *ctx = (maxsum_info_t*) context;
    if(!terminal && cached) ctx->maxsum = (long long) result;
}

/*
 * Computes the maximum sum of this value and the rest, and the next vector.
 * Also checks for long overflow.
 */
static void
maxsum_post(int val, void* context, int* cache, void** result)
{
    maxsum_info_t *ctx = (maxsum_info_t*) context;

    if (    (ctx->down->maxsum > 0 && val > LLONG_MAX - ctx->down->maxsum) ||
            (ctx->down->maxsum < 0 && val < LLONG_MIN - ctx->down->maxsum)) {
        Abort("Integer overflow in maxsum");
    }

    ctx->maxsum = max(ctx->down->maxsum + val, ctx->right->maxsum);

    *cache = 1;

    *result = (void*) ctx->maxsum;
}

/*
 * Run the visitor on all --maxsum options.
 * First, the visitor functions are set.
 * Second, a new cache operation is requested.
 * Third, all maximum sums are computed.
 * The maxisum sums are printed on stderr.
 */
void
compute_maxsum(vset_t set, vdom_t dom)
{
    if (maxsums != NULL) {
        vset_visit_callbacks_t cbs;
        cbs.vset_visit_pre = maxsum_pre;
        cbs.vset_visit_init_context = maxsum_init;
        cbs.vset_visit_post = maxsum_post;
        cbs.vset_visit_cache_success = NULL;

        vset_t to_visit;
        maxsum_info_t context;
        context.maxsum = 0;

        const int cache_op = vdom_next_cache_op(dom);

        for (int i = 0; i < SIgetCount(maxsums); i++) {
            if (domains[i].count == -1) to_visit = set;
            else {
                to_visit = vset_create(dom, domains[i].count, domains[i].vars);
                vset_project(to_visit, set);
            }

            vset_visit_par(to_visit, &cbs, sizeof(maxsum_info_t), &context, cache_op);
            vdom_clear_cache(dom, cache_op);

            if (to_visit != set) {
                vset_destroy(to_visit);
                RTfree(domains[i].vars);
            }

            const char *file = SIget(maxsums, i);

            Warning(info, "Maximum sum of %s is: %ld", WHOLE_DOMAIN ? "all integer type state variables" : file, context.maxsum);
        }

        RTfree(domains);
        SIdestroy(&maxsums);
    }
}
