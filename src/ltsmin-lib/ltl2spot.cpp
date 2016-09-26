

// C++ libraries
#include <iostream>
#include <string>
#include <vector>

// spot libraries
#include <bddx.h>
#include <spot/tl/parse.hh>
#include <spot/tl/print.hh>
#include <spot/parseaut/public.hh>
#include <spot/twa/bddprint.hh>
#include <spot/twaalgos/translate.hh>
#include <spot/twaalgos/isdet.hh>
#include <spot/twaalgos/hoa.hh>

extern "C" {

#include <hre/config.h>
#include <stdlib.h>
#include <ctype.h>

#include <hre/user.h>
#include <ltsmin-lib/ltl2spot.h>
#include <ltsmin-lib/ltl2ba-lex-helper.h>
#include <ltsmin-lib/ltsmin-syntax.h>
#include <ltsmin-lib/ltsmin-tl.h>
#include <ltsmin-lib/ltsmin-buchi.h>

#define LTL_LPAR ((void*)0x01)
#define LTL_RPAR ((void*)0x02)

static ltsmin_expr_list_t *le_list = NULL;
static ltsmin_lin_expr_t *le;
static char ltl_formula[4096*32];

// linearizes the ltl expression to an array of tokens
// then iterates over this array and creates a string for the formula
// all found predicates are stored in a lookup list
static char *
ltl_to_store(ltsmin_expr_t e)
{
  const int le_size = 64;
  // linearized expression
  le = (ltsmin_lin_expr_t*) RTmalloc(sizeof(ltsmin_lin_expr_t) + le_size * sizeof(ltsmin_expr_t));
  le->size = le_size;
  le->count = 0;

  linearize_ltsmin_expr(e, &le);

  // create buffer to store the LTL formula in
  memset(ltl_formula, 0, sizeof(ltl_formula));
  char* at = ltl_formula;
  *at = '\0';

  for(int i=0; i < le->count; i++) {
    if (le->lin_expr[i] == LTL_LPAR) { at += sprintf(at, "("); continue; }
    if (le->lin_expr[i] == LTL_RPAR) { at += sprintf(at, ")"); continue; }

    switch(le->lin_expr[i]->token) {
      case LTL_TRUE:      at += sprintf(at, "true"); break;
      case LTL_FALSE:     at += sprintf(at, "false"); break;
      case LTL_AND:       at += sprintf(at, " && "); break;
      case LTL_OR:        at += sprintf(at, " || "); break;
      case LTL_NOT:       at += sprintf(at, " ! "); break;
      case LTL_FUTURE:    at += sprintf(at, " <> "); break;
      case LTL_GLOBALLY:  at += sprintf(at, " [] "); break;
      case LTL_UNTIL:     at += sprintf(at, " U "); break;
      case LTL_RELEASE:   at += sprintf(at, " R "); break;
      case LTL_NEXT:      at += sprintf(at, " X "); break;
      case LTL_EQUIV:     at += sprintf(at, " <-> "); break;
      case LTL_IMPLY:     at += sprintf(at, " -> "); break;
      case LTL_EQ:
      case LTL_SVAR:
      case LTL_VAR:
      case LTL_NEQ:
      case LTL_LT:
      case LTL_LEQ:
      case LTL_GT:
      case LTL_GEQ:
      case LTL_MULT:
      case LTL_DIV:
      case LTL_REM:
      case LTL_ADD:
      case LTL_SUB: {
        char buffer[2048];
        memset(buffer, 0, sizeof(buffer));
        ltsmin_expr_print_ltl(le->lin_expr[i], buffer); 
        ltsmin_expr_lookup(le->lin_expr[i], buffer, &le_list);
        at += sprintf(at, "\"%s\"", buffer);  
        break;
      }
      default:
        Abort("unhandled LTL_TOKEN: %d\n", le->lin_expr[i]->token); 
        break;
    }
  }
  return ltl_formula;
}

}

spot::twa_graph_ptr spot_automaton;
bool isTGBA;

static int 
get_predicate_index(std::vector<std::string> pred_vec, std::string predicate) 
{
  // iterate over the vector until the predicate matches
  for (size_t i=0; i<pred_vec.size(); i++) {
    if (predicate.compare(pred_vec[i]) == 0)
      return i;
  }
  return -1;
}



