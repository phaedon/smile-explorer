#ifndef SMILEEXPLORER_EXPLORER_TREE_RENDER_H_
#define SMILEEXPLORER_EXPLORER_TREE_RENDER_H_

#include <vector>

#include "trees/binomial_tree.h"
#include "trees/trinomial_tree.h"

namespace smileexplorer {

struct TreeRenderData {
  std::vector<double> x_coords, y_coords, edge_x_coords, edge_y_coords;
};

inline TreeRenderData getTreeRenderData(const BinomialTree& tree) {
  TreeRenderData r;

  // First loop to collect node coordinates
  for (int t = 0; t < tree.numTimesteps(); ++t) {
    if (tree.isTreeEmptyAt(t)) {
      break;
    }
    for (int i = 0; i <= t; ++i) {
      r.x_coords.push_back(tree.totalTimeAtIndex(t));
      r.y_coords.push_back(tree.nodeValue(t, i));
    }
  }

  // Second loop to add edge coordinates
  int cumul_start_index = 0;
  for (int t = 0; t < tree.numTimesteps() - 1;
       ++t) {  // Iterate up to second-to-last timestep
    if (tree.isTreeEmptyAt(t)) {
      break;
    }
    for (int i = 0; i <= t; ++i) {
      // Calculate child indices
      size_t child1Index = cumul_start_index + t + i + 1;
      size_t child2Index = child1Index + 1;

      if (child1Index < r.x_coords.size()) {  // Check if child 1 exists
        // Add edge coordinates for child 1
        r.edge_x_coords.push_back(tree.totalTimeAtIndex(t));
        r.edge_y_coords.push_back(tree.nodeValue(t, i));
        r.edge_x_coords.push_back(tree.totalTimeAtIndex(t + 1));
        r.edge_y_coords.push_back(tree.nodeValue(t + 1, i));
      }
      if (child2Index < r.x_coords.size()) {  // Check if child 2 exists
        // Add edge coordinates for child 2
        r.edge_x_coords.push_back(tree.totalTimeAtIndex(t));
        r.edge_y_coords.push_back(tree.nodeValue(t, i));
        r.edge_x_coords.push_back(tree.totalTimeAtIndex(t + 1));
        r.edge_y_coords.push_back(tree.nodeValue(t + 1, i + 1));
      }
    }
    cumul_start_index += t + 1;
  }

  return r;
}

inline TreeRenderData getTreeRenderData(const TrinomialTree& tree) {
  TreeRenderData r;

  // First loop to collect node coordinates
  for (size_t ti = 0; ti < tree.tree_.size(); ++ti) {
    for (size_t j = 0; j < tree.tree_[ti].size(); ++j) {
      const auto& curr_node = tree.tree_[ti][j];
      r.x_coords.push_back(tree.dt_ * ti);
      r.y_coords.push_back(curr_node.val + tree.alphas_[ti]);
    }
  }

  for (size_t ti = 0; ti < tree.tree_.size() - 1; ++ti) {
    for (size_t j = 0; j < tree.tree_[ti].size(); ++j) {
      const auto& curr_node = tree.tree_[ti][j];

      const auto next = tree.getSuccessorNodes(curr_node, ti, j);

      r.edge_x_coords.push_back(tree.dt_ * ti);
      r.edge_y_coords.push_back(curr_node.val + tree.alphas_[ti]);
      r.edge_x_coords.push_back(tree.dt_ * (ti + 1));
      r.edge_y_coords.push_back(next.up.val + tree.alphas_[ti + 1]);

      r.edge_x_coords.push_back(tree.dt_ * ti);
      r.edge_y_coords.push_back(curr_node.val + tree.alphas_[ti]);
      r.edge_x_coords.push_back(tree.dt_ * (ti + 1));
      r.edge_y_coords.push_back(next.mid.val + tree.alphas_[ti + 1]);

      r.edge_x_coords.push_back(tree.dt_ * ti);
      r.edge_y_coords.push_back(curr_node.val + tree.alphas_[ti]);
      r.edge_x_coords.push_back(tree.dt_ * (ti + 1));
      r.edge_y_coords.push_back(next.down.val + tree.alphas_[ti + 1]);
    }
  }

  return r;
}

}  // namespace smileexplorer

#endif  // SMILEEXPLORER_EXPLORER_TREE_RENDER_H_