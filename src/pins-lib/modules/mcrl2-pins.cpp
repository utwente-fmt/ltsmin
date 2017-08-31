#include <hre/config.h>

#include <algorithm>
#include <iterator>
#include <iostream>
#include <memory>
#include <string>
#include <set>
#include <vector>
#include <stack>
#include <utility>

#include <mcrl2/utilities/logger.h>
#include <mcrl2/lps/ltsmin.h>

extern "C" {
#include <popt.h>
#include <sys/stat.h>

#include <dm/dm.h>
#include <hre/user.h>
#include <pins-lib/pins.h>
#include <pins-lib/pins-util.h>
#include <pins-lib/modules/mcrl2-pins.h>
#include <ltsmin-lib/ltsmin-standard.h>
}

#if defined(MCRL2_JITTYC_AVAILABLE) && !defined(DISABLE_JITTYC)
static const char* mcrl2_rewriter = "jittyc";
#else
static const char* mcrl2_rewriter = "jitty";
#endif
static int mcrl2_verbosity = 0;
static int         finite_types = 0;
static int         readable_edge_labels = 1;

namespace ltsmin {

class pins : public mcrl2::lps::pins {
public:
    typedef ltsmin_state_type state_vector;
    typedef int *label_vector;

    pins(model_t& model, std::string lpsfilename, std::string rewr_args)
        : mcrl2::lps::pins(lpsfilename, rewr_args), model_(model) {
        map_.resize(datatype_count());
        rmap_.resize(datatype_count());
    }

    template <typename callback>
    void next_state_long(state_vector const& src, std::size_t group, callback&& f,
                         state_vector const& dest, label_vector const& labels)
    {
        int state[process_parameter_count()];
        for (size_t i = 0; i < process_parameter_count(); ++i) {
            int mt = process_parameter_type (i);
            int pt = lts_type_get_state_typeno (GBgetLTStype (model_), i);
            state[i] = find_mcrl2_index (mt, pt, src[i]);
        }
        mcrl2::lps::pins::next_state_long (state, group, std::forward<callback>(f), dest, labels);
    }

#ifdef MCRL2_GUARDS
    template <typename callback>
    void update_long(state_vector const& src, std::size_t group, callback&& f,
                         state_vector const& dest, label_vector const& labels)
    {
        int state[process_parameter_count()];
        for (size_t i = 0; i < process_parameter_count(); ++i) {
            int mt = process_parameter_type (i);
            int pt = lts_type_get_state_typeno (GBgetLTStype (model_), i);
            state[i] = find_mcrl2_index (mt, pt, src[i]);
        }
        mcrl2::lps::pins::update_long (state, group, std::forward<callback>(f), dest, labels);
    }

    guard_evaluation_t eval_guard_long(state_vector const& src, std::size_t guard)
    {
        int state[process_parameter_count()];
        for (size_t i = 0; i < process_parameter_count(); ++i) {
            int mt = process_parameter_type (i);
            int pt = lts_type_get_state_typeno (GBgetLTStype (model_), i);
            state[i] = find_mcrl2_index (mt, pt, src[i]);
        }
        return mcrl2::lps::pins::eval_guard_long(state, guard);
    }
#endif

    template <typename callback>
    void next_state_all(state_vector const& src, callback&& f,
                        state_vector const& dest, label_vector const& labels)
    {
        int state[process_parameter_count()];
        for (size_t i = 0; i < process_parameter_count(); ++i) {
            int mt = process_parameter_type (i);
            int pt = lts_type_get_state_typeno (GBgetLTStype (model_), i);
            state[i] = find_mcrl2_index (mt, pt, src[i]);
        }
        mcrl2::lps::pins::next_state_all (state, std::forward<callback>(f), dest, labels);
    }

    void make_pins_edge_labels(label_vector const& src, label_vector const& dst)
    {
        for (size_t i = 0; i < edge_label_count(); ++i) {
            int mt = edge_label_type (i);
            int pt = lts_type_get_edge_label_typeno (GBgetLTStype (model_), i);
            dst[i] = find_pins_index (mt, pt, src[i], readable_edge_labels);
        }
    }

    void make_pins_state (state_vector const& src, state_vector const& dst)
    {
        for (size_t i = 0; i < process_parameter_count(); ++i) {
            int mt = process_parameter_type (i);
            int pt = lts_type_get_state_typeno (GBgetLTStype (model_), i);
            dst[i] = find_pins_index (mt, pt, src[i]);
        }
    }

