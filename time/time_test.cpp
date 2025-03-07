#include "time/time.h"

#include <gtest/gtest.h>
namespace markets {
namespace {

TEST(TimegridTest, RegularGrid) {
  Timegrid grid(11);
  for (int i = 0; i < 11; ++i) {
    grid.set(i, i * 0.1);
  }
  EXPECT_DOUBLE_EQ(1.0, grid.time(10));

  // Failure cases.
  EXPECT_EQ(std::nullopt, grid.getTimeIndexForExpiry(-0.5));
  EXPECT_EQ(std::nullopt, grid.getTimeIndexForExpiry(1.001));

  // Nearly-exact matches, to within accumulated floating-point error.
  EXPECT_EQ(5, grid.getTimeIndexForExpiry(0.5));
  EXPECT_EQ(8, grid.getTimeIndexForExpiry(0.8));
  EXPECT_EQ(10, grid.getTimeIndexForExpiry(1.0));

  // In betweens...
  EXPECT_EQ(8, grid.getTimeIndexForExpiry(0.76));
  EXPECT_EQ(8, grid.getTimeIndexForExpiry(0.84));

  EXPECT_EQ(0, grid.getTimeIndexForExpiry(0.001));
  EXPECT_EQ(0, grid.getTimeIndexForExpiry(0.049));
  EXPECT_EQ(1, grid.getTimeIndexForExpiry(0.05));  // Tiebreak due to rounding.
  EXPECT_EQ(1, grid.getTimeIndexForExpiry(0.051));

  EXPECT_EQ(10, grid.getTimeIndexForExpiry(0.999));
}

TEST(TimegridTest, IrregularGrid) {
  Timegrid grid;
  for (double t : {0.0, 1.0, 1.2, 1.3, 2.0, 5.0, 10.0, 10.01}) {
    grid.append(t);
  }

  // Spot-check a couple of values.
  EXPECT_DOUBLE_EQ(2.0, grid.time(4));
  EXPECT_DOUBLE_EQ(10.01, grid.time(7));

  EXPECT_EQ(0, grid.getTimeIndexForExpiry(0.49));
  EXPECT_EQ(2, grid.getTimeIndexForExpiry(1.1));
  EXPECT_EQ(2, grid.getTimeIndexForExpiry(1.24));
  EXPECT_EQ(4, grid.getTimeIndexForExpiry(3.49));
  EXPECT_EQ(5, grid.getTimeIndexForExpiry(3.51));
  EXPECT_EQ(5, grid.getTimeIndexForExpiry(7.495));
  EXPECT_EQ(6, grid.getTimeIndexForExpiry(7.505));
  EXPECT_EQ(6, grid.getTimeIndexForExpiry(10));
  EXPECT_EQ(7, grid.getTimeIndexForExpiry(10.005));
  EXPECT_EQ(7, grid.getTimeIndexForExpiry(10.01));
  EXPECT_EQ(std::nullopt, grid.getTimeIndexForExpiry(10.011));
}

}  // namespace
}  // namespace markets
