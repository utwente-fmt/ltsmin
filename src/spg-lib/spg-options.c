/*
 * \file spg-options.c
 */
#include <hre/config.h>

#include <limits.h>
#if HAVE_PROFILER
#include <gperftools/profiler.h>
#endif

#include <hre/user.h>
#include <spg-lib/spg-options.h>
#include <spg-lib/spg-attr.h>

static int saturating_attractor_flag = 0;

#ifdef LTSMIN_DEBUG
static int dot_flag = 0;
#endif

static char* strategy_filename = NULL;

static int check_strategy_flag = 0;

static int interactive_strategy_play_flag = 0;

static int player = 0;

static enum { DEFAULT, CHAIN, PAR, PAR2 } attr_strategy = DEFAULT;

static char *attr_str = "default";
static si_map_entry ATTR[] = {
    {"default", DEFAULT},
    {"chain",   CHAIN},
#ifdef HAVE_SYLVAN
    {"par",     PAR},
    {"par2",    PAR2},
#endif
    {NULL, 0}
};

static void
spg_solve_popt(poptContext con, enum poptCallbackReason reason,
               const struct poptOption * opt, const char * arg, void * data)
{
    (void)con; (void)opt; (void)arg; (void)data;

    switch (reason) {
    case POPT_CALLBACK_REASON_PRE:
        Abort("unexpected call to spg_solve_popt");
    case POPT_CALLBACK_REASON_POST: {
        int res;

        res = linear_search(ATTR, attr_str);
        if (res < 0) {
            Print(lerror, "Unknown attractor strategy %s", attr_str);
            HREexitUsage(HRE_EXIT_FAILURE);
        } else if (HREme(HREglobal())==0) {
            Print(info, "Attractor strategy is %s", attr_str);
        }
        attr_strategy = res;
        return;
    }
    case POPT_CALLBACK_REASON_OPTION:
        Abort("unexpected call to spg_solve_popt");
    }
}


struct poptOption spg_solve_options[]={
    { NULL, 0 , POPT_ARG_CALLBACK|POPT_CBFLAG_POST|POPT_CBFLAG_SKIPOPTION , (void*)spg_solve_popt , 0 , NULL , NULL },
#ifdef HAVE_SYLVAN
    { "attr" , 0 , POPT_ARG_STRING|POPT_ARGFLAG_SHOW_DEFAULT , &attr_str , 0 , "set the attractor strategy" , "<default|chain|par|par2>" },
#else
    { "attr" , 0 , POPT_ARG_STRING|POPT_ARGFLAG_SHOW_DEFAULT , &attr_str , 0 , "set the attractor strategy" , "<default|chain>" },
#endif
    { "saturating-attractor" , 0 , POPT_ARG_NONE , &saturating_attractor_flag, 0, "Use attractor with saturation.","" },
#ifdef LTSMIN_DEBUG
    { "pg-write-dot" , 0 , POPT_ARG_NONE , &dot_flag, 0, "Write dot files to disk.","" },
#endif
    { "write-strategy" , 0 , POPT_ARG_STRING , &strategy_filename, 0, "file to write the computed strategy to","<strategy>.spg" },
    { "check-strategy" , 0 , POPT_ARG_NONE , &check_strategy_flag, 0, "run random plays to test the strategy", NULL },
    { "interactive-play" , 0 , POPT_ARG_NONE , &interactive_strategy_play_flag, 0, "play interactively according to the strategy", NULL },
    { "player" , 0 , POPT_ARG_NONE , &player, 0, "player (default: 0)", "" },
    POPT_TABLEEND
};


/**
 * \brief Creates a new spgsolver_options object.
 */
spgsolver_options* spg_get_solver_options()
{
    spgsolver_options* options = (spgsolver_options*)RTmalloc(sizeof(spgsolver_options));
    switch (attr_strategy) {
    case CHAIN:
        options->attr = spg_attractor_chaining;
        Print(infoLong, "attractor: chaining");
        break;
#ifdef HAVE_SYLVAN
    case PAR:
        options->attr = spg_attractor_par;
        Print(infoLong, "attractor: par");
        break;
    case PAR2:
        options->attr = spg_attractor_par2;
        Print(infoLong, "attractor: par2");
        break;
#endif
    default:
        options->attr = spg_attractor;
        Print(infoLong, "attractor: default");
        break;
    }
    options->attr_options = (spg_attr_options*)RTmalloc(sizeof(spg_attr_options));
    options->attr_options->dot = false;
#ifdef LTSMIN_DEBUG
    options->attr_options->dot = (dot_flag > 0);
    options->attr_options->dot_count = 0;
#endif
    options->attr_options->saturation = (saturating_attractor_flag > 0);
    options->attr_options->timer = RTcreateTimer();
    options->strategy_filename = strategy_filename;
    options->attr_options->compute_strategy = (strategy_filename != NULL);
    if (options->attr_options->compute_strategy) {
        if (options->attr == spg_attractor_chaining) {
            Abort("Computing stategy not supported in chaining attractor. Use, e.g., '--attr=default'.");
        }
        Print(info, "Writing winning strategies to %s", strategy_filename);
    }
    options->check_strategy = (check_strategy_flag > 0);
    options->interactive_strategy_play = (interactive_strategy_play_flag > 0);
    if (player == 0 || player == 1) {
        options->player = player;
    } else {
        Abort("Invalid player: %d", player);
    }
    return options;
}


/**
 * \brief Destroy options object
 */
void spg_destroy_solver_options(spgsolver_options* options)
{
    RTdeleteTimer(options->attr_options->timer);
    RTfree(options->attr_options);
    RTfree(options);
}

