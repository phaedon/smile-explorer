#ifndef SMILEEXPLORER_TEST_TREE_MATCHERS_H_
#define SMILEEXPLORER_TEST_TREE_MATCHERS_H_

#include "gmock/gmock.h"
#include "trees/binomial_tree.h"

namespace smileexplorer {

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
      const auto actual = binomial_tree.nodeValue(i, j);
      if (std::abs(actual - expected_row[j]) > tolerance) {
        *result_listener << "Value at (" << i << ", " << j << "):" << actual
                         << " does not match:" << expected_row[j];
        return false;
      }
    }
  }
  return true;
}
}  // namespace smileexplorer

#endif  // SMILEEXPLORER_TEST_TREE_MATCHERS_H_