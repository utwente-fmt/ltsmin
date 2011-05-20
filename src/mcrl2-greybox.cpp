#include <config.h>

#include <algorithm>
#include <iterator>
#include <iostream>
#include <memory>
#include <string>
#include <set>
#include <vector>
#include <stack>

#include <mcrl2/atermpp/aterm_init.h>
#include <mcrl2/lps/ltsmin.h>

extern "C" {
#include <assert.h>
#include <limits.h>
#include <popt.h>

#include <mcrl2-greybox.h>
#include <dm/dm.h>
#include <runtime.h>
}

static std::string mcrl2_rewriter_strategy;
#ifdef MCRL2_JITTYC_AVAILABLE
static char const* mcrl2_args="--rewriter=jittyc";
#else
static char const* mcrl2_args="--rewriter=jitty";
#endif

struct state_cb
{
    typedef int *state_vector;
    typedef int *label_vector;
    TransitionCB& cb;
    void* ctx;
    int& count;

    state_cb (TransitionCB& cb_, void *ctx_, int& count_)
        : cb(cb_), ctx(ctx_), count(count_)
    {}

    void operator()(state_vector const& next_state, label_vector const& edge_labels, int group = -1)
    {
        transition_info_t ti = { edge_labels, group };
        cb (ctx, &ti, next_state);
        ++count;
    }
};

class mcrl2_index {
public:
    std::string buffer_;
    mcrl2_index(mcrl2::lps::pins* pins, int tag)
        : pins_(pins), tag_(tag) {}
    mcrl2::lps::pins* get_pins() const { return pins_; }
    int tag() const { return tag_; }
protected:
    mcrl2::lps::pins* pins_;
    // crucially relies on that tag == typeno, although they
    // are created separately
    int tag_;
};

