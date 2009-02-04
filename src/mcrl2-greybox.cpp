#include <algorithm>
#include <iterator>
#include <iostream>
#include <memory>
#include <string>
#include <set>
#include <vector>
#include <stack>

#include "mcrl2/lps/nextstate.h"
#include "mcrl2/lps/specification.h"
#include "mcrl2/lps/mcrl22lps.h"
#include "mcrl2/atermpp/set.h"
#include "mcrl2/core/print.h"
#include "mcrl2/lps/data_elimination.h"

class group_information {

  private:

    mcrl2::lps::specification const&     m_model;

    std::vector< std::vector< size_t > > m_group_indices;

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
};

void group_information::gather(mcrl2::lps::specification const& l) {
  using namespace mcrl2;

  using data::find_all_data_variables;
  using data::data_variable;

  struct local {
    static void add_used_variables(std::set< data_variable >& r, std::set< data_variable > const& c) {
      r.insert(c.begin(), c.end());
    }
  };

  lps::linear_process specification(l.process());

  // the set with process parameters
  std::set< data_variable > parameters = find_all_data_variables(specification.process_parameters());

  // the list of summands
  std::vector< lps::summand > summands(specification.summands().begin(), specification.summands().end());

  m_group_indices.resize(summands.size());

  for (std::vector< lps::summand >::const_iterator i = summands.begin(); i != summands.end(); ++i) {
    std::set< data_variable > used_variables;

    local::add_used_variables(used_variables, find_all_data_variables(i->condition()));
    local::add_used_variables(used_variables, find_all_data_variables(i->actions()));

    if (i->has_time()) {
      local::add_used_variables(used_variables, find_all_data_variables(i->time()));
    }

    data::data_assignment_list assignments(i->assignments());

    for (data::data_assignment_list::const_iterator j = assignments.begin(); j != assignments.end(); ++j) {
      if(j->lhs() != j->rhs()) {
        local::add_used_variables(used_variables, find_all_data_variables(j->lhs()));
        local::add_used_variables(used_variables, find_all_data_variables(j->rhs()));
      }
    }

    // process parameters used in condition or action of summand
    std::set< data_variable > used_parameters;

    std::set_intersection(used_variables.begin(), used_variables.end(),
                          parameters.begin(), parameters.end(), std::inserter(used_parameters, used_parameters.begin()));

    std::vector< data_variable > parameters_list(specification.process_parameters().begin(), specification.process_parameters().end());

    for (std::vector< data_variable >::const_iterator j = parameters_list.begin(); j != parameters_list.end(); ++j) {
      if (used_parameters.find(*j) != used_parameters.end()) {
        m_group_indices[i - summands.begin()].push_back(j - parameters_list.begin());
      }
    }
  }
}

mcrl2::lps::specification convert(mcrl2::lps::specification const& l) {
  mcrl2::lps::linear_process lp(l.process());

  mcrl2::lps::summand_list summands;

  for (mcrl2::lps::non_delta_summand_list::iterator i = lp.non_delta_summands().begin(); i != lp.non_delta_summands().end(); ++i) {
    summands = push_front(summands, *i);
  }

  summands = reverse(summands);

  return mcrl2::lps::specification(l.data(), l.action_labels(),
            mcrl2::lps::linear_process(lp.free_variables(), lp.process_parameters(), summands),
               l.initial_process());
}


extern "C" {

#include "mcrl2-greybox.h"
/// We have to define CONFIG_H_ due to a problem with double defines.
#define CONFIG_H_
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



void MCRL2initGreybox(int argc,char *argv[],void* stack_bottom){
	ATinit(argc, argv, (ATerm*) stack_bottom);
	ATsetWarningHandler(WarningHandler);
	ATsetErrorHandler(ErrorHandler);
}

typedef struct grey_box_context {
	int atPars;
	int atGrps;
	NextState* explorer;
	Rewriter* rewriter;
	AFun StateFun;
	group_information *info;
	ATerm s0;
	at_map_t termmap;
	at_map_t actionmap;
} *mcrl2_model_t;

static struct state_info s_info={0,NULL,NULL};


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
	Rewriter* rw=(Rewriter*)arg;
	return ATwriteToString((ATerm)rw->fromRewriteFormat(t));
}

