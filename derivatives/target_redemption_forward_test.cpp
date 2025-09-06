#include "target_redemption_forward.h"

#include <gtest/gtest.h>

namespace smileexplorer {
namespace {

TEST(TargetRedemptionForwardTest, Placeholder) {
  TargetRedemptionForward tarf(1000000, 150000, 0.99, 2.0, 0.25);
  double npv = tarf.price(1.10, 0.15, 0.01, 10000);

  EXPECT_GT(npv, 0);
}

}  // namespace
}  // namespace smileexplorer
