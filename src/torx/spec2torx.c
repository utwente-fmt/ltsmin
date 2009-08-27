#include "config.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <strings.h>
#include <ctype.h>
#include <assert.h>

#include <stdint.h>
#include <stringindex.h>
#include <limits.h>

#include "runtime.h"
#include "treedbs.h"

#if defined(MCRL)
#include "mcrl-greybox.h"
#endif
#if defined(MCRL2)
#include "mcrl2-greybox.h"
#endif
#if defined(NIPS)
#include "nips-greybox.h"
#endif
#if defined(ETF)
#include "etf-greybox.h"
#endif

static struct poptOption options[] = {
#if defined(MCRL)
    {NULL, 0, POPT_ARG_INCLUDE_TABLE, mcrl_options, 0, "mCRL options",
     NULL},
#endif
#if defined(MCRL2)
    {NULL, 0, POPT_ARG_INCLUDE_TABLE, mcrl2_options, 0, "mCRL2 options",
     NULL},
#endif
#if defined(NIPS)
    {NULL, 0, POPT_ARG_INCLUDE_TABLE, nips_options, 0, "NIPS options",
     NULL},
#endif
#if defined(ETF)
    {NULL, 0, POPT_ARG_INCLUDE_TABLE, etf_options, 0, "ETF options", NULL},
#endif
    {NULL, 0, POPT_ARG_INCLUDE_TABLE, greybox_options, 0,
     "Greybox options", NULL},
    POPT_TABLEEND
};

static void *
new_string_index (void *ctx)
{
    (void)ctx;
    Warning (info, "creating a new string index");
    return SIcreate ();
}

typedef struct {
    model_t             model;
    lts_type_t          ltstype;
} torx_ctx_t;

static int          N;
static int          K;
static int          state_labels;
static int          edge_labels;
static treedbs_t    dbs;

static void
torx_transition (void *arg, int *lbl, int *dst)
{

    torx_ctx_t         *ctx = (torx_ctx_t *)arg;

    int                 tmp = TreeFold (dbs, dst);
    chunk               c =
        GBchunkGet (ctx->model,
                    lts_type_get_edge_label_typeno (ctx->ltstype, 0),
                    lbl[0]);

    int                 vis = 1;
    if (c.len == 3 && strncmp (c.data, "tau", c.len) == 0)
        vis = 0;

    /* tab-separated fields: edge vis sat lbl pred vars state */
    fprintf (stdout, "Ee\t\t%d\t1\t%.*s\t\t\t%d\n", vis, c.len, c.data, tmp);
}

static int
torx_handle_request (torx_ctx_t *ctx, char *req)
{
    while (isspace (*req)) ++req;
    switch (req[0]) {
    case 'r':                           /* reset */
        fprintf (stdout, "R 0\t1\n");   /* initial state has index 0 */
        fflush (stdout);
        break;
    case 'e': {                         /* explore */
        int n, res;
        req++;
        while (isspace (*req)) ++req;
        if ((res = sscanf (req, "%u", &n)) != 1) {
            int                 l = strlen (req);
            if (req[l - 1] == '\n')
                req[l - 1] = '\0';
            fprintf (stdout, "E0 Missing event number (%s; sscanf found #%d)\n",
                     req, res);
        } else if (n >= TreeCount (dbs)) {
            fprintf (stdout, "E0 Unknown event number\n");
            fflush (stdout);
        } else {
            int src[N];
            TreeUnfold (dbs, n, src);
            fprintf (stdout, "EB\n");
            GBgetTransitionsAll (ctx->model, src, torx_transition, ctx);
            fprintf (stdout, "EE\n");
            fflush (stdout);
        }
        break;
    }
    case 'q': {
        fprintf (stdout, "Q\n");
        fflush (stdout);
        return 1;
        break;
    }
    default:                          /* unknown command */
        fprintf (stdout, "A_ERROR UnknownCommand: %s\n", req);
        fflush (stdout);
    }
    return 0;
}

static void
torx_ui (torx_ctx_t *ctx)
{
    char                buf[BUFSIZ];
    int                 stop = 0;
    while (!stop && fgets (buf, sizeof buf, stdin)) {
        if (!strchr (buf, '\n'))
            /* incomplete read; ignore the problem for now */
            Warning (info, "no end-of-line character read on standard input (incomplete read?)\n");
        stop = torx_handle_request (ctx, buf);
    }
}

int
main (int argc, char *argv[])
{
    char               *files[1];
    RTinitPopt (&argc, &argv, options, 1, 1, files, NULL, "<model>",
                "Run the TorX remote procedure call protocol on <model>.\n\n"
                "Options");
    Warning (info, "loading model from %s", files[0]);
    model_t             model = GBcreateBase ();
    GBsetChunkMethods (model, new_string_index, NULL,
                       (int2chunk_t)SIgetC, (chunk2int_t)SIputC,
                       (get_count_t)SIgetCount);

    GBloadFile (model, files[0], &model);

    lts_type_t          ltstype = GBgetLTStype (model);
    N = lts_type_get_state_length (ltstype);
    K = dm_nrows (GBgetDMInfo (model));
    Warning (info, "length is %d, there are %d groups", N, K);
    state_labels = lts_type_get_state_label_count (ltstype);
    edge_labels  = lts_type_get_edge_label_count (ltstype);
    Warning (info, "There are %d state labels and %d edge labels",
             state_labels, edge_labels);

    int                 ini[N];
    GBgetInitialState (model, ini);
    Warning (info, "got initial state");
    dbs = TreeDBScreate (N);
    int ini_idx = TreeFold (dbs, ini);
    assert (ini_idx == 0);

    torx_ctx_t          ctx = { model, ltstype };
    torx_ui (&ctx);

    exit (EXIT_SUCCESS);
}