extern "C" {

static void *
mcrl2_newmap (void *ctx)
{
    static int tag = 0; // XXX typemap
    return (void*)new mcrl2_index(reinterpret_cast<mcrl2::lps::pins*>(ctx), tag++);
}

static int
mcrl2_chunk2int (void *map_, void *chunk, int len)
{
    mcrl2_index *map = reinterpret_cast<mcrl2_index*>(map_);
    mcrl2::lps::pins& pins = *map->get_pins();
    return pins.data_type(map->tag()).deserialize(std::string((char*)chunk,len));
}

static const void *
mcrl2_int2chunk (void *map_, int idx, int *len)
{
    mcrl2_index *map = reinterpret_cast<mcrl2_index*>(map_);
    mcrl2::lps::pins& pins = *map->get_pins();
    map->buffer_ = pins.data_type(map->tag()).print(idx); // XXX serialize
    *len = map->buffer_.length();
    return map->buffer_.data(); // XXX life-time
}

static int
mcrl2_chunk_count(void *map_)
{
    mcrl2_index *map = reinterpret_cast<mcrl2_index*>(map_);
    return map->get_pins()->data_type(map->tag()).size();
}

static void
override_chunk_methods(model_t model, mcrl2::lps::pins* pins)
{
    Warning(info, "This mcrl2 language module does not work with the MPI backend.");
    GBsetChunkMethods(model, mcrl2_newmap, pins,
                      (int2chunk_t)mcrl2_int2chunk,
                      mcrl2_chunk2int, mcrl2_chunk_count);
}

static void
mcrl2_popt (poptContext con, enum poptCallbackReason reason,
            const struct poptOption *opt, const char *arg, void *data)
{
    (void)con;(void)opt;(void)arg;(void)data;
    switch (reason) {
    case POPT_CALLBACK_REASON_PRE:
        break;
    case POPT_CALLBACK_REASON_POST: {
        Warning(debug,"mcrl2 init");
        int argc;
        char **argv;
        RTparseOptions (mcrl2_args,&argc,&argv);
        Warning (debug,"ATerm init");
        MCRL2_ATERMPP_INIT_(argc, argv, RTstackBottom());
        char *rewriter = NULL;
        struct poptOption options[] = {
            { "rewriter", 0 , POPT_ARG_STRING , &rewriter , 0 , "select rewriter" , NULL },
            POPT_TABLEEND
        };
        Warning (debug,"options");
        poptContext optCon = poptGetContext(NULL, argc,const_cast<const char**>(argv), options, 0);
        int res = poptGetNextOpt(optCon);
        if (res != -1 || poptPeekArg(optCon)!=NULL) {
            Fatal(1,error,"Bad mcrl2 options: %s",mcrl2_args);
        }
        poptFreeContext(optCon);
        if (rewriter) {
            mcrl2_rewriter_strategy = std::string(rewriter);
        } else {
            Fatal(1,error,"unrecognized rewriter: %s (jitty, jittyc, inner and innerc supported)",rewriter);
        }
        GBregisterLoader("lps",MCRL2loadGreyboxModel);
        Warning(info,"mCRL2 language module initialized");
        return;
    }
    case POPT_CALLBACK_REASON_OPTION:
        break;
    }
    Fatal(1,error,"unexpected call to mcrl2_popt");
}

struct poptOption mcrl2_options[] = {
    { NULL, 0 , POPT_ARG_CALLBACK|POPT_CBFLAG_POST|POPT_CBFLAG_SKIPOPTION , (void*)mcrl2_popt , 0 , NULL , NULL},
    { "mcrl2" , 0 , POPT_ARG_STRING|POPT_ARGFLAG_SHOW_DEFAULT , &mcrl2_args , 0, "Pass options to the mcrl2 library.","<mcrl2 options>" },
    POPT_TABLEEND
};

void
MCRL2initGreybox (int argc,char *argv[],void* stack_bottom)
{
    Warning(debug,"ATerm init");
    MCRL2_ATERMPP_INIT_(argc, argv, stack_bottom);
    (void)stack_bottom;
}

static int
MCRL2getTransitionsLong (model_t m, int group, int *src, TransitionCB cb, void *ctx)
{
    mcrl2::lps::pins *pins = (mcrl2::lps::pins *)GBgetContext (m);
    int count = 0;
    int dst[pins->process_parameter_count()];
    int labels[pins->edge_label_count()];
    state_cb f(cb, ctx, count);
    pins->next_state_long(src, group, f, dst, labels);
    return count;
}

static int
MCRL2getTransitionsAll (model_t m, int* src, TransitionCB cb, void *ctx)
{
    mcrl2::lps::pins *pins = (mcrl2::lps::pins *)GBgetContext (m);
    int count = 0;
    int dst[pins->process_parameter_count()];
    int labels[pins->edge_label_count()];
    state_cb f(cb, ctx, count);
    pins->next_state_all(src, f, dst, labels);
    return count;
}

void
MCRL2loadGreyboxModel (model_t m, const char *model_name)
{
    Warning(info, "mCRL2 rewriter: %s", mcrl2_rewriter_strategy.c_str());
    mcrl2::lps::pins *pins = new mcrl2::lps::pins(std::string(model_name), mcrl2_rewriter_strategy);
    GBsetContext(m,pins);

    lts_type_t ltstype = lts_type_create();
    lts_type_set_state_length (ltstype, pins->process_parameter_count());

    // create ltsmin type for each mcrl2-provided type
    for(size_t i = 0; i < pins->datatype_count(); ++i) {
        lts_type_add_type(ltstype, pins->data_type(i).name().c_str(), NULL);
    }

    // process parameters
    for(size_t i = 0; i < pins->process_parameter_count(); ++i) {
        lts_type_set_state_name(ltstype, i, pins->process_parameter_name(i).c_str());
        lts_type_set_state_type(ltstype, i, pins->data_type(pins->process_parameter_type(i)).name().c_str());
    }

    // edge labels
    lts_type_set_edge_label_count(ltstype, pins->edge_label_count());
    for (size_t i = 0; i < pins->edge_label_count(); ++i) {
        lts_type_set_edge_label_name(ltstype, i, pins->edge_label_name(i).c_str());
        lts_type_set_edge_label_type(ltstype, i, pins->data_type(pins->edge_label_type(i)).name().c_str());
    }

    override_chunk_methods(m, pins);
    GBsetLTStype(m,ltstype);

    int s0[pins->process_parameter_count()];
    mcrl2::lps::pins::ltsmin_state_type p_s0 = s0;
    pins->get_initial_state(p_s0);
    GBsetInitialState(m, s0);

    matrix_t *p_dm_info       = reinterpret_cast<matrix_t *>(RTmalloc(sizeof *p_dm_info));
    matrix_t *p_dm_read_info  = reinterpret_cast<matrix_t *>(RTmalloc(sizeof *p_dm_read_info));
    matrix_t *p_dm_write_info = reinterpret_cast<matrix_t *>(RTmalloc(sizeof *p_dm_write_info));
    dm_create(p_dm_info, pins->group_count(),
              pins->process_parameter_count());
    dm_create(p_dm_read_info, pins->group_count(),
              pins->process_parameter_count());
    dm_create(p_dm_write_info, pins->group_count(),
              pins->process_parameter_count());

    for (int i = 0; i <dm_nrows (p_dm_info); i++) {
        std::vector<size_t> const& vec_r = pins->read_group(i);
        for (size_t j=0; j <vec_r.size(); j++) {
            dm_set (p_dm_info, i, vec_r[j]);
            dm_set (p_dm_read_info, i, vec_r[j]);
        }
        std::vector<size_t> const& vec_w = pins->write_group(i);
        for (size_t j=0; j <vec_w.size(); j++) {
            dm_set (p_dm_info, i, vec_w[j]);
            dm_set (p_dm_write_info, i, vec_w[j]);
        }
    }

    GBsetDMInfo (m, p_dm_info);
    GBsetDMInfoRead (m, p_dm_read_info);
    GBsetDMInfoWrite (m, p_dm_write_info);
    matrix_t *p_sl_info = reinterpret_cast<matrix_t *>(RTmalloc(sizeof *p_sl_info));
    dm_create (p_sl_info, 0, pins->process_parameter_count());
    GBsetStateLabelInfo (m, p_sl_info);
    GBsetNextStateLong (m, MCRL2getTransitionsLong);
    GBsetNextStateAll (m, MCRL2getTransitionsAll);
}

}
