#ifndef MARKETS_EXPLORER_EXPLORER_PARAMS_
#define MARKETS_EXPLORER_EXPLORER_PARAMS_

#include "markets/rates/rates_curve.h"

namespace markets {

struct ExplorerParams {
  float asset_tree_duration = 10.0;
  float asset_tree_timestep = 0.25;
  double spot_price = 100.;
  double jr_expected_drift = 0.1;
  std::unique_ptr<RatesCurve> curve;

  // See for example
  // https://sebgroup.com/our-offering/reports-and-publications/rates-and-iban/swap-rates
  std::array<float, 4> rates = {0.045, 0.0423, 0.0401, 0.0397};

  float option_expiry = 1.0;
  float flat_vol = 0.1587;

  ExplorerParams()
      : curve(std::make_unique<NoDiscountingCurve>(NoDiscountingCurve())
                  .release()) {}
};
}  // namespace markets

#endif  // MARKETS_EXPLORER_EXPLORER_PARAMS_