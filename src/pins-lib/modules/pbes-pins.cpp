/**
 \file pbes-greybox.cpp
 */

#include <hre/config.h>
#include <utility>
#include <mcrl2/pbes/pbes.h>
#include <mcrl2/pbes/detail/pbes_greybox_interface.h>
#include <mcrl2/pbes/pbes_explorer.h>
#include <mcrl2/pbes/detail/ppg_rewriter.h>


extern "C" {

#include <popt.h>
#include <sys/stat.h>

#include <hre/user.h>
#include <pins-lib/pg-types.h>
#include <pins-lib/pins.h>
#include <pins-lib/pins-util.h>
#include <pins-lib/modules/pbes-pins.h>

} // end of extern "C"

#if defined(MCRL2_JITTYC_AVAILABLE) && !defined(DISABLE_JITTYC)
static const char* mcrl2_rewriter = "jittyc";
#else
static const char* mcrl2_rewriter = "jitty";
#endif
static int mcrl2_verbosity = 0;

using namespace mcrl2;
using namespace mcrl2::core;
using namespace mcrl2::data;


namespace ltsmin
{

class explorer : public mcrl2::pbes_system::explorer {
private:
    model_t model_;
    std::vector<std::map<int,int> > local2global_maps;
    std::vector<std::vector<int> > global2local_maps;
    int* global_group_var_ids;
    static const int IDX_NOT_FOUND;
public:
    explorer(model_t& model, const std::string& filename, const std::string& rewrite_strategy, bool reset = false, bool always_split = false) :
#ifdef PBES_EXPLORER_VERSION
        mcrl2::pbes_system::explorer(filename, rewrite_strategy, reset, always_split),
#else
        mcrl2::pbes_system::explorer(filename, rewrite_strategy, reset),
#endif
        model_(model),
        global_group_var_ids(0)
    {
        local2global_maps.resize(get_info()->get_lts_type().get_number_of_state_types());
        for (size_t i = 0; i <= (size_t) get_info()->get_lts_type().get_number_of_state_types(); i++) {
            std::map<int,int> local2global_map;
            local2global_maps.push_back(local2global_map);
            std::vector<int> global2local_map;
            global2local_maps.push_back(global2local_map);
        }
        (void)always_split;
    }

    ~explorer()
    {
        if (global_group_var_ids != 0)
        {
            RTfree(global_group_var_ids);
        }
    }

