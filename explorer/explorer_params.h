#ifndef SMILEEXPLORER_EXPLORER_EXPLORER_PARAMS_
#define SMILEEXPLORER_EXPLORER_EXPLORER_PARAMS_

#include "global_rates.h"
#include "rates/rates_curve.h"

namespace smileexplorer {

struct ExplorerParams {
  ExplorerParams(GlobalRates* rates) : global_rates(rates) {}

  float asset_tree_duration = 3.0;
  float asset_tree_timestep = 0.05;
  float spot_price = 100.;

  // Parameter for the Jarrow-Rudd convention.
  double jarrowrudd_expected_drift = 0.1;

  const RatesCurve* curve() const {
    return global_rates->curves[currency].get();
  }

  const RatesCurve* foreign_curve() const {
    return global_rates->curves[foreign_currency].get();
  }

  // Not owned.
  GlobalRates* global_rates;

  Currency currency = Currency::USD;
  Currency foreign_currency = Currency::EUR;

  float option_expiry = 1.0;
  float option_strike = 105.;

  // sqrt(252) is chosen as a reasonable initial value, in the absence of any
  // other info.
  float flat_vol = 0.158745;

  float sigmoid_vol_range = 0.3;
  float sigmoid_vol_stretch = 0.1;

  float time_for_displaying_probability = 1.0;
};
}  // namespace smileexplorer

#endif  // SMILEEXPLORER_EXPLORER_EXPLORER_PARAMS_