    int pretty_index (int i, int idx)
    {
        if (i >= (int)process_parameter_count())
            Abort("Parameter index exceeds number of parameters.");
        int mt = process_parameter_type (i);
        int pt = lts_type_get_state_typeno (GBgetLTStype (model_), i);

        data_format_t format = lts_type_get_format (GBgetLTStype (model_), pt);
        switch (format) {
        case LTStypeDirect:
        case LTStypeRange:
        case LTStypeBool:
        case LTStypeTrilean:
        case LTStypeSInt32:
            return idx;
        case LTStypeChunk:
        case LTStypeEnum:
            break;
        }

        chunk c = pins_chunk_get (model_, pt, idx);
        if (c.len == 0)
            Abort ("lookup of %d failed", idx);

        std::string s = std::string(reinterpret_cast<char*>(c.data), c.len);

        std::string pretty = data_type(mt).print(data_type(mt).deserialize(s));

        int pidx = pins_chunk_put (model_, pt,
                                       chunk_str(const_cast<char*>(pretty.c_str())));
        return pidx;
    }

    inline int find_pins_index (int mt, int pt, int idx, int pretty = 0)
    {
        data_format_t format = lts_type_get_format (GBgetLTStype (model_), pt);
        switch (format){
        case LTStypeDirect:
        case LTStypeRange:
        case LTStypeBool:
        case LTStypeTrilean:
        case LTStypeSInt32:
            return idx;
        case LTStypeChunk:
        case LTStypeEnum:
            break;
        }

        std::map<int,int>::iterator it = rmap_[mt].find(idx);
        if (it != rmap_[mt].end())
            return it->second;

        std::string c;

        if (!pretty)
            c = data_type(mt).serialize(idx);
        else
            c = data_type(mt).print(idx);

        if (finite_types && format == LTStypeEnum)
            Abort("lookup of %s failed", c.c_str());

        int pidx = pins_chunk_put (model_, pt,
                               chunk_str(const_cast<char*>(c.c_str())));
        map_[mt].resize (pidx + 1, IDX_NOT_FOUND);
        map_[mt][pidx] = idx;
        return rmap_[mt][idx] = pidx;
    }

    inline int find_mcrl2_index (int mt, int pt, int idx, int pretty = 0)
    {
        data_format_t format = lts_type_get_format (GBgetLTStype (model_), pt);
        switch (format) {
        case LTStypeDirect:
        case LTStypeRange:
        case LTStypeBool:
        case LTStypeTrilean:
        case LTStypeSInt32:
            return idx;
        case LTStypeChunk:
        case LTStypeEnum:
            break;
        }

        if (idx < static_cast<ssize_t>(map_[mt].size())
            && map_[mt][idx] != IDX_NOT_FOUND)
            return map_[mt][idx];

        chunk c = pins_chunk_get (model_, pt, idx);
        if (c.len == 0)
            Abort ("lookup of %d failed", idx);

        std::string s = std::string(reinterpret_cast<char*>(c.data), c.len);

        if (finite_types && format == LTStypeEnum)
            Abort ("lookup of %s failed", s.c_str());

        int midx;

        if (!pretty)
            midx = data_type(mt).deserialize(s);
        else
            midx = data_type(mt).parse(s);

        rmap_[mt][midx] = idx;
        map_[mt].resize (idx+1, IDX_NOT_FOUND);
        return map_[mt][idx] = midx;
    }

    void populate_type_index(int mt, int pt, int pretty = 0)
    {
        switch (lts_type_get_format (GBgetLTStype (model_), pt)) {
        case LTStypeDirect:
        case LTStypeRange:
        case LTStypeBool:
        case LTStypeTrilean:
        case LTStypeSInt32:
            return; // Direct and range types are automatically prefilled
        case LTStypeChunk:
            return; // Chunk types cannot be prefilled
        case LTStypeEnum:
            break;
        }

        std::vector<std::string> values
            = data_type(mt).generate_values(std::numeric_limits<std::size_t>::max());
        std::vector<std::string>::const_iterator i;

        for (i = values.begin(); i != values.end(); ++i) {
            int midx;

            if (!pretty)
                midx = data_type(mt).deserialize(*i);
            else
                midx = data_type(mt).parse(*i);

            std::map<int,int>::iterator it = rmap_[mt].find(midx);
            if (it != rmap_[mt].end())
                continue;

            int pidx = pins_chunk_put (model_, pt,
                                       chunk_str(const_cast<char*>((*i).c_str())));

            map_[mt].resize (pidx + 1, IDX_NOT_FOUND);
            map_[mt][pidx] = midx;
            rmap_[mt][midx] = pidx;
        }
    }