    void init_group_id_cache()
    {
        if (global_group_var_ids != 0)
        {
            RTfree(global_group_var_ids);
        }
        global_group_var_ids = (int*)RTmalloc((this->get_info()->get_transition_variable_names().size())*sizeof(int));
        int type_no = lts_type_get_state_typeno(GBgetLTStype(model_), 0);
        int group = 0;
#ifdef PBES_EXPLORER_VERSION
        for(std::vector<std::string>::const_iterator var_name_it = this->get_info()->get_transition_variable_names().begin();
                var_name_it != this->get_info()->get_transition_variable_names().end(); ++var_name_it)
        {
            int group_var_id = this->put_chunk(type_no, *var_name_it);
#else
        for(std::map<int,std::string>::const_iterator var_name_it = this->get_info()->get_transition_variable_names().begin();
                var_name_it != this->get_info()->get_transition_variable_names().end(); ++var_name_it)
        {
            int group_var_id = this->put_chunk(type_no, var_name_it->second);
#endif
            global_group_var_ids[group] = group_var_id;
            group++;
        }
    }

    inline int put_chunk(int type_no, std::string value)
    {
        int index = pins_chunk_put(model_, type_no, chunk_str(const_cast<char*>(value.c_str())));
        return index;
    }

    void local_to_global(int* const& local, int* global)
    {
        int state_length = lts_type_get_state_length(GBgetLTStype(model_));
        for(int i=0; i<state_length; i++)
        {
            int type_no = lts_type_get_state_typeno(GBgetLTStype(model_), i);
            std::map<int,int>::iterator it = local2global_maps[type_no].find(local[i]);
            if(it != local2global_maps[type_no].end())
            {
                global[i] = it->second;
            }
            else
            {
                std::string s = this->get_value(type_no, local[i]);
                global[i] = this->put_chunk(type_no, s);
                local2global_maps[type_no][local[i]] = global[i];
                global2local_maps[type_no].resize(global[i]+1, IDX_NOT_FOUND);
                global2local_maps[type_no][global[i]] = local[i];
            }
        }
    }

    inline std::string get_chunk(int type_no, int index)
    {
        chunk c = pins_chunk_get(model_, type_no, index);
        if (c.len == 0) {
            Abort("lookup of %d failed", index);
        }
        return std::string(reinterpret_cast<char*>(c.data), c.len);
    }

    inline int find_local_index(int type_no, int global)
    {
        if (global < static_cast<ssize_t>(global2local_maps[type_no].size()) && global2local_maps[type_no][global] != IDX_NOT_FOUND)
        {
            return global2local_maps[type_no][global];
        }
        else
        {
            int local = this->get_index(type_no, this->get_chunk(type_no, global));
            global2local_maps[type_no].resize(global+1, IDX_NOT_FOUND);
            global2local_maps[type_no][global] = local;
            local2global_maps[type_no][local] = global;
            return local;
        }
    }

    void global_to_local(int* const& global, int* local)
    {
        int state_length = lts_type_get_state_length(GBgetLTStype(model_));
        for(int i=0; i<state_length; i++)
        {
            int type_no = lts_type_get_state_typeno(GBgetLTStype(model_), i);
            local[i] = find_local_index(type_no, global[i]);
        }
    }

    inline bool check_group(int* const& src, std::size_t group)
    {
        if (global_group_var_ids == 0)
        {
            Abort("call to check_group before init_group_id_cache.");
        }
        return global_group_var_ids[group] == src[0];
    }

    template <typename callback>
    void next_state_long(int* const& src, std::size_t group, callback&& f)
    {
        if (this->check_group(src, group))
        {
            int state_length = lts_type_get_state_length(GBgetLTStype(model_));
            int local_src[state_length];
            this->global_to_local(src, local_src);
            mcrl2::pbes_system::explorer::next_state_long(local_src, group, std::forward<callback>(f));
        }
    }

    template <typename callback>
    void next_state_all(int* const& src, callback&& f)
    {
        int state_length = lts_type_get_state_length(GBgetLTStype(model_));
        int local_src[state_length];
        this->global_to_local(src, local_src);
        mcrl2::pbes_system::explorer::next_state_all(local_src, std::forward<callback>(f));
    }

    void initial_state(int* state)
    {
        int state_length = lts_type_get_state_length(GBgetLTStype(model_));
        int local_state[state_length];
        mcrl2::pbes_system::explorer::initial_state(local_state);
        this->local_to_global(local_state, state);
    }

    int state_label(int label, int* const& s)
    {
        int state_length = lts_type_get_state_length(GBgetLTStype(model_));
        int local_s[state_length];
        this->global_to_local(s, local_s);
        std::string varname = this->get_string_value(local_s[0]);
        if (label==PG_PRIORITY)
        {
            int priority = this->get_info()->get_variable_priorities().at(varname);
            //std::clog << "var: " << varname << ", priority: " << priority << std::endl;
            return priority;
        }
        else if (label==PG_PLAYER)
        {
            lts_info::operation_type type = this->get_info()->get_variable_types().at(varname);
            return type==parity_game_generator::PGAME_AND ? PG_AND : PG_OR;
        }
        return 0;
    }
};

// initialisation outside class to avoid linking error
const int explorer::IDX_NOT_FOUND = -1;

struct pbes_state_cb
{
    model_t model;
    ltsmin::explorer* explorer;
    TransitionCB cb;
    void* ctx;
    size_t count;

    pbes_state_cb (model_t& model_, ltsmin::explorer* explorer_, TransitionCB cb_, void *ctx_)
        : model(model_), explorer(explorer_), cb(cb_), ctx(ctx_), count(0)
    {}

    void operator()(int* const& next_state,
                    int group = -1)
    {
        int state_length = lts_type_get_state_length(GBgetLTStype(model));
        int dst[state_length];
        explorer->local_to_global(next_state, dst);
        int* edge_labels = NULL;
        transition_info_t ti = GB_TI(edge_labels, group);
        cb(ctx, &ti, dst, NULL);
        count++;
    }

