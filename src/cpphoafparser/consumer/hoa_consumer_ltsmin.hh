//==============================================================================
//
//  Author:
//  * Vincent Bloemen <v.bloemen@utwente.nl>
//
//------------------------------------------------------------------------------
//
// This file constructs a buchi_t object from a HOA input.
//
//==============================================================================

#ifndef CPPHOAFPARSER_HOACONSUMERLTSMIN_H
#define CPPHOAFPARSER_HOACONSUMERLTSMIN_H

extern "C" {
#include <hre/config.h>
#include <stdlib.h>
#include <ctype.h>

#include <hre/user.h>
#include <ltsmin-lib/ltl2ba-lex-helper.h>
#include <ltsmin-lib/ltsmin-syntax.h>
#include <ltsmin-lib/ltsmin-tl.h>
#include <ltsmin-lib/ltsmin-buchi.h>
}

#include <iostream>
#include <vector>
#include <string>

#include <cpphoafparser/consumer/hoa_consumer.hh>
#include <cpphoafparser/parser/hoa_parser_helper.hh>

namespace cpphoafparser {

/**
 * Constructs a ltsmin_buchi_t object for a deterministic TGBA, provided in HOA
 */
class HOAConsumerLTSmin : public HOAConsumer {
public:

  /* returns the constructed ltsmin_buchi_t object */
  ltsmin_buchi_t *get_ltsmin_buchi() {
    return ba;
  }

  void set_ltsmin_expr_list(ltsmin_expr_list_t *list) {
    le_list = list;
  }

  virtual bool parserResolvesAliases() override {
    return false;
  }

  virtual void notifyHeaderStart(const std::string& version) override {
    (void) version;
  }

  virtual void setNumberOfStates(unsigned int numberOfStates) override {
    // allocate memory for ltsmin_buchi_t
    ba = (ltsmin_buchi_t*) RTmalloc(sizeof(ltsmin_buchi_t) + numberOfStates * sizeof(ltsmin_buchi_state_t*));
    ba->state_count = numberOfStates;

    // create a state mapping for the number of states
    for (unsigned int i=0; i<numberOfStates; i++) {
      state_map.push_back(i); // every state is mapped to itself
    }
  }

  virtual void addStartStates(const int_list& stateConjunction) override {
    // one initial state, and this must be named '0'
    HREassert(stateConjunction.size() == 1, "Multiple initial states defined");
    for (unsigned int state : stateConjunction) {
      if (state != 0) {
        // possibly swap starting state so it's always 0
        state_map[state] = 0;
        state_map[0] = state;
      }
    }
  }

  virtual void addAlias(const std::string& name, label_expr::ptr labelExpr) override {
    (void) name;
    (void) labelExpr;
  }

  virtual void setAPs(const std::vector<std::string>& aps) override {
    ba->predicate_count = aps.size();
    ba->predicates = (ltsmin_expr_t*) RTmalloc(ba->predicate_count * sizeof(ltsmin_expr_t));
    int i = 0;

    //std::cout << "found predicates: " << std::endl;
    for (const std::string& ap : aps) {
      std::string ap_name = ap;
      replace( ap_name.begin(), ap_name.end(), '\'', '\"');

      ltsmin_expr_t e = ltsmin_expr_lookup(NULL, (char*) ap_name.c_str(), &le_list);

      HREassert(e, "Empty LTL expression");

      ba->predicates[i] = e;
      i++;
    }
  }

