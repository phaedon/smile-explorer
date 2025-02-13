#include "markets/monte_carlo.h"

std::vector<double> RandomProcess::generate(double start, double t, double dt) {
  std::vector<double> prices = {start};
  int num_timesteps = t / dt;
  double dw = absl::Gaussian(bitgen_, 0.0, 1.0);
  for (int i = 0; i < num_timesteps; ++i) {
    prices.push_back(prices.back() + dw);
  }
  return prices;
}