    int groups_of_edge (int edgeno, int index, const int **groups)
    {
        int mt = edge_label_type(edgeno);
        int pt = lts_type_get_edge_label_typeno (GBgetLTStype (model_), edgeno);
        int id = find_mcrl2_index (mt, pt, index, readable_edge_labels);

        std::string c;

        if (!readable_edge_labels)
            c = data_type(mt).serialize(id);
        else
            c = data_type(mt).print(id);

        groups_of_edge_.clear();

        for (size_t i = 0; i < group_count(); i++) {
            std::set<std::string> s = summand_action_names(i);

            for (std::set<std::string>::iterator j = s.begin(); j != s.end(); ++j) {
                if (c.find(*j) == std::string::npos) groups_of_edge_.push_back(static_cast<int>(i));
            }
        }

        *groups = &groups_of_edge_[0];

        return static_cast<int>(groups_of_edge_.size());
    }

private:
    static const int IDX_NOT_FOUND;
    model_t model_;
    std::vector< std::map<int,int> > rmap_;
    std::vector< std::vector<int> > map_;
    std::vector<int> groups_of_edge_;
};

// initialisation outside class to avoid linking error
const int pins::IDX_NOT_FOUND = -1;

struct state_cb
{
    typedef ltsmin::pins::state_vector state_vector;
    typedef ltsmin::pins::label_vector label_vector;
    ltsmin::pins *pins;
    TransitionCB& cb;
    void* ctx;
    size_t count;

    state_cb (ltsmin::pins *pins_, TransitionCB& cb_, void *ctx_)
        : pins(pins_), cb(cb_), ctx(ctx_), count(0)
    {}

    void operator()(state_vector const& next_state,
                    label_vector const& edge_labels, int group = -1)
    {
        int lbl[pins->edge_label_count()];
        pins->make_pins_edge_labels(edge_labels, lbl);
        int dst[pins->process_parameter_count()];
        pins->make_pins_state(next_state, dst);
        transition_info_t ti = GB_TI(lbl,group);
        cb (ctx, &ti, dst, NULL);
        ++count;
    }

    size_t get_count() const
    {
        return count;
    }

};

}

