#ifndef MARKETS_TEST_TREE_MATCHERS_H_
#define MARKETS_TEST_TREE_MATCHERS_H_

#include "gmock/gmock.h"
namespace markets {

MATCHER_P2(BinomialTreeMatchesUpToTimeIndex, tree_excerpt, tolerance, "") {
  const BinomialTree& binomial_tree = arg;
  for (size_t i = 0; i < tree_excerpt.size(); ++i) {
    const auto& expected_row = tree_excerpt[i];
    if (binomial_tree.numTimesteps() < i + 1) {
      *result_listener << "Actual matrix does not have enough columns for row "
                       << i << ".";
      return false;
    }
    if (expected_row.size() < i + 1) {
      *result_listener << "Expected row " << i
                       << " does not have the correct size.";
      return false;
    }

    for (size_t j = 0; j < expected_row.size(); ++j) {
      if (std::abs(binomial_tree.nodeValue(i, j) - expected_row[j]) >
          tolerance) {
        *result_listener << "Value at (" << i << ", " << j
                         << ") does not match.";
        return false;
      }
    }
  }
  return true;
}
}  // namespace markets

#endif  // MARKETS_TEST_TREE_MATCHERS_H_