static ltsmin_buchi_t *
create_ltsmin_buchi(spot::twa_graph_ptr& aut)
{
  // ensure that the automaton is deterministic (apparently this is not possible)
  //HREassert(spot::is_deterministic(aut), "The automaton is not deterministic");

  // We need the dictionary to print the BDDs that label the edges
  const auto& dict = aut->get_dict();

  // create the ltsmin_buchi_t
  ltsmin_buchi_t *ba = NULL;
  ba = (ltsmin_buchi_t*) RTmalloc(sizeof(ltsmin_buchi_t) + aut->num_states() * 
    sizeof(ltsmin_buchi_state_t*));
  
  ba->state_count     = aut->num_states();
  ba->predicate_count = aut->ap().size();

  // create bitset for the acceptance set
  uint32_t acceptance_set = 0;
  if (isTGBA) {
    HREassert (aut->num_sets() <= 32, "No more than 32 TGBA accepting sets supported.")
    for (size_t i=0; i<aut->num_sets(); i++)
      acceptance_set |= (1ULL << i);
  }
  else 
    HREassert(aut->num_sets() == 1, "Multiple acceptance sets for BA");
  ba->acceptance_set = acceptance_set;

  // the initial state is not always 0, thus we create a map for the state numbers
  int state_map[ba->state_count];
  for (int i=0; i<ba->state_count; i++)
    state_map[i] = i;
  state_map[aut->get_init_state_number()] = 0;
  state_map[0] = aut->get_init_state_number();

  // fill in the predicates
  int i = 0;
  ba->predicates = (ltsmin_expr_t*) RTmalloc(ba->predicate_count * sizeof(ltsmin_expr_t));
  std::vector<std::string> pred_vec;
  for (spot::formula ap: aut->ap()) {
    pred_vec.push_back(ap.ap_name());
    ltsmin_expr_t e = ltsmin_expr_lookup(NULL, (char*) ap.ap_name().c_str(), &le_list);
    HREassert(e, "Empty LTL expression");
    ba->predicates[i++] = e;
  }

  // states are numbered from 0 to n-1
  int index = 0; // globally unique index
  for (int _s = 0; _s < ba->state_count; _s++) {
    int s = state_map[_s];

    // iterate over the outgoing edges to count it 
    int transition_count = 0;
    for (auto& t: aut->out(s)) {
      std::string cond = spot::bdd_format_formula(dict, t.cond);
      // count the number of '|' occurrences in the predicates
      for (size_t c_i=0; c_i<cond.length(); c_i++) {
        if (cond.at(c_i) == '|') {
          transition_count ++;
        }
      }
      transition_count ++;
    }

    // create the transitions
    ltsmin_buchi_state_t * bs = NULL;
    bs = (ltsmin_buchi_state_t*) RTmalloc(sizeof(ltsmin_buchi_state_t) + 
      transition_count * sizeof(ltsmin_buchi_transition_t));
    bs->transition_count = transition_count;

    if (isTGBA) 
      bs->accept = 0;
    else 
      bs->accept = aut->state_is_accepting(s);

    // iterate over the transitions
    int trans_index = 0;
    for (auto& t: aut->out(s)) {
        bs->transitions[trans_index].to_state = state_map[(int) t.dst];
        bs->transitions[trans_index].pos      = (int*) RTmalloc(sizeof(int) * 2);
        bs->transitions[trans_index].neg      = (int*) RTmalloc(sizeof(int) * 2);
        bs->transitions[trans_index].index    = index++;

        // parse the acceptance sets
        bs->transitions[trans_index].acc_set  = 0;
        if (isTGBA) {
          for (unsigned acc : t.acc.sets()) {
            bs->transitions[trans_index].acc_set |= (1 << ((uint32_t) acc));
          }
        }

        // transition conditions, parse the sat formula to the pos and neg bitsets
        bs->transitions[trans_index].pos[0]   = 0;
        bs->transitions[trans_index].neg[0]   = 0;
        std::string cond = spot::bdd_format_formula(dict, t.cond);
        int pred_start = -1;
        bool is_neg = false;
        for (size_t c_i=0; c_i<cond.length(); c_i++) {
          switch (cond.at(c_i)) {
            case '\"': {
              if (pred_start != -1) {
                // finished predicate
                std::string predicate = cond.substr(pred_start+1, c_i-pred_start-1);
                int pred_index = get_predicate_index(pred_vec, predicate);
                HREassert(pred_index >= 0, "Predicate not found");
                if (is_neg)
                  bs->transitions[trans_index].neg[0] |= (1 << pred_index);
                else 
                  bs->transitions[trans_index].pos[0] |= (1 << pred_index);
                pred_start = -1;
                is_neg = false;
              } 
              else
                pred_start = c_i; // start point of predicate
              } break;
            case '!':
              is_neg = true;
              break;
            case '|': {
              // create a new transition
              trans_index ++;
              bs->transitions[trans_index].to_state = state_map[(int) t.dst];
              bs->transitions[trans_index].pos      = (int*) RTmalloc(sizeof(int) * 2);
              bs->transitions[trans_index].neg      = (int*) RTmalloc(sizeof(int) * 2);
              bs->transitions[trans_index].index    = index++;
              bs->transitions[trans_index].acc_set  = bs->transitions[trans_index-1].acc_set;
              bs->transitions[trans_index].pos[0]   = 0;
              bs->transitions[trans_index].neg[0]   = 0;
              } break;
            default:
              break;
          }
        }

        trans_index ++;
    }
    ba->states[_s] = bs;
  }

  // NB: not necessarily equal to num_edges
  ba->trans_count = index;

  return ba;
}


void 
ltsmin_ltl2spot(ltsmin_expr_t e, int to_tgba) 
{
  // construct the LTL formula and store the predicates
  char *buff = ltl_to_store(e);
  
  // create and run a system command that produces the HOA
  std::string ltl = std::string(buff);

  // use Spot to parse the LTL and create an automata
  spot::parsed_formula f = spot::parse_infix_psl(ltl);
  bool parse_errors = f.format_errors(std::cerr);
  HREassert(!parse_errors, "Parse errors found in conversion of LTL to Spot formula. LTL = %s", buff);

  spot::translator trans;
  isTGBA = to_tgba;
  if (isTGBA)
    trans.set_type(spot::postprocessor::TGBA);
  else
    trans.set_type(spot::postprocessor::BA);
  trans.set_pref(spot::postprocessor::Deterministic);

  // create the automaton
  spot_automaton = trans.run(f.f);
}


ltsmin_buchi_t *
ltsmin_hoa_buchi() 
{
  ltsmin_buchi_t *ret = create_ltsmin_buchi(spot_automaton);
  return ret;
}
