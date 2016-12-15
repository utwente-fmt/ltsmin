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
  }

  virtual void addStartStates(const int_list& stateConjunction) override {
    // one initial state, and this must be named '0'
    HREassert(stateConjunction.size() == 1, "Multiple initial states defined");
    for (unsigned int state : stateConjunction) {
      HREassert(state == 0, "The initial state is not labeled '0'");
    }
  }

  virtual void addAlias(const std::string& name, label_expr::ptr labelExpr) override {
    (void) name;
    (void) labelExpr;
  }

  virtual void setAPs(const std::vector<std::string>& aps) override {
    return; // TODO: fix 
    /*
    ba->predicate_count = aps.size();
    ba->predicates = (ltsmin_expr_t*) RTmalloc(ba->predicate_count * sizeof(ltsmin_expr_t));
    int i = 0;

    //std::cout << "found predicates: " << std::endl;
    for (const std::string& ap : aps) {
      //std::cout << "ap: " << ap << std::endl;

      ltsmin_expr_t e = ltsmin_expr_lookup(NULL, (char*) ap.c_str(), &le_list);

      HREassert(e, "Empty LTL expression");

      //char buffer[2048];
      //memset(buffer, 0, sizeof(buffer));
      //ltsmin_expr_print_ltl(e, buffer); 
      //std::cout << "ap expr: " << buffer << std::endl;

      ba->predicates[i] = e;
      i++;
    }*/

    (void) aps;
  }

  /* recursive auxiliary function to derive the acceptance conditions */
  void recurAcceptance(acceptance_expr::ptr accExpr) {
    if (accExpr->isAND()) {
      recurAcceptance(accExpr->getLeft());
      recurAcceptance(accExpr->getRight());
    }
    else {
      return;
      HREassert(accExpr->isAtom(), "Unknown acceptance condition"); // we only allow AND and Atoms
      AtomAcceptance atom = accExpr->getAtom();
      HREassert(!atom.isNegated(), "We don't allow negated atoms"); // we don't allow negated atoms yet
      HREassert(atom.getType() == AtomAcceptance::AtomType::TEMPORAL_INF, "We only allow 'Inf(x)' acceptance condtitions"); // type must be Inf()
      ba->acceptance_set |= ( 1 << ((uint32_t) atom.getAcceptanceSet()));
      //std::cout << "atom: " << atom.getAcceptanceSet() << " acceptance_set: " << ba->acceptance_set << std::endl;
    }
  }

  virtual void setAcceptanceCondition(unsigned int numberOfSets, acceptance_expr::ptr accExpr) override {
    // Currently we only allow: Inf(a_1) & Inf(a_2) & ... & Inf(a_k)
    recurAcceptance(accExpr);
    (void) numberOfSets;
  }

  virtual void provideAcceptanceName(const std::string& name, const std::vector<IntOrString>& extraInfo) override {
    (void) name;
    (void) extraInfo;
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


  // parses the predicate that is assigned to a transition
  void parsePredicate(label_expr::ptr labelExpr, bool negated) {
    if (labelExpr->isAND()) {
      parsePredicate(labelExpr->getLeft(), false);
      parsePredicate(labelExpr->getRight(), false);
    }
    else if (labelExpr->isNOT()) {
      parsePredicate(labelExpr->getLeft(), !negated);
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

    tmp_pos = 0; 
    tmp_neg = 0;
    if (labelExpr) {
      parsePredicate(labelExpr, false);
    }

    // we can only handle deterministic successors
    HREassert(conjSuccessors.size()==1, "Nondeterministic choice of successors"); 

    uint32_t t_acc = 0;
    if (accSignature) {
      for (unsigned int acc : *accSignature) {
        t_acc |= (1 << acc);
      }
    } 

    transitions.push_back(tmp_pos);
    transitions.push_back(tmp_neg);
    transitions.push_back(conjSuccessors.front());
    transitions.push_back(t_acc);

    transition_count ++;

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

    ba->states[state_index++] = bs;

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
  uint32_t s_acc = 0;
  int transition_count = 0;
  int state_index = 0;
  int index = 0; // global uniquely increasing counter
  std::vector<int> transitions;
};

}

#endif
