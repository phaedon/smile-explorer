#ifndef MARKETS_EXPLORER_EXPLORER_PARAMS_
#define MARKETS_EXPLORER_EXPLORER_PARAMS_

#include "global_rates.h"
#include "markets/rates/rates_curve.h"

namespace markets {

struct ExplorerParams {
  ExplorerParams(GlobalRates* rates) : global_rates(rates) {}

  float asset_tree_duration = 10.0;
  float asset_tree_timestep = 0.25;
  float spot_price = 100.;

  // Parameter for the Jarrow-Rudd convention.
  double jarrowrudd_expected_drift = 0.1;

  const RatesCurve* curve() const {
    return global_rates->curves[currency].get();
  }

  // Not owned.
  GlobalRates* global_rates;

  Currency currency = Currency::USD;

  float option_expiry = 1.0;
  float option_strike = 100.;

  // sqrt(252) is chosen as a reasonable initial value, in the absence of any
  // other info.
  float flat_vol = 0.158745;
};
}  // namespace markets

#endif  // MARKETS_EXPLORER_EXPLORER_PARAMS_