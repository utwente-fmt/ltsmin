#include <hre/config.h>

#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <strings.h>

#include <hre/user.h>
#include <ltsmin-lib/ltsmin-standard.h>
#include <pins-lib/pins.h>
#include <pins-lib/pins-impl.h>
#include <pins-lib/pins-util.h>
#include <hre/stringindex.h>
#include <util-lib/treedbs.h>


static struct poptOption options[] = {
    SPEC_POPT_OPTIONS,
    {NULL, 0, POPT_ARG_INCLUDE_TABLE, greybox_options, 0,
     "PINS options", NULL},
    POPT_TABLEEND
};

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
torx_transition (void *arg, transition_info_t *ti, int *dst, int *cpy)
{

    (void) cpy;
    torx_ctx_t         *ctx = (torx_ctx_t *)arg;

    int                 tmp = TreeFold (dbs, dst);
    int                 vis = 0;
    chunk               c;
    if (edge_labels > 0) {
        c = pins_chunk_get  (ctx->model,
                        lts_type_get_edge_label_typeno (ctx->ltstype, 0),
                        ti->labels[0]);
        if (c.len != strlen(LTSMIN_EDGE_VALUE_TAU) || strncmp (c.data, LTSMIN_EDGE_VALUE_TAU, c.len) != 0)
            vis = 1;
    } else {
        c.len  = strlen(LTSMIN_EDGE_VALUE_TAU);
        c.data = (char *) LTSMIN_EDGE_VALUE_TAU;
    }    
    
    /* tab-separated fields: edge vis sat lbl pred vars state */
    fprintf (stdout, "Ee\t\t%d\t1\t%.*s\t\t\t%d\n", vis, c.len, c.data, tmp);
}

static int
torx_handle_request (torx_ctx_t *ctx, char *req)
{
    while (isspace ((unsigned char)*req)) ++req;
    switch (req[0]) {
    case 'r':                           /* reset */
        fprintf (stdout, "R 0\t1\n");   /* initial state has index 0 */
        fflush (stdout);
        break;
    case 'e': {                         /* explore */
        int n, res;
        req++;
        while (isspace ((unsigned char)*req)) ++req;
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
    HREinitBegin(argv[0]);
    HREaddOptions(options,"Run the TorX remote procedure call protocol on <model>.\n\nOtions");
    //lts_lib_setup(); // TODO
    HREinitStart(&argc,&argv,1,1,files,"<model>");

    Warning (info, "loading model from %s", files[0]);
    model_t             model = GBcreateBase ();
    GBsetChunkMap (model, simple_table_factory_create());

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
    assert (ini_idx == 0); (void) ini_idx;

    torx_ctx_t          ctx = { model, ltstype };
    torx_ui (&ctx);

    HREexit (LTSMIN_EXIT_SUCCESS);
}
