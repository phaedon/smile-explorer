#include "global_rates.h"

#include <gtest/gtest.h>

namespace smileexplorer {
namespace {

TEST(GlobalCurrenciesTest, BasicCSVFileLoading) {
  GlobalCurrencies currencies;
  EXPECT_EQ(133.14, currencies(Currency::USD, Currency::ISK));
  EXPECT_EQ(std::nullopt, currencies(Currency::NOK, Currency::NOK));
}

}  // namespace
}  // namespace smileexplorer