  /* recursive auxiliary function to derive the acceptance conditions */
  void recurAcceptance(acceptance_expr::ptr accExpr, int pair_id) {
    if (accExpr->isAND()) {
      // rabin pair = FIN & INF & INF & INF ...
      recurAcceptance(accExpr->getLeft(), pair_id);
      //std::cout << " AND ";
      recurAcceptance(accExpr->getRight(), pair_id);
    }
    else if (accExpr->isOR()) {
      // new rabin pair
      recurAcceptance(accExpr->getLeft(), pair_id);
      //std::cout << " OR ";
      recurAcceptance(accExpr->getRight(), pair_id+1);
    }
    else {
      HREassert(accExpr->isAtom(), "Unknown acceptance condition"); // we only allow AND and Atoms
      AtomAcceptance atom = accExpr->getAtom();
      HREassert(!atom.isNegated(), "We don't allow negated atoms"); // we don't allow negated atoms yet
      
      // decide if its FIN or INF
      uint32_t acc_mark = ( 1 << ((uint32_t) atom.getAcceptanceSet()));
      if (atom.getType() == AtomAcceptance::AtomType::TEMPORAL_FIN) {
        //std::cout << " FIN:" << acc_mark << " ";
        ba->rabin->pairs[pair_id].fin_set |= acc_mark;
        ba->acceptance_set |= acc_mark;
        
      }
      else if (atom.getType() == AtomAcceptance::AtomType::TEMPORAL_INF) {
        //std::cout << " INF:" << acc_mark << " ";
        ba->rabin->pairs[pair_id].inf_set |= acc_mark;
        ba->acceptance_set |= acc_mark;
      }
      else {
        HREassert(false, "We don't support acceptance atoms other than Fin or Inf"); 
      }
    }
  }

  virtual void setAcceptanceCondition(unsigned int numberOfSets, acceptance_expr::ptr accExpr) override {
    // We assume generalized rabin acceptance
    //std::cout << "Acceptance: " << numberOfSets << " " << *accExpr << std::endl;
    
    recurAcceptance(accExpr,0);
    //std::cout << std::endl;
    (void) numberOfSets;
  }

  virtual void provideAcceptanceName(const std::string& name, const std::vector<IntOrString>& extraInfo) override {
    std::string rabin ("Rabin");
    std::string gra ("generalized-Rabin");
    if (gra.compare(name) != 0 && rabin.compare(name) != 0)
      HREassert(false, "We only accept Rabin or Generalized Rabin automata"); 
    
    // get the number of rabin pairs
    HREassert(extraInfo.size() > 0, "Info on the number of pairs is not available in the HOA"); 
    HREassert(extraInfo[0].isInteger(), "First extra element for generalized rabin should indicate the number of pairs"); 

    //std::cout << "Number of pairs: " << extraInfo[0].getInteger() << std::endl;
    int n_pairs = extraInfo[0].getInteger();

    ba->rabin = (rabin_t*) RTmalloc(sizeof(rabin_t) + n_pairs * sizeof(rabin_pair_t) );
    ba->rabin->n_pairs = n_pairs;

    // initialize to 0
    for (int i=0; i<n_pairs; i++) {
      ba->rabin->pairs[i].fin_set = 0;
      ba->rabin->pairs[i].inf_set = 0;
    }
    ba->acceptance_set = 0;
  }

  virtual void setName(const std::string& name) override {
    (void) name;
  }

  virtual void setTool(const std::string& name, std::shared_ptr<std::string> version) override {
    (void) name;
    (void) version;
  }

  virtual void addProperties(const std::vector<std::string>& properties) override {
    (void) properties;
  }

  virtual void addMiscHeader(const std::string& name, const std::vector<IntOrString>& content) override {
    (void) name;
    (void) content;
  }

  virtual void notifyBodyStart() override {
  }

  virtual void addState(unsigned int id,
                        std::shared_ptr<std::string> info,
                        label_expr::ptr labelExpr,
                        std::shared_ptr<int_list> accSignature) override {
    // for state based acceptance marks
    s_acc = 0;
    if (accSignature) {
      for (unsigned int acc : *accSignature) {
        s_acc |= (1 << acc);
      }
    } 
    (void) id;
    (void) info;
    (void) labelExpr;
  }

  virtual void addEdgeImplicit(unsigned int stateId,
                               const int_list& conjSuccessors,
                               std::shared_ptr<int_list> accSignature) override {
    HREassert(0, "Implicit edges are not supported");
    (void) stateId;
    (void) conjSuccessors;
    (void) accSignature;
  }


  void addTransition(const int_list& conjSuccessors,
                     std::shared_ptr<int_list> accSignature) {

    uint32_t t_acc = 0;
    if (accSignature) {
      for (unsigned int acc : *accSignature) {
        t_acc |= (1 << acc);
      }
    } 

    transitions.push_back(tmp_pos);
    transitions.push_back(tmp_neg);
    transitions.push_back(state_map[conjSuccessors.front()]);
    transitions.push_back(t_acc);

    transition_count ++;
  }

