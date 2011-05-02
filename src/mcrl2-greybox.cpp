#include <config.h>
#include <algorithm>
#include <iterator>
#include <iostream>
#include <memory>
#include <string>
#include <set>
#include <vector>
#include <stack>

#include "mcrl2/lps/detail/instantiate_global_variables.h"
#include "mcrl2/lps/nextstate.h"
#include "mcrl2/lps/nextstate/standard.h"
#include "mcrl2/lps/specification.h"
#include "mcrl2/data/variable.h"
#include "mcrl2/data/selection.h"
#include "mcrl2/data/find.h"
#include "mcrl2/atermpp/set.h"
#include "mcrl2/core/print.h"
#include "dm/dm.h"

class group_information {

  private:

    mcrl2::lps::specification     m_model;
    std::vector< std::vector< size_t > > m_group_indices;
    std::vector< std::vector< size_t > > m_group_read_indices;
    std::vector< std::vector< size_t > > m_group_write_indices;

  private:

    void gather(mcrl2::lps::specification const& l);

  public:

    /**
     * \brief constructor from an mCRL2 lps
     **/
    group_information(mcrl2::lps::specification const& l) : m_model(l) {
      gather(l);
    }

    /**
     * \brief The number of groups (summands in the LPS)
     * \return lps::specification(l).summands().size()
     **/
    inline size_t number_of_groups() const {
      return m_group_indices.size();
    }

    inline size_t number_of_parameters() const {
      return m_model.process().process_parameters().size();
    }

    /**
     * \brief Indices of process parameters that influence event or next state of a summand
     * \param[in] index the selected summand
     * \returns reference to a vector of indices of parameters
     **/
    inline std::vector< size_t > const& get_group(size_t index) const {
      return m_group_indices[index];
    }

    /**
     * \brief Indices of process parameters that influence event or next state of a summand by being read
     * \param[in] index the selected summand
     * \returns reference to a vector of indices of parameters
     **/
    inline std::vector< size_t > const& get_read_group(size_t index) const {
      return m_group_read_indices[index];
    }

    /**
     * \brief Indices of process parameters that influence event or next state of a summand by being written
     * \param[in] index the selected summand
     * \returns reference to a vector of indices of parameters
     **/
    inline std::vector< size_t > const& get_write_group(size_t index) const {
      return m_group_write_indices[index];
    }
};

void group_information::gather(mcrl2::lps::specification const& l) {
  using namespace mcrl2;

  using data::find_variables;
  using data::variable;

  lps::linear_process const& specification = l.process();

  // the set with process parameters
  std::set< variable > parameters = find_variables(specification.process_parameters());

  // the list of summands
  std::vector< lps::action_summand > const& summands = specification.action_summands();

  m_group_indices.resize(summands.size());
  m_group_read_indices.resize(summands.size());
  m_group_write_indices.resize(summands.size());

  for (std::vector< lps::action_summand >::const_iterator i = summands.begin(); i != summands.end(); ++i) {
    std::set< variable > used_read_variables;
    std::set< variable > used_write_variables;

    find_variables(i->condition(), std::inserter(used_read_variables, used_read_variables.end()));
    lps::find_variables(i->multi_action().actions(), std::inserter(used_read_variables, used_read_variables.end()));

    if (i->multi_action().has_time()) {
      data::find_variables(i->multi_action().time(), std::inserter(used_read_variables, used_read_variables.end()));
    }

    data::assignment_list assignments(i->assignments());

    for (data::assignment_list::const_iterator j = assignments.begin(); j != assignments.end(); ++j) {
      if(j->lhs() != j->rhs()) {
        find_variables(j->lhs(), std::inserter(used_write_variables, used_write_variables.end()));
        find_variables(j->rhs(), std::inserter(used_read_variables, used_read_variables.end()));
      }
    }

    // process parameters used in condition or action of summand
    std::set< variable > used_read_parameters;
    std::set< variable > used_write_parameters;

    std::set_intersection(used_read_variables.begin(),
                          used_read_variables.end(),
                          parameters.begin(),
                          parameters.end(),
                          std::inserter(used_read_parameters,
                                        used_read_parameters.begin()));
    std::set_intersection(used_write_variables.begin(),
                          used_write_variables.end(),
                          parameters.begin(),
                          parameters.end(),
                          std::inserter(used_write_parameters,
                                        used_write_parameters.begin()));

    std::vector< variable > parameters_list = atermpp::convert< std::vector< variable > >(specification.process_parameters());

    for (std::vector< variable >::const_iterator j = parameters_list.begin(); j != parameters_list.end(); ++j) {
        if (!used_read_parameters.empty() && used_read_parameters.find(*j) != used_read_parameters.end()) {
        m_group_read_indices[i - summands.begin()].push_back(j - parameters_list.begin());
        m_group_indices[i - summands.begin()].push_back(j - parameters_list.begin());
      }
      if (!used_read_parameters.empty() && used_write_parameters.find(*j) != used_write_parameters.end()) {
        m_group_write_indices[i - summands.begin()].push_back(j - parameters_list.begin());
        m_group_indices[i - summands.begin()].push_back(j - parameters_list.begin());
      }
    }
  }
}

