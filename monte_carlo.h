#include <vector>

#include "absl/random/random.h"

class RandomProcess {
 public:
  // For now, assume const vol
  RandomProcess(double volatility) : volatility_(volatility) {}

  std::vector<double> generate(double start, double t, double dt);

 private:
  absl::BitGen bitgen_;
  double volatility_;
};