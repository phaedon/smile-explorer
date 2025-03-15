#ifndef SMILEEXPLORER_TREES_HULL_WHITE_PROPAGATOR_H_
#define SMILEEXPLORER_TREES_HULL_WHITE_PROPAGATOR_H_

#include "trinomial_tree.h"

namespace smileexplorer {

struct HullWhitePropagator {
  double operator()(const TrinomialTree& tree, int t, int i) { return -1; }
};

}  // namespace smileexplorer

#endif  // SMILEEXPLORER_TREES_HULL_WHITE_PROPAGATOR_H_