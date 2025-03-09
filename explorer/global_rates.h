#ifndef MARKETS_EXPLORER_GLOBAL_RATES_
#define MARKETS_EXPLORER_GLOBAL_RATES_

#include <unordered_map>

#include "magic_enum.hpp"
#include "rates/rates_curve.h"

namespace markets {

enum class Currency { USD, EUR, JPY, CHF, NOK, ISK };

// This is very hacked 0th-order approximation just to get something going,
// in the absence of API access for market closes or live feeds.
inline double getApproxRate(Currency currency) {
  switch (currency) {
    case Currency::USD:
      return 0.042;
    case Currency::EUR:
      return 0.025;
    case Currency::JPY:
      return 0.01;
    case Currency::NOK:
      return 0.04;
    case Currency::CHF:
      return 0.0030;
    case Currency::ISK:
      return 0.082;
  }
}

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
      double flat_rate = getApproxRate(currency);
      curves[currency] = std::make_unique<ZeroSpotCurve>(
          ZeroSpotCurve({1, 2, 5, 10},
                        {flat_rate, flat_rate, flat_rate, flat_rate},
                        CompoundingPeriod::kAnnual));
    }
  }

  std::unordered_map<Currency, std::unique_ptr<RatesCurve>> curves;
};

}  // namespace markets

#endif  // MARKETS_EXPLORER_GLOBAL_RATES_