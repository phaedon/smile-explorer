#include "calibration/nelder_mead.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>

namespace smileexplorer {
namespace {

double rosenbrock2(const std::array<double, 2>& x) {
  return std::pow(1 - x[0], 2) + 100 * std::pow(x[1] - x[0] * x[0], 2);
}

double rosenbrock3(const std::array<double, 3>& x) {
  double sum = 0;
  for (int k = 0; k < 2; ++k) {
    sum += std::pow(1 - x[k], 2) + 100 * std::pow(x[k + 1] - x[k] * x[k], 2);
  }
  return sum;
}

TEST(NelderMeadTest, Rosenbrock2D) {
  const auto result =
      nelderMead<2>(rosenbrock2, {-1.2, 1.0}, {0.5, 0.5}, 2000, 1e-20);
  EXPECT_NEAR(1.0, result.x[0], 1e-5);
  EXPECT_NEAR(1.0, result.x[1], 1e-5);
  EXPECT_LT(result.value, 1e-10);
  EXPECT_LT(result.iterations, 2000);
}

TEST(NelderMeadTest, Rosenbrock3D) {
  const auto result = nelderMead<3>(
      rosenbrock3, {-1.0, 0.5, 0.5}, {0.5, 0.5, 0.5}, 5000, 1e-20);
  for (double xi : result.x) EXPECT_NEAR(1.0, xi, 1e-4);
}

TEST(NelderMeadTest, StopsAtTheIterationLimit) {
  const auto result =
      nelderMead<2>(rosenbrock2, {-1.2, 1.0}, {0.5, 0.5}, 5, 1e-20);
  EXPECT_EQ(5, result.iterations);
  EXPECT_LT(result.value, rosenbrock2({-1.2, 1.0}));
}

}  // namespace
}  // namespace smileexplorer
