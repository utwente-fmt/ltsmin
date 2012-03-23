/**
 \file pbes-greybox.cpp
 */

#include <config.h>

#include <mcrl2/atermpp/aterm_init.h>
#include <mcrl2/pbes/pbes.h>
#include <mcrl2/pbes/detail/pbes_greybox_interface.h>
#include <mcrl2/pbes/pbes_explorer.h>
#include <mcrl2/pbes/detail/ppg_rewriter.h>


extern "C" {

#include <popt.h>
#include "pbes-greybox.h"
#include <runtime.h>

} // end of extern "C"

#ifdef MCRL2_JITTYC_AVAILABLE
static std::string mcrl2_rewriter_strategy = "jittyc";
#else
static std::string mcrl2_rewriter_strategy = "jitty";
#endif

using namespace mcrl2;
using namespace mcrl2::core;
using namespace mcrl2::data;


namespace ltsmin
{

class explorer : public mcrl2::pbes_system::explorer {
private:
    model_t model_;
    std::vector<std::map<int,int> > local2global_maps;
    std::vector<std::map<int,int> > global2local_maps;

public:
    explorer(model_t& model, const std::string& filename, const std::string& rewrite_strategy, bool reset = false) :
        mcrl2::pbes_system::explorer(filename, rewrite_strategy, reset),
        model_(model)
    {
        for (int i = 0; i <= get_info()->get_lts_type().get_number_of_state_types(); i++) {
            std::map<int,int> local2global_map;
            local2global_maps.push_back(local2global_map);
            std::map<int,int> global2local_map;
            global2local_maps.push_back(global2local_map);
        }
    }

    ~explorer()
    {}

    int put_chunk(int type_no, std::string value)
    {
        char* s = new char[value.length() + 1];
        value.copy(s, std::string::npos);
        s[value.length()] = 0;
        int index = GBchunkPut(model_, type_no, chunk_str(s));
        delete[] s;
        return index;
    }

    void local_to_global(int* const& local, int* global)
    {
        //gb_context_t ctx = (gb_context_t)GBgetContext(model);
        int state_length = lts_type_get_state_length(GBgetLTStype(model_));
        int type_no;
        std::map<int,int> local2global_map;
        for(int i=0; i<state_length; i++)
        {
            type_no = lts_type_get_state_typeno(GBgetLTStype(model_), i);
            //std::clog << "[" << getpid() << "] local2Global i = " << i << " type_no = " << type_no << " local[i] = " << local[i] << std::endl;
            local2global_map = local2global_maps.at(type_no);
            std::map<int,int>::const_iterator it = local2global_map.find(local[i]);
            if(it == local2global_map.end())
            {
                std::string s = this->get_value(type_no, local[i]);
                global[i] = this->put_chunk(type_no, s);
                local2global_map.insert(std::make_pair(local[i],global[i]));
                global2local_maps.at(type_no).insert(std::make_pair(global[i],local[i]));
            }
            else {
                global[i] = it->second;
            }
        }
    }

    std::string get_chunk(int type_no, int index)
    {
        chunk c = GBchunkGet(model_, type_no, index);
        if (c.len == 0) {
            Fatal(1, error, "lookup of %d failed", index);
        }
        char s[c.len + 1];
        for (unsigned int i = 0; i < c.len; i++) {
            s[i] = c.data[i];
        }
        s[c.len] = 0;
        std::string value = std::string(s);
        return value;
    }

    void global_to_local(int* const& global, int* local)
    {
        //gb_context_t ctx = (gb_context_t)GBgetContext(model);
        //ltsmin::explorer* pbes_explorer = ctx->pbes_explorer;
        int state_length = lts_type_get_state_length(GBgetLTStype(model_));
        int type_no;
        std::map<int,int> global2local_map;
        for(int i=0; i<state_length; i++)
        {
            type_no = lts_type_get_state_typeno(GBgetLTStype(model_), i);
            //std::clog << "globalToLocal i = " << i << " type_no = " << type_no << " global[i] = " << global[i] << std::endl;
            global2local_map = global2local_maps.at(type_no);
            std::map<int,int>::const_iterator it = global2local_map.find(global[i]);
            if(it == global2local_map.end())
            {
                std::string s = this->get_chunk(type_no, global[i]);
                local[i] = this->get_index(type_no, s);
                global2local_map.insert(std::make_pair(global[i],local[i]));
                local2global_maps.at(type_no).insert(std::make_pair(local[i],global[i]));
            }
            else {
                local[i] = it->second;
            }
        }
    }

