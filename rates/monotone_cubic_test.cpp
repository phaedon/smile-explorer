#include "rates/monotone_cubic.h"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

namespace smileexplorer {
namespace {

struct Golden {
  double x;
  double value;
  double derivative;
};

// Values from boost::math::interpolators::pchip (Boost 1.90), which this
// replaces, on the same data.
void expectGolden(const MonotoneCubicInterpolator& f,
                  const std::vector<Golden>& golden) {
  for (const Golden& g : golden) {
    EXPECT_NEAR(f(g.x), g.value, 1e-14 * std::max(1.0, std::abs(g.value)))
        << "x = " << g.x;
    EXPECT_NEAR(f.derivative(g.x),
                g.derivative,
                1e-13 * std::max(1.0, std::abs(g.derivative)))
        << "x = " << g.x;
  }
}

TEST(MonotoneCubicTest, MatchesBoostOnAZeroCurve) {
  const MonotoneCubicInterpolator f({0.5, 1, 2, 3, 5, 10},
                                    {0.03, 0.032, 0.035, 0.0365, 0.038, 0.04});
  expectGolden(f,
               {{0.5, 0.029999999999999999, 0.0040000000000000036},
                {0.75, 0.031032258064516129, 0.0041290322580645198},
                {1, 0.032000000000000001, 0.0034838709677419383},
                {1.4, 0.033365677419354839, 0.0032619354838709729},
                {2, 0.035000000000000003, 0.0019999999999999957},
                {2.5, 0.035870192307692311, 0.0014903846153846083},
                {3, 0.036499999999999998, 0.0010384615384615374},
                {4.2, 0.037514293706293701, 0.00068223776223776339},
                {5, 0.037999999999999999, 0.00054545454545454591},
                {7.5, 0.039090909090909093, 0.00036363636363636399},
                {9.99, 0.03999600290327273, 0.0003994199272727276},
                {10, 0.040000000000000001, 0.00040000000000000034}});
}

TEST(MonotoneCubicTest, MatchesBoostOnAHump) {
  const MonotoneCubicInterpolator f({0, 1, 2, 3, 4}, {1, 3, 2, 2.5, 0.5});
  expectGolden(f,
               {{0, 1, 2},
                {0.3, 1.7259999999999998, 2.6600000000000001},
                {1, 3, 0},
                {1.5, 2.5, -1.5},
                {2, 2, 0},
                {2.7, 2.3920000000000003, 0.62999999999999978},
                {3, 2.5, 0},
                {3.5, 1.75, -2.5},
                {4, 0.5, -2}});
}

TEST(MonotoneCubicTest, MatchesBoostWithAFlatSegment) {
  const MonotoneCubicInterpolator f({0, 1, 2, 3, 5}, {0, 1, 1, 2, 2.2});
  expectGolden(f,
               {{0.5, 0.625, 1.25},
                {1.5, 1, 0},
                {2.25, 1.1468750000000001, 1.0625},
                {2.9, 1.9558, 0.66600000000000004},
                {4, 2.125, 0.075000000000000067},
                {5, 2.2000000000000002, 0.10000000000000009}});
}

TEST(MonotoneCubicTest, MonotoneDataGiveAMonotoneCurve) {
  // Steep then flat: a natural cubic spline would overshoot here.
  const MonotoneCubicInterpolator f({0, 1, 2, 3, 4, 5},
                                    {0, 0.1, 5, 5.1, 5.2, 9});
  double previous = f(0);
  for (int k = 1; k <= 500; ++k) {
    const double v = f(k / 100.0);
    EXPECT_GE(v, previous) << "x = " << k / 100.0;
    previous = v;
  }
}

TEST(MonotoneCubicTest, DoesNotOvershootAnExtremum) {
  const MonotoneCubicInterpolator f({0, 1, 2, 3, 4}, {1, 3, 2, 2.5, 0.5});
  for (int k = 0; k <= 400; ++k) {
    EXPECT_LE(f(k / 100.0), 3.0);
    EXPECT_GE(f(k / 100.0), 0.5);
  }
  EXPECT_EQ(f.derivative(1), 0.0);
}

TEST(MonotoneCubicTest, IsContinuouslyDifferentiableAtTheKnots) {
  const MonotoneCubicInterpolator f({0.5, 1, 2, 3, 5, 10},
                                    {0.03, 0.032, 0.035, 0.0365, 0.038, 0.04});
  for (double knot : {1.0, 2.0, 3.0, 5.0}) {
    EXPECT_NEAR(f(knot - 1e-9), f(knot + 1e-9), 1e-11);
    EXPECT_NEAR(f.derivative(knot - 1e-9), f.derivative(knot + 1e-9), 1e-9);
  }
}

TEST(MonotoneCubicTest, TwoPointsAreLinear) {
  const MonotoneCubicInterpolator f({1, 3}, {2, 6});
  EXPECT_DOUBLE_EQ(f(2), 4);
  EXPECT_DOUBLE_EQ(f.derivative(1.5), 2);
}

TEST(MonotoneCubicTest, ExtrapolatesTheEndCubics) {
  const MonotoneCubicInterpolator f({0, 1, 2}, {0, 1, 4});
  // Just outside each end, the end interval's cubic carries on smoothly.
  EXPECT_NEAR(f(-1e-9), f(0) - 1e-9 * f.derivative(0), 1e-15);
  EXPECT_NEAR(f(2 + 1e-9), f(2) + 1e-9 * f.derivative(2), 1e-15);
}

}  // namespace
}  // namespace smileexplorer