extern "C" {

static void
mcrl2_popt (poptContext con, enum poptCallbackReason reason,
            const struct poptOption *opt, const char *arg, void *data)
{
    (void)con;(void)opt;(void)arg;(void)data;
    switch (reason) {
    case POPT_CALLBACK_REASON_PRE:
        break;
    case POPT_CALLBACK_REASON_POST: {
        GBregisterLoader("lps", MCRL2loadGreyboxModel);
        GBregisterLoader("txt", MCRL2CompileGreyboxModel);
        if (mcrl2_verbosity > 0) {
            Warning(info, "increasing mcrl2 verbosity level by %d", mcrl2_verbosity);
            mcrl2_log_level_t level = static_cast<mcrl2_log_level_t>(static_cast<size_t>(mcrl2_logger::get_reporting_level()) + mcrl2_verbosity);
            mcrl2_logger::set_reporting_level(level);
        }
#ifdef DISABLE_JITTYC
        if (strcmp(mcrl2_rewriter, "jittyc") == 0) {
            Abort("The jittyc rewriter was disabled at compile time");
        }
#endif
        Warning(info,"mCRL2 language module initialized");
        return;
    }
    case POPT_CALLBACK_REASON_OPTION:
        break;
    }
    Abort ("unexpected call to mcrl2_popt");
}

struct poptOption mcrl2_options[] = {
    { NULL, 0, POPT_ARG_CALLBACK|POPT_CBFLAG_POST|POPT_CBFLAG_SKIPOPTION, (void*)mcrl2_popt, 0 , NULL , NULL},
    { "mcrl2-rewriter", 0, POPT_ARG_STRING|POPT_ARGFLAG_SHOW_DEFAULT, &mcrl2_rewriter, 0, "select mCRL2 rewriter: jittyc, jitty, ...", NULL },
    { "mcrl2-verbosity", 0, POPT_ARG_INT, &mcrl2_verbosity, 1, "increase mCRL2 verbosity", "INT" },
    { "mcrl2-finite-types", 0, POPT_ARG_VAL, &finite_types, 1, "use mCRL2 finite type information", NULL },
    { "mcrl2-internal-edge-labels", 0, POPT_ARG_VAL, &readable_edge_labels, 0, "use mcrl2-internal edge label encoding", NULL },
    POPT_TABLEEND
};

void
MCRL2CompileGreyboxModel(model_t model, const char *filename)
{
    const char* command = "txt2lps %s %s.lps";
    char buf[snprintf(NULL, 0, command, filename, filename) + 1];
    sprintf(buf, command, filename, filename);

    Warning(info, "Compiling .txt to .lps with txt2lps ...");

    if (system(buf) != 0) Abort("Could not launch 'txt2lps', make sure it is on your PATH");

    char newfile[strlen(filename) + 4 + 1];
    sprintf(newfile, "%s.lps", filename);

    MCRL2loadGreyboxModel(model, newfile);
}

static int
MCRL2getTransitionsLong (model_t m, int group, int *src, TransitionCB cb, void *ctx)
{
    ltsmin::pins *pins = reinterpret_cast<ltsmin::pins*>(GBgetContext (m));
    int dst[pins->process_parameter_count()];
    int labels[pins->edge_label_count()];
    ltsmin::state_cb f(pins, cb, ctx);
    pins->next_state_long(src, group, f, dst, labels);
    return f.get_count();
}

#ifdef MCRL2_GUARDS
static int
MCRL2getUpdateLong (model_t m, int group, int *src, TransitionCB cb, void *ctx)
{
    ltsmin::pins *pins = reinterpret_cast<ltsmin::pins*>(GBgetContext (m));
    int dst[pins->process_parameter_count()];
    int labels[pins->edge_label_count()];
    ltsmin::state_cb f(pins, cb, ctx);
    pins->update_long(src, group, f, dst, labels);
    return f.get_count();
}
#endif

static int
MCRL2getTransitionsAll (model_t m, int* src, TransitionCB cb, void *ctx)
{
    ltsmin::pins *pins = reinterpret_cast<ltsmin::pins*>(GBgetContext (m));
    int dst[pins->process_parameter_count()];
    int labels[pins->edge_label_count()];
    ltsmin::state_cb f(pins, cb, ctx);
    pins->next_state_all(src, f, dst, labels);
    return f.get_count();
}

static int
MCRL2groupsOfEdge (model_t m, int edge_no, int index, const int **groups)
{
    ltsmin::pins *pins = reinterpret_cast<ltsmin::pins*>(GBgetContext (m));
    return pins->groups_of_edge(edge_no, index, groups);
}

static int
MCRL2prettyPrint (model_t m, int pos, int idx)
{
    ltsmin::pins *pins = reinterpret_cast<ltsmin::pins*>(GBgetContext (m));
    return pins->pretty_index(pos, idx);
}

ltsmin::pins *pins;

#ifdef MCRL2_GUARDS

guard_t** guards;

void
MCRL2exit ()
{
    RTfree(guards);
    delete pins;
}

int MCRL2getLabelLong(model_t m, int function, int *src) {

    ltsmin::pins *pins = reinterpret_cast<ltsmin::pins*>(GBgetContext (m));
    return pins->eval_guard_long(src, function);
}

guard_t** MCRL2getAllGuards() {
    return guards;
}

#else

void
MCRL2exit ()
{
    delete pins;
}

#endif

void
MCRL2loadGreyboxModel (model_t m, const char *model_name)
{
    Warning(info, "mCRL2 rewriter: %s", mcrl2_rewriter);
    // check file exists
    struct stat st;
    if (stat(model_name, &st) != 0)
        Abort ("File does not exist: %s", model_name);

    pins = new ltsmin::pins(m, std::string(model_name), std::string(mcrl2_rewriter));
    GBsetContext(m,pins);

    lts_type_t ltstype = lts_type_create();
    lts_type_set_state_length (ltstype, pins->process_parameter_count());

    // create ltsmin type for each mcrl2-provided type
    for(size_t i = 0; i < pins->datatype_count(); ++i) {
        std::string n = pins->data_type(i).name();
        if (pins->data_type(i).is_bounded())
            lts_type_put_type(ltstype, n.c_str(), LTStypeEnum, NULL);
        else
            lts_type_put_type(ltstype, n.c_str(), LTStypeChunk, NULL);
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
#ifdef MCRL2_GUARDS
    int guard_type = lts_type_add_type (ltstype, "guard", NULL);
    lts_type_set_format(ltstype, guard_type, LTStypeTrilean);
#endif

    GBsetLTStype(m,ltstype);

    for(size_t i = 0; i < pins->datatype_count(); ++i) {
        if (finite_types && pins->data_type(i).is_bounded()) {
            std::string n = pins->data_type(i).name();
            int pt = lts_type_put_type(ltstype, n.c_str(), LTStypeEnum, NULL);
            pins->populate_type_index(i, pt);
        }
    }

    int s0[pins->process_parameter_count()];
    ltsmin::pins::state_vector p_s0 = s0;
    pins->get_initial_state(p_s0);
    int tmp[pins->process_parameter_count()];
    pins->make_pins_state(s0,tmp);
    GBsetInitialState(m, tmp);

    matrix_t *p_dm_info       = new matrix_t;
    matrix_t *p_dm_read_info  = new matrix_t;
    matrix_t *p_dm_write_info = new matrix_t;
    dm_create(p_dm_info, pins->group_count(),
              pins->process_parameter_count());
    dm_create(p_dm_read_info, pins->group_count(),
              pins->process_parameter_count());
    dm_create(p_dm_write_info, pins->group_count(),
              pins->process_parameter_count());

#ifdef MCRL2_GUARDS
        matrix_t *p_dm_update_info = new matrix_t;
        dm_create(p_dm_update_info, pins->group_count(),
                  pins->process_parameter_count());
#endif

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

#ifdef MCRL2_GUARDS
        std::vector<size_t> const& vec_u = pins->update_group(i);
        for (size_t j=0; j <vec_u.size(); j++) {
            dm_set(p_dm_update_info, i, vec_u[j]);
        }
        GBsetMatrix(m, LTSMIN_MATRIX_ACTIONS_READS, p_dm_update_info, PINS_MAY_SET,
                                                PINS_INDEX_GROUP, PINS_INDEX_STATE_VECTOR);
#endif
    }

#ifdef MCRL2_GUARDS

    // init state labels
    int nguards = pins->guard_count();
    int sl_size = nguards;
    lts_type_set_state_label_count (ltstype, sl_size);

    for(int i = 0;i < sl_size; i++) {
        const std::string& name = pins->guard_name(i);
        lts_type_set_state_label_name (ltstype, i, name.c_str());
        lts_type_set_state_label_typeno (ltstype, i, guard_type);
    }

    lts_type_validate(ltstype); // done with ltstype

    // set the label group implementation
    sl_group_t* sl_group_all = (sl_group_t*) RTmallocZero(sizeof(sl_group_t) + sl_size * sizeof(int));
    sl_group_all->count = sl_size;
    for(int i=0; i < sl_group_all->count; i++) sl_group_all->sl_idx[i] = i;
    GBsetStateLabelGroupInfo(m, GB_SL_ALL, sl_group_all);
    if (nguards > 0) {
        sl_group_t* sl_group_guards = (sl_group_t*) RTmallocZero(sizeof(sl_group_t) + nguards * sizeof(int));
        sl_group_guards->count = nguards;
        for(int i=0; i < sl_group_guards->count; i++) sl_group_guards->sl_idx[i] = i;
        GBsetStateLabelGroupInfo(m, GB_SL_GUARDS, sl_group_guards);
    }

    matrix_t *p_sl_info = new matrix_t;
    dm_create (p_sl_info, sl_size, pins->process_parameter_count());

    for (int i = 0; i < dm_nrows(p_sl_info); i++) {
        std::vector<size_t> const& vec = pins->guard_parameters(i);
        for (size_t j=0; j < vec.size(); j++) {
            dm_set (p_sl_info, i, vec[j]);
        }
    }
    GBsetStateLabelInfo (m, p_sl_info);

    guards = (guard_t**) RTmalloc(pins->group_count()*sizeof(guard_t*));
    for (size_t i = 0; i < pins->group_count(); i++) {
        std::vector<size_t> const& vec = pins->guard_info(i);
        guards[i] = (guard_t*) RTmalloc(sizeof(int) + sizeof(int[vec.size()]));
        guards[i]->count = vec.size();
        for (size_t j = 0; j < vec.size(); j++) {
            guards[i]->guard[j] = vec[j];
        }
    }
    GBsetGuardsInfo(m, MCRL2getAllGuards());
    GBsetActionsLong(m, MCRL2getUpdateLong);
    GBsetStateLabelLong(m, (get_label_method_t)MCRL2getLabelLong);
#else
    lts_type_validate(ltstype); // done with ltstype
    matrix_t *p_sl_info = new matrix_t;
    dm_create (p_sl_info, 0, pins->process_parameter_count());
    GBsetStateLabelInfo (m, p_sl_info);
#endif

    GBsetDMInfo (m, p_dm_info);
    GBsetDMInfoRead (m, p_dm_read_info);
    GBsetDMInfoMustWrite (m, p_dm_write_info);
    GBsetNextStateLong (m, MCRL2getTransitionsLong);
    GBsetNextStateAll (m, MCRL2getTransitionsAll);
    GBsetGroupsOfEdge(m, MCRL2groupsOfEdge);
    GBsetPrettyPrint(m, MCRL2prettyPrint);

    atexit(MCRL2exit);
}

}