extern "C" {

#include <popt.h>

/// We have to define CONFIG_H_ due to a problem with double defines.
#define CONFIG_H_

/// We have to define CONFIG_H_ due to a problem with double defines.
#define CONFIG_H_
#include "mcrl2-greybox.h"
#include "runtime.h"
#include "at-map.h"

static void WarningHandler(const char *format, va_list args) {
	FILE* f=log_get_stream(info);
	if (f) {
		fprintf(f,"MCRL2 grey box: ");
		ATvfprintf(f, format, args);
		fprintf(f,"\n");
	}
}

static void ErrorHandler(const char *format, va_list args) {
	FILE* f=log_get_stream(error);
	if (f) {
		fprintf(f,"MCRL2 grey box: ");
		ATvfprintf(f, format, args);
		fprintf(f,"\n");
	}
	Fatal(1,error,"ATerror");
	exit(1);
}

#ifdef MCRL2_INNERC_AVAILABLE
static char const* mcrl2_args="--rewriter=jittyc";
static mcrl2::data::rewriter::strategy mcrl2_rewriter=mcrl2::data::rewriter::jitty_compiling;
#else
static char const* mcrl2_args="--rewriter=jitty";
static RewriteStrategy mcrl2_rewriter=mcrl2::data::rewriter::jitty;
#endif

static void mcrl2_popt(poptContext con,
		enum poptCallbackReason reason,
                            const struct poptOption * opt,
                             const char * arg, void * data){
	(void)con;(void)opt;(void)arg;(void)data;
	switch(reason){
	case POPT_CALLBACK_REASON_PRE:
		break;
	case POPT_CALLBACK_REASON_POST: {
		Warning(debug,"mcrl2 init");
		int argc;
		char **argv;
		RTparseOptions(mcrl2_args,&argc,&argv);
		Warning(debug,"ATerm init");
		ATinit(argc, argv, (ATerm*) RTstackBottom());
		ATsetWarningHandler(WarningHandler);
		ATsetErrorHandler(ErrorHandler);
		char*rewriter=NULL;
		struct poptOption options[]={
			{ "rewriter", 0 , POPT_ARG_STRING , &rewriter , 0 , "select rewriter" , NULL },
			POPT_TABLEEND
		};
		Warning(debug,"options");
		poptContext optCon=poptGetContext(NULL, argc,(const char **) argv, options, 0);
		int res=poptGetNextOpt(optCon);
		if (res != -1 || poptPeekArg(optCon)!=NULL){
			Fatal(1,error,"Bad mcrl2 options: %s",mcrl2_args);
		}
		poptFreeContext(optCon);
		if (rewriter) {
			if (!strcmp(rewriter,"jitty")){
				mcrl2_rewriter=mcrl2::data::rewriter::jitty;
			} else if (!strcmp(rewriter,"jittyc")){
				mcrl2_rewriter=mcrl2::data::rewriter::jitty_compiling;
			} else if (!strcmp(rewriter,"inner")){
				mcrl2_rewriter=mcrl2::data::rewriter::innermost;
			} else if (!strcmp(rewriter,"innerc")){
				mcrl2_rewriter=mcrl2::data::rewriter::innermost_compiling;
			} else {
				Fatal(1,error,"unrecognized rewriter: %s (jitty, jittyc, inner and innerc supported)",rewriter);
			}
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

struct poptOption mcrl2_options[]= {
	{ NULL, 0 , POPT_ARG_CALLBACK|POPT_CBFLAG_POST|POPT_CBFLAG_SKIPOPTION , (void*)mcrl2_popt , 0 , NULL , NULL},
	{ "mcrl2" , 0 , POPT_ARG_STRING|POPT_ARGFLAG_SHOW_DEFAULT , &mcrl2_args , 0, "Pass options to the mcrl2 library.","<mcrl2 options>" },
	POPT_TABLEEND
};

void MCRL2initGreybox(int argc,char *argv[],void* stack_bottom){
	ATinit(argc, argv, (ATerm*) stack_bottom);
	ATsetWarningHandler(WarningHandler);
	ATsetErrorHandler(ErrorHandler);
}

typedef struct grey_box_context {
	int atPars;
	int atGrps;
	NextState* explorer;
	legacy_rewriter* rewriter_object;
	mcrl2::data::detail::Rewriter* rewriter;
    mcrl2::data::enumerator_factory< mcrl2::data::classic_enumerator< > > *enumerator_factory;
	AFun StateFun;
	group_information *info;
	ATerm s0;
	at_map_t termmap;
	at_map_t actionmap;
} *mcrl2_model_t;

static matrix_t sl_info;


static char global_label[65536];


static char* print_label(void*arg,ATerm act){
	(void)arg;
	std::string s=mcrl2::core::pp(act);
	const char* cs=s.c_str();
	if(strlen(cs)>=65536){
		Fatal(1,error,"global_label overflow");
	}
	strcpy(global_label,cs);
	return global_label;
}

static char* print_term(void*arg,ATerm t){
	mcrl2::data::detail::Rewriter* rw=(mcrl2::data::detail::Rewriter*)arg;
	return ATwriteToString((ATerm)rw->fromRewriteFormat(t));
}

static ATerm parse_term(void *arg,char*str){
	mcrl2::data::detail::Rewriter* rw=(mcrl2::data::detail::Rewriter*)arg;
	return rw->toRewriteFormat((ATermAppl)ATreadFromString(str));
}

static int MCRL2getTransitionsLong(model_t m,int group,int*src,TransitionCB cb,void*context){
	struct grey_box_context *model=(struct grey_box_context*)GBgetContext(m);
	ATerm src_term[model->atPars];
	for(int i=0;i<model->atPars;i++) {
		src_term[i]=ATfindTerm(model->termmap,src[i]);
	}
	ATerm src_state=(ATerm)ATmakeApplArray(model->StateFun,src_term);
	//src_state=(ATerm)model->explorer->parseStateVector((ATermAppl)src_state);
	std::auto_ptr< NextStateGenerator > generator(model->explorer->getNextStates(src_state, group));
	ATerm     state;
	ATermAppl transition;
	int dst[model->atPars];
	int pp_lbl[1];
	int count=0;
	while(generator->next(&transition, &state)){
		//state=(ATerm)model->explorer->makeStateVector(state);
		for(int i=0;i<model->atPars;i++) {
			dst[i]=ATfindIndex(model->termmap,ATgetArgument(state,i));
		}
		pp_lbl[0]=ATfindIndex(model->actionmap,(ATerm)transition);
        transition_info_t ti = {pp_lbl, group};
		cb(context,&ti,dst);
		count++;
	}
	return count;
}

static int MCRL2getTransitionsAll(model_t m,int*src,TransitionCB cb,void*context){
	struct grey_box_context *model=(struct grey_box_context*)GBgetContext(m);
	ATerm src_term[model->atPars];
	for(int i=0;i<model->atPars;i++) {
		src_term[i]=ATfindTerm(model->termmap,src[i]);
	}
	ATerm src_state=(ATerm)ATmakeApplArray(model->StateFun,src_term);
	//src_state=(ATerm)model->explorer->parseStateVector((ATermAppl)src_state);
	std::auto_ptr< NextStateGenerator > generator(model->explorer->getNextStates(src_state));
	ATerm     state;
	ATermAppl transition;
	int dst[model->atPars];
	int pp_lbl[1];
	int count=0;
	while(generator->next(&transition, &state)){
		//state=(ATerm)model->explorer->makeStateVector(state);
		for(int i=0;i<model->atPars;i++) {
			dst[i]=ATfindIndex(model->termmap,ATgetArgument(state,i));
		}
		pp_lbl[0]=ATfindIndex(model->actionmap,(ATerm)transition);
        transition_info_t ti = {pp_lbl, -1};
		cb(context,&ti,dst);
		count++;
	}
	return count;
}

void MCRL2loadGreyboxModel(model_t m,const char*model_name){
	struct grey_box_context *ctx=(struct grey_box_context*)RTmalloc(sizeof(struct grey_box_context));
	GBsetContext(m,ctx);

	using namespace mcrl2;
	using namespace mcrl2::lps;

	lps::specification model;

        try {
          model.load(model_name);

          lps::detail::instantiate_global_variables(model);
        }
        catch (...) {
          Fatal(1,error,"could not read specification from %s",model_name);
        }

	model.process().deadlock_summands().clear();

	int state_length=model.process().process_parameters().size();
	lts_type_t ltstype=lts_type_create();
	lts_type_set_state_length(ltstype,state_length);
	for(int i=0;i<state_length;i++) {
            char name[64];
            snprintf(name,sizeof name,"x%d",i);
            lts_type_set_state_name(ltstype,i,name);
            lts_type_set_state_type(ltstype,i,"leaf");
	}
	lts_type_set_edge_label_count(ltstype,1);
	lts_type_set_edge_label_name(ltstype,0,"action");
	lts_type_set_edge_label_type(ltstype,0,"action");
	GBsetLTStype(m,ltstype);

    ctx->rewriter_object = new legacy_rewriter(model.data(), mcrl2::data::used_data_equation_selector(model.data(), mcrl2::lps::find_function_symbols(model), model.global_variables()), mcrl2_rewriter);
    ctx->rewriter = &ctx->rewriter_object->get_rewriter();

    ctx->enumerator_factory = new mcrl2::data::enumerator_factory< mcrl2::data::classic_enumerator< > >(model.data(), *(ctx->rewriter_object));

	// Note the second argument that specifies that don't care variables are not treated specially
	ctx->explorer = createNextState(model, *(ctx->enumerator_factory), false);
	ctx->info=new group_information(model);
	ctx->termmap=ATmapCreate(m,lts_type_add_type(ltstype,"leaf",NULL),ctx->rewriter,print_term,parse_term);
	ctx->actionmap=ATmapCreate(m,lts_type_add_type(ltstype,"action",NULL),ctx->rewriter,print_label,NULL);
	ctx->atPars=state_length;
	ctx->atGrps=model.process().summand_count();
	ATerm s0=ctx->explorer->getInitialState();
	//s0=(ATerm)ctx->explorer->makeStateVector(s0);
	ctx->StateFun=ATgetAFun(s0);
	int temp[state_length];
	for(int i=0;i<state_length;i++) {
		temp[i]=ATfindIndex(ctx->termmap,ATgetArgument(s0,i));
	}
	GBsetInitialState(m,temp);

	matrix_t * p_dm_info = (matrix_t*)RTmalloc(sizeof(matrix_t));
	matrix_t * p_dm_read_info = (matrix_t*)RTmalloc(sizeof(matrix_t));
	matrix_t * p_dm_write_info = (matrix_t*)RTmalloc(sizeof(matrix_t));
	dm_create(p_dm_info, ctx->atGrps, state_length);
	dm_create(p_dm_read_info, ctx->atGrps, state_length);
	dm_create(p_dm_write_info, ctx->atGrps, state_length);

	for(int i=0; i < dm_nrows(p_dm_info); i++) {
            std::vector< size_t > const & vec = ctx->info->get_group(i);
            for(size_t j=0; j < vec.size(); j++)
                dm_set (p_dm_info, i, vec[j]);
            std::vector< size_t > const & vec_r = ctx->info->get_read_group(i);
            for(size_t j=0; j < vec_r.size(); j++)
                dm_set (p_dm_read_info, i, vec_r[j]);
            std::vector< size_t > const & vec_w = ctx->info->get_write_group(i);
            for(size_t j=0; j < vec_w.size(); j++)
                dm_set (p_dm_write_info, i, vec_w[j]);
	}

	GBsetDMInfo(m, p_dm_info);
	GBsetDMInfoRead(m, p_dm_read_info);
	GBsetDMInfoWrite(m, p_dm_write_info);
	dm_create(&sl_info, 0, state_length);
	GBsetStateLabelInfo(m,&sl_info);
	GBsetNextStateLong(m,MCRL2getTransitionsLong);
	GBsetNextStateAll(m,MCRL2getTransitionsAll);
}


}// end extern "C".