  // parses the predicate that is assigned to a transition
  void parsePredicate(label_expr::ptr labelExpr, bool negated, 
                      const int_list& conjSuccessors,
                      std::shared_ptr<int_list> accSignature) {
    if (labelExpr->isAND()) {
      parsePredicate(labelExpr->getLeft(), false, conjSuccessors, accSignature);
      parsePredicate(labelExpr->getRight(), false, conjSuccessors, accSignature);
    }
    else if (labelExpr->isOR()) {
      parsePredicate(labelExpr->getLeft(), false, conjSuccessors, accSignature);

      // add the transition and reset
      addTransition (conjSuccessors, accSignature);
      tmp_pos = 0; 
      tmp_neg = 0;

      parsePredicate(labelExpr->getRight(), false, conjSuccessors, accSignature);
    }
    else if (labelExpr->isNOT()) {
      parsePredicate(labelExpr->getLeft(), !negated, conjSuccessors, accSignature);
    }
    else if (labelExpr->isTRUE()) {
      HREassert(!negated, "True predicate should not be negated") 
      tmp_neg = 0;
      tmp_pos = 0;
    }
    else if (labelExpr->isAtom()) {
      HREassert(!labelExpr->getAtom().isAlias(), "Atom should not be an alias");
      uint32_t ap_index = labelExpr->getAtom().getAPIndex();
      if (negated) 
        tmp_neg |= (1 << ap_index);
      else
        tmp_pos |= (1 << ap_index);
    }
    else {
      HREassert(0, "Unsuported predicate");
    }
  }


  virtual void addEdgeWithLabel(unsigned int stateId,
                                label_expr::ptr labelExpr,
                                const int_list& conjSuccessors,
                                std::shared_ptr<int_list> accSignature) override {

    // we can only handle deterministic successors
    HREassert(conjSuccessors.size()==1, "Nondeterministic choice of successors"); 

    tmp_pos = 0; 
    tmp_neg = 0;
    if (labelExpr) {
      parsePredicate(labelExpr, false, conjSuccessors, accSignature);
    }

    addTransition(conjSuccessors, accSignature);

    (void) stateId;
  }

  virtual void notifyEndOfState(unsigned int stateId) override {

    // allocate memory for transitions
    ltsmin_buchi_state_t * bs = NULL;
    bs = (ltsmin_buchi_state_t*) RTmalloc(sizeof(ltsmin_buchi_state_t) + transition_count * sizeof(ltsmin_buchi_transition_t));
    bs->transition_count = transition_count;
    bs->accept = s_acc;

    int n_trans = 0;
    for (uint32_t i=0; i<transitions.size(); i+=4) {
        bs->transitions[n_trans].pos      = (int*) RTmalloc(sizeof(int) * 2);
        bs->transitions[n_trans].neg      = (int*) RTmalloc(sizeof(int) * 2);
        bs->transitions[n_trans].pos[0]   = transitions[i];
        bs->transitions[n_trans].neg[0]   = transitions[i+1];
        bs->transitions[n_trans].to_state = transitions[i+2];
        bs->transitions[n_trans].acc_set  = transitions[i+3];
        bs->transitions[n_trans].index    = index++;
        n_trans++;
    }

    ba->states[state_map[state_index++]] = bs;

    transitions.clear();
    transition_count = 0;

    (void) stateId;
  }

  virtual void notifyEnd() override {
    ba->trans_count = index;
  }

  virtual void notifyAbort() override {
  }

  virtual void notifyWarning(const std::string& warning) override {
    std::cerr << "Warning: " << warning << std::endl;
  }

private:
  int tmp_pos, tmp_neg;
  ltsmin_buchi_t *ba = NULL;
  ltsmin_expr_list_t *le_list = NULL;
  uint32_t s_acc = 0;
  int transition_count = 0;
  int state_index = 0;
  int index = 0; // global uniquely increasing counter
  std::vector<int> transitions;
  std::vector<int> state_map;
};

}

#endif