    template <typename callback>
    void next_state_long(int* const& src, std::size_t group, callback& f)
    {
        int state_length = lts_type_get_state_length(GBgetLTStype(model_));
        int local_src[state_length];
        this->global_to_local(src, local_src);
        mcrl2::pbes_system::explorer::next_state_long(local_src, group, f);
    }

    template <typename callback>
    void next_state_all(int* const& src, callback& f)
    {
        int state_length = lts_type_get_state_length(GBgetLTStype(model_));
        int local_src[state_length];
        this->global_to_local(src, local_src);
        mcrl2::pbes_system::explorer::next_state_all(local_src, f);
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
        if (label==0)
        {
            int priority = this->get_info()->get_variable_priorities().at(varname);
            return priority;
        }
        else if (label==1)
        {
            lts_info::operation_type type = this->get_info()->get_variable_types().at(varname);
            return type==parity_game_generator::PGAME_AND ? 1 : 0;
        }
        return 0;
    }
};

struct pbes_state_cb
{
    model_t model;
    ltsmin::explorer* explorer;
    TransitionCB& cb;
    void* ctx;
    size_t count;

    pbes_state_cb (model_t& model_, ltsmin::explorer* explorer_, TransitionCB& cb_, void *ctx_)
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
        cb(ctx, &ti, dst);
        count++;
    }

    size_t get_count() const
    {
        return count;
    }
};

} // namespace ltsmin

extern "C" {

static int debug_flag = 0;
static int reset_flag = 0;

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
            Warning(debug,"pbes init");
            Warning (debug,"ATerm init");
            MCRL2_ATERMPP_INIT_(0,0,RTstackBottom());
            if (reset_flag) {
                Warning(info,"Reset flag is set.");
            }
            if (debug_flag) {
                Warning(info,"Debug flag is set.");
                log::mcrl2_logger::set_reporting_level(log::debug1);
            }
            GBregisterLoader("pbes", PBESloadGreyboxModel);
            Warning(info,"PBES language module initialized");
            return;
        }
    case POPT_CALLBACK_REASON_OPTION:
        break;
    }
    Fatal(1,error,"unexpected call to pbes_popt");
}

struct poptOption pbes_options[] = {
     { NULL, 0, POPT_ARG_CALLBACK | POPT_CBFLAG_POST | POPT_CBFLAG_SKIPOPTION, (void*)pbes_popt, 0, NULL, NULL },
     { "reset" , 0 , POPT_ARG_NONE , &reset_flag, 0, "Indicate that unset parameters should be reset.","" },
     { "debug" , 0 , POPT_ARG_NONE , &debug_flag, 0, "Enable debug output.","" },
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
    //Warning(info, "PBESgetStateLabelLong: label = %d", label);
    gb_context_t ctx = (gb_context_t)GBgetContext(model);
    return ctx->pbes_explorer->state_label(label, s);
}

/**
 * \brief Converts C++-style PBES_LTS_Type to C-style lts_type_t
 */
lts_type_t PBESgetLTSType(const lts_type& t)
{
    assert(t.get_state_length()==t.get_state_names().size());
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

void
PBESexit ()
{}

void PBESloadGreyboxModel(model_t model, const char*name)
{
    //std::clog << "PBESloadGreyboxModel(model, " << name << "):" << std::endl;
    gb_context_t ctx = (gb_context_t)RTmalloc(sizeof(struct grey_box_context));
    GBsetContext(model, ctx);

    log::log_level_t log_level = debug_flag ? log::debug1 : log::quiet;
    log::mcrl2_logger::set_reporting_level(log_level);

    bool reset = (reset_flag==1);
    ltsmin::explorer* pbes_explorer = new ltsmin::explorer(model, std::string(name), mcrl2_rewriter_strategy, reset);
    ctx->pbes_explorer = pbes_explorer;
    lts_info* info = pbes_explorer->get_info();
    lts_type_t ltstype = PBESgetLTSType(info->get_lts_type());
    GBsetLTStype(model, ltstype);
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
    GBsetDMInfoWrite(model, p_wm_info);

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

