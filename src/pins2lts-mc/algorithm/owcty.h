/**
 * OWCTY-MAP Barnat et al.
 *
 * Input-output diagram of OWCTY procedures:
 *
 *      reach(init)         swap          (reset)       reach
 * stack--------> out_stack ----> in_stack ----> stack ------> out_stack
 *                                   ^  [pre>0;pre=0]  [pre++] /| [pre==0]
 *                                    \_______________________/ |pre_elimination
 *                                       pre_elimination        v
 *                                            [pre>0]        stack-\ [--pre==0]
 *                                                             ^___/ elimination
 *
 * Only accepting states are transfered between methods.
 * The predecessor count (pre) is kept in a global array (global->pre). It also
 * includes a flip bit to distinguish new states in each iteration of
 * reachability. By doing a compare and swap on pre + flip, we can grab
 * states with accepting predecessors and reset pre = 0, hence
 * reset van be inlined in reach(ability).
 */

#ifndef OWCTY_H
#define OWCTY_H

#include <pins2lts-mc/algorithm/ltl.h>

#endif // OWCTY_H