    size_t get_count() const
    {
        return count;
    }
};

} // namespace ltsmin

extern "C" {

static int reset_flag = 0;
static int always_split_flag = 0;

static void pbes_popt(poptContext con, enum poptCallbackReason reason,
                      const struct poptOption * opt, const char * arg,
                      void * data)
{
    (void)con;
    (void)opt;
    (void)arg;
    (void)data;
    switch (reason) {
    case POPT_CALLBACK_REASON_PRE:
        break;
    case POPT_CALLBACK_REASON_POST:
        {
            if (reset_flag) {
                Warning(info,"Reset flag is set.");
            }
            if (always_split_flag) {
                Warning(info,"Always split flag is set.");
            }

            GBregisterLoader("pbes", PBESloadGreyboxModel);
            if (mcrl2_verbosity > 0) {
                Warning(info, "increasing mcrl2 verbosity level by %d", mcrl2_verbosity);
                log::log_level_t log_level = static_cast<log::log_level_t>(static_cast<size_t>(log::mcrl2_logger::get_reporting_level()) + mcrl2_verbosity);
                log::mcrl2_logger::set_reporting_level(log_level);
            }
#ifdef DISABLE_JITTYC
            if (strcmp(mcrl2_rewriter, "jittyc") == 0) {
                Abort("The jittyc rewriter was disabled at compile time");
            }
#endif
            Warning(info,"PBES language module initialized");
            return;
        }
    case POPT_CALLBACK_REASON_OPTION:
        break;
    }
    Abort("unexpected call to pbes_popt");
}

struct poptOption pbes_options[] = {
     { NULL, 0, POPT_ARG_CALLBACK | POPT_CBFLAG_POST | POPT_CBFLAG_SKIPOPTION, (void*)pbes_popt, 0, NULL, NULL },
     { "reset" , 0 , POPT_ARG_NONE , &reset_flag, 0, "Indicate that unset parameters should be reset.","" },
     { "always-split" , 0 , POPT_ARG_NONE , &always_split_flag, 0, "Always use conjuncts and disjuncts as transition groups.","" },
     { "mcrl2-rewriter", 0, POPT_ARG_STRING|POPT_ARGFLAG_SHOW_DEFAULT, &mcrl2_rewriter, 0, "select mCRL2 rewriter: jittyc, jitty, ...", NULL },
     { "mcrl2-verbosity", 0, POPT_ARG_INT, &mcrl2_verbosity, 1, "increase mCRL2 verbosity", "INT" },
     POPT_TABLEEND };

typedef struct grey_box_context
{
    ltsmin::explorer* pbes_explorer;
}*gb_context_t;

static matrix_t sl_info;

int PBESgetTransitionsLong(model_t model, int group, int*src, TransitionCB cb,
                           void*context)
{
    //std::clog << "PBESgetTransitionsLong(model, group=" << group
    //        << ", src, cb, context)" << std::endl;
    gb_context_t ctx = (gb_context_t)GBgetContext(model);
    ltsmin::pbes_state_cb f(model, ctx->pbes_explorer, cb, context);
    ctx->pbes_explorer->next_state_long(src, group, f);
    size_t count = f.get_count();
    //std::clog << count << " successors." << std::endl;
    return count;
}

int PBESgetTransitionsAll(model_t model, int*src, TransitionCB cb,
                           void*context)
{
    //std::clog << "PBESgetTransitionsAll(model, src, cb, context)" << std::endl;
    gb_context_t ctx = (gb_context_t)GBgetContext(model);
    ltsmin::pbes_state_cb f(model, ctx->pbes_explorer, cb, context);
    ctx->pbes_explorer->next_state_all(src, f);
    size_t count = f.get_count();
    //std::clog << count << " successors." << std::endl;
    return count;
}

/*
 *
 */
int PBESgetStateLabelLong(model_t model, int label, int *s)
{
    gb_context_t ctx = (gb_context_t)GBgetContext(model);
    int result = ctx->pbes_explorer->state_label(label, s);
    //Warning(info, "PBESgetStateLabelLong: label = %d, result = %d.", label, result);
    return result;
}

/**
 * \brief Converts C++-style PBES_LTS_Type to C-style lts_type_t
 */
lts_type_t PBESgetLTSType(const lts_type& t)
{
    assert((size_t)t.get_state_length()==t.get_state_names().size());
    lts_type_t type = lts_type_create();
    lts_type_set_state_length(type, t.get_state_length());
    for (size_t i = 0; i < t.get_state_names().size(); i++) {
        lts_type_set_state_name(type, i, t.get_state_names().at(i).c_str());
        lts_type_set_state_type(type, i, t.get_state_types().at(i).c_str());
    }
    lts_type_set_state_label_count(type, t.get_number_of_state_labels());
    for (size_t i = 0; i < t.get_number_of_state_labels(); i++) {
        lts_type_set_state_label_name(type, i,
                                      t.get_state_labels().at(i).c_str());
        lts_type_set_state_label_type(type, i,
                                      t.get_state_label_types().at(i).c_str());
    }
    lts_type_set_edge_label_count(type, t.get_number_of_edge_labels());
    for (size_t i = 0; i < t.get_number_of_edge_labels(); i++) {
        lts_type_set_edge_label_name(type, i,
                                     t.get_edge_labels().at(i).c_str());
        lts_type_set_edge_label_type(type, i,
                                     t.get_edge_label_types().at(i).c_str());
    }
    return type;
}

ltsmin::explorer* pbes_explorer;

void
PBESexit ()
{
    delete pbes_explorer;
}

void PBESloadGreyboxModel(model_t model, const char*name)
{
    // check file exists
    struct stat st;
    if (stat(name, &st) != 0)
        Abort ("File does not exist: %s", name);

    //std::clog << "PBESloadGreyboxModel(model, " << name << "):" << std::endl;
    gb_context_t ctx = (gb_context_t)RTmalloc(sizeof(struct grey_box_context));
    GBsetContext(model, ctx);

    log::log_level_t log_level = log_active(infoLong) ? log::verbose : log::error;
    log::mcrl2_logger::set_reporting_level(log_level);

    bool reset = (reset_flag==1);
    bool always_split = (always_split_flag==1);
    pbes_explorer = new ltsmin::explorer(model, std::string(name), std::string(mcrl2_rewriter), reset, always_split);
    ctx->pbes_explorer = pbes_explorer;
    lts_info* info = pbes_explorer->get_info();
    lts_type_t ltstype = PBESgetLTSType(info->get_lts_type());
    GBsetLTStype(model, ltstype);
    ctx->pbes_explorer->init_group_id_cache();
    int state_length = lts_type_get_state_length(ltstype);


    int state[state_length];
    pbes_explorer->initial_state(state);
    GBsetInitialState(model, state);

    ctx->pbes_explorer = pbes_explorer;

    int num_rows = info->get_number_of_groups();
    std::map<int,std::vector<bool> > matrix = info->get_dependency_matrix();
    std::map<int,std::vector<bool> > read_matrix = info->get_read_matrix();
    std::map<int,std::vector<bool> > write_matrix = info->get_write_matrix();
    // dependency matrix dm
    matrix_t * p_dm_info = (matrix_t*)RTmalloc(sizeof(matrix_t));
    dm_create(p_dm_info, num_rows, state_length);
    // read matrix rm
    matrix_t * p_rm_info = (matrix_t*)RTmalloc(sizeof(matrix_t));
    dm_create(p_rm_info, num_rows, state_length);
    // write matrix wm
    matrix_t * p_wm_info = (matrix_t*)RTmalloc(sizeof(matrix_t));
    dm_create(p_wm_info, num_rows, state_length);
    for (int i = 0; i < num_rows; i++) {
        std::vector<bool> dep_row = matrix.at(i);
        std::vector<bool> read_row = read_matrix.at(i);
        std::vector<bool> write_row = write_matrix.at(i);
        assert(dep_row.size()==(unsigned int)state_length);
        assert(read_row.size()==(unsigned int)state_length);
        assert(write_row.size()==(unsigned int)state_length);
        int j = 0;
        std::vector<bool>::const_iterator read_row_iterator = read_row.begin();
        std::vector<bool>::const_iterator write_row_iterator = write_row.begin();
        for(std::vector<bool>::const_iterator row_iterator = dep_row.begin(); row_iterator != dep_row.end(); ++row_iterator) {
            // if (dependency between transition group j and state vector part i)
            if (*row_iterator)
            {
                dm_set(p_dm_info, i, j);
            }
            if (*read_row_iterator)
            {
                dm_set(p_rm_info, i, j);
            }
            if (*write_row_iterator)
            {
                dm_set(p_wm_info, i, j);
            }
            if (read_row_iterator != read_row.end())
            {
                ++read_row_iterator;
            }
            if (write_row_iterator != write_row.end())
            {
                ++write_row_iterator;
            }
            j++;
        }
    }
    GBsetDMInfo(model, p_dm_info);
    GBsetDMInfoRead(model, p_rm_info);
    GBsetDMInfoMustWrite(model, p_wm_info);

    int num_state_labels = info->get_lts_type().get_number_of_state_labels();
    dm_create(&sl_info, num_state_labels, state_length);
    for (int i = 0; i < num_state_labels; i++) {
        dm_set(&sl_info, i, 0);
    }
    GBsetStateLabelInfo(model, &sl_info);

    GBsetNextStateLong(model, PBESgetTransitionsLong);
    GBsetNextStateAll(model, PBESgetTransitionsAll);

    GBsetStateLabelLong(model, PBESgetStateLabelLong);

    //std::clog << "-- end of PBESloadGreyboxModel." << std::endl;
    atexit(PBESexit);
}

} // end of extern "C"

