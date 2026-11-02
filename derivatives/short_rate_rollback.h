#ifndef SMILEEXPLORER_DERIVATIVES_SHORT_RATE_ROLLBACK_H_
#define SMILEEXPLORER_DERIVATIVES_SHORT_RATE_ROLLBACK_H_

#include <cmath>
#include <functional>
#include <vector>

#include "trees/trinomial_tree.h"

// Backward induction on a short-rate tree, with a choice at each step of
// discounting, compounding, or neither.
//
// The tree's branch probabilities are risk-neutral (the bank account is the
// numeraire), so:
//  - discounting every step gives bond prices: E[exp(-int r)];
//  - doing neither gives a plain expectation, which is how a futures price
//    rolls back (its margin is settled daily, with no interest);
//  - compounding gives the expected growth of money left at the overnight
//    rate: E[exp(+int r)], the settlement of a compounded-rate future.

namespace smileexplorer {

// Values at every node of slices 0..ti_end (ragged, like the tree), starting
// from `terminal` at every node of slice ti_end. Stepping back from slice ti,
// the expectation over the successors is multiplied by exp(k * r * dt), where
// r is the short rate at the node and k = rate_sign(ti) is -1 (discount),
// 0 (neither) or +1 (compound).
inline std::vector<std::vector<double>> rollBackOnShortRateTree(
    const TrinomialTree& rates,
    int ti_end,
    double terminal,
    const std::function<int(int)>& rate_sign) {
  std::vector<std::vector<double>> values(ti_end + 1);
  values[ti_end].assign(rates.numStatesAt(ti_end), terminal);
  const Timegrid& grid = rates.getTimegrid();
  for (int ti = ti_end - 1; ti >= 0; --ti) {
    const int k = rate_sign(ti);
    values[ti].resize(rates.numStatesAt(ti));
    for (int i = 0; i < rates.numStatesAt(ti); ++i) {
      const TrinomialNode& node = rates.node(ti, i);
      const auto next = rates.getSuccessorNodes(node, ti, i);
      // Successors are found by address, so convert them back to indices.
      const auto index = [&](const TrinomialNode& n) {
        return static_cast<int>(&n - &rates.tree_[ti + 1][0]);
      };
      const std::vector<double>& v = values[ti + 1];
      const double expected = node.branch_probs.pu * v[index(next.up)] +
                              node.branch_probs.pm * v[index(next.mid)] +
                              node.branch_probs.pd * v[index(next.down)];
      values[ti][i] =
          std::exp(k * rates.shortRate(ti, i) * grid.dt(ti)) * expected;
    }
  }
  return values;
}

// E[exp(int_start^end r)] at every node up to ti_start: the expected
// compounding over the period [ti_start, ti_end]. A compounded-rate future's
// rate at a node is (value - 1) / (period length).
inline std::vector<std::vector<double>> expectedCompounding(
    const TrinomialTree& rates, int ti_start, int ti_end) {
  return rollBackOnShortRateTree(rates, ti_end, 1.0, [ti_start](int ti) {
    return ti >= ti_start ? 1 : 0;
  });
}

// The price at every node of a zero-coupon bond paying 1 at slice ti_end.
inline std::vector<std::vector<double>> bondPrices(const TrinomialTree& rates,
                                                   int ti_end) {
  return rollBackOnShortRateTree(rates, ti_end, 1.0, [](int) { return -1; });
}

}  // namespace smileexplorer

#endif  // SMILEEXPLORER_DERIVATIVES_SHORT_RATE_ROLLBACK_H_
