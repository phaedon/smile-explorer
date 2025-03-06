#ifndef MARKETS_EXPLORER_GLOBAL_RATES_
#define MARKETS_EXPLORER_GLOBAL_RATES_

#include <unordered_map>

#include "magic_enum.hpp"
#include "markets/rates/rates_curve.h"

namespace markets {

enum class Currency {
  USD,
  EUR,
  GBP,
  JPY,
  CHF,
};

struct GlobalRates {
  GlobalRates() {
    // For now, just hard-code the initialisation into the constructor.
    // Later, fetch from a real source such as
    // https://sebgroup.com/our-offering/reports-and-publications/rates-and-iban/swap-rates
    //
    // Note: At the moment, the demo assumes that these are zero-coupon bond
    // yields, not swap rates. But the goal for now is just to have a working
    // demo, not to match exact market rates.
    constexpr auto currencies = magic_enum::enum_values<Currency>();
    for (const auto currency : currencies) {
      curves[currency] = std::make_unique<ZeroSpotCurve>(ZeroSpotCurve(
          {1, 2, 5, 10}, {0.05, 0.05, 0.05, 0.05}, CompoundingPeriod::kAnnual));
    }
  }

  std::unordered_map<Currency, std::unique_ptr<RatesCurve>> curves;
};

}  // namespace markets

#endif  // MARKETS_EXPLORER_GLOBAL_RATES_