static ATerm parse_term(void *arg,char*str){
	Rewriter* rw=(Rewriter*)arg;
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
		cb(context,pp_lbl,dst);
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
		cb(context,pp_lbl,dst);
		count++;
	}
	return count;
}



static char const_action[7]="action";
static char* edge_name[1]={const_action};
static int edge_type[1]={1};
static char const_leaf[5]="leaf";
static char* MCRL_types[2]={const_leaf,const_action};

void MCRL2loadGreyboxModel(model_t m,char*model_name){
	struct grey_box_context *ctx=(struct grey_box_context*)RTmalloc(sizeof(struct grey_box_context));
	GBsetContext(m,ctx);



	using namespace mcrl2;
	using namespace mcrl2::lps;

	ATermAppl Spec=(ATermAppl)ATreadFromNamedFile(model_name);
	if (!Spec) {
		Fatal(1,error,"could not read specification from %s",model_name);
	}
	Warning(info,"removing unused parts of the data specification.");
	Spec = removeUnusedData(Spec);

	lps::specification model(Spec);

//	model.load(model_name);
	model = convert(model);
	

	lts_struct_t ltstype=(lts_struct_t)RTmalloc(sizeof(struct lts_structure_s));
	ltstype->state_length=model.process().process_parameters().size();
	ltstype->visible_count=0;
	ltstype->visible_indices=NULL;
	ltstype->visible_name=NULL;
	ltstype->visible_type=NULL;
	ltstype->state_labels=0;
	ltstype->state_label_name=NULL;
	ltstype->state_label_type=NULL;
	ltstype->edge_labels=1;
	ltstype->edge_label_name=edge_name;
	ltstype->edge_label_type=edge_type;
	ltstype->type_count=2;
	ltstype->type_names=MCRL_types;
	GBsetLTStype(m,ltstype);



	// Note the second argument that specifies that don't care variables are not treated specially
	ctx->explorer = createNextState(model, false, GS_STATE_VECTOR,GS_REWR_JITTYC);
	ctx->rewriter = ctx->explorer->getRewriter();
	ctx->info=new group_information(model);
	ctx->termmap=ATmapCreate(m,0,ctx->rewriter,print_term,parse_term);
	ctx->actionmap=ATmapCreate(m,1,ctx->rewriter,print_label,NULL);

	ctx->atPars=model.process().process_parameters().size();
	ctx->atGrps=model.process().summands().size();
	ATerm s0=ctx->explorer->getInitialState();
	//s0=(ATerm)ctx->explorer->makeStateVector(s0);
	ctx->StateFun=ATgetAFun(s0);
	int temp[ltstype->state_length];
	for(int i=0;i<ltstype->state_length;i++) {
		temp[i]=ATfindIndex(ctx->termmap,ATgetArgument(s0,i));
	}
	GBsetInitialState(m,temp);

	edge_info_t e_info=(edge_info_t)RTmalloc(sizeof(struct edge_info));
	e_info->groups=ctx->atGrps;
	e_info->length=(int*)RTmalloc(e_info->groups*sizeof(int));
	e_info->indices=(int**)RTmalloc(e_info->groups*sizeof(int*));
	for(int i=0;i<e_info->groups;i++){
		std::vector< size_t > const & vec = ctx->info->get_group(i);
		e_info->length[i]=vec.size();
		e_info->indices[i]=(int*)RTmalloc(vec.size()*sizeof(int));
		for(int j=0;j<vec.size();j++) e_info->indices[i][j]=vec[j];
	}
	GBsetEdgeInfo(m,e_info);
	GBsetStateInfo(m,&s_info);
	GBsetNextStateLong(m,MCRL2getTransitionsLong);
	GBsetNextStateAll(m,MCRL2getTransitionsAll);
}


}// end extern "C".


