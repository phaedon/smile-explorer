#include <Eigen/Dense>

#include "absl/random/random.h"
#include "absl/random/uniform_real_distribution.h"

namespace smileexplorer {

Eigen::VectorXd generateUniformRandomPath(size_t n) {
  absl::BitGen bitgen;
  Eigen::VectorXd path(n);
  for (size_t i = 0; i < n; ++i) {
    path(i) = absl::Uniform(bitgen, 0.0, 1.0);
  }

  return path;
}
}  // namespace smileexplorer
