
#include "curve_calculators.h"

#include <gtest/gtest.h>

namespace smileexplorer {
namespace {

TEST(CurveCalculatorsTest, ForwardRatesAndDiscountFactorsMatch) {
  double expected_rate = 0.031415;
  double tenor = 2.718;
  for (const auto period : {CompoundingPeriod::kAnnual,
                            CompoundingPeriod::kSemi,
                            CompoundingPeriod::kQuarterly,
                            CompoundingPeriod::kMonthly,
                            CompoundingPeriod::kContinuous}) {
    double df_end = dfByPeriod(expected_rate, tenor, period);
    double fwdrate = fwdRateByPeriod(1.0, df_end, tenor, period);
    EXPECT_DOUBLE_EQ(expected_rate, fwdrate);
  }
}

}  // namespace
}  // namespace smileexplorer
