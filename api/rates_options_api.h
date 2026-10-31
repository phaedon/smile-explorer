#ifndef SMILEEXPLORER_API_RATES_OPTIONS_API_H_
#define SMILEEXPLORER_API_RATES_OPTIONS_API_H_

#include <vector>

#include "absl/status/statusor.h"
#include "api/pricing_api.h"
#include "api/short_rate_api.h"
#include "calibration/hull_white_calibration.h"

// The runtime API for interest rate options on a Hull-White tree: swaptions
// (priced as options on coupon bonds) and caps, each alongside its closed
// form, and calibration of the model to quoted normal vols.

namespace smileexplorer::api {

struct SwaptionSpec {
  double expiry_years = 1.0;
  // Length of the underlying swap, which starts at expiry: a whole number of
  // fixed periods.
  double tenor_years = 5.0;
  int fixed_payments_per_year = 1;
  // At the money, the fixed rate is the forward par swap rate, and
  // `fixed_rate` is ignored.
  bool at_the_money = true;
  double fixed_rate = 0.03;
  double notional = 100.0;
  // A payer swaption is the right to pay fixed; a receiver, to receive it.
  bool is_payer = true;
  // A Bermudan can be exercised at expiry or on any later fixed payment date
  // before the last, into the rest of the swap (at the same fixed rate).
  bool is_bermudan = false;
};

// The tree's horizon_years and bond_face are ignored: the tree runs to the end
// of the swap, and the bond is the swap's fixed leg plus notional. Expiry and
// payment dates must fall on the tree's steps.
struct SwaptionRequest {
  HullWhiteTreeSpec tree;
  SwaptionSpec swaption;
};

struct SwaptionResult {
  // The option on the bond (a put at par for a payer swaption, a call for a
  // receiver), with the bond's and the option's values at every node.
  // option.analytic_price is Jamshidian's closed form.
  BondOptionResult option;
  double fixed_rate = 0.0;
  // Today's forward par swap rate, and the annuity (per unit notional).
  double par_rate = 0.0;
  double annuity = 0.0;
  std::vector<double> payment_times;
  // The tree and closed-form prices as normal vols. For a Bermudan, which
  // has no closed form, analytic prices and vols are NaN.
  double normal_vol = 0.0;
  double analytic_normal_vol = 0.0;
  // A Bermudan's exercise dates, and for each, the European swaption
  // exercisable only then into the rest of the swap, priced on the same tree.
  std::vector<double> exercise_times;
  std::vector<double> coterminal_european_prices;
};

absl::StatusOr<SwaptionResult> priceSwaption(const SwaptionRequest& request);

struct CapSpec {
  double maturity_years = 5.0;
  // Caplets cover each period after the first, which has already fixed.
  int payments_per_year = 4;
  // At the money, the strike is the par swap rate over the caplets' periods.
  bool at_the_money = true;
  double strike = 0.03;
  double notional = 100.0;
  // A cap pays when rates are above the strike; a floor when below.
  bool is_cap = true;
};

struct CapRequest {
  HullWhiteTreeSpec tree;
  CapSpec cap;
};

struct CapResult {
  double strike = 0.0;
  double price = 0.0;
  double analytic_price = 0.0;
  // Per caplet:
  std::vector<double> fixing_times;
  std::vector<double> payment_times;
  std::vector<double> forward_rates;
  std::vector<double> prices;
  std::vector<double> analytic_prices;
  // The normal vol of each caplet's closed-form price.
  std::vector<double> normal_vols;
};

absl::StatusOr<CapResult> priceCap(const CapRequest& request);

struct CalibrationRequest {
  RateCurveSpec curve;
  std::vector<RateOptionQuote> quotes;
  int fixed_payments_per_year = 1;
};

absl::StatusOr<CalibrationResult> calibrateHullWhite(
    const CalibrationRequest& request);

// The model's at-the-money normal vol for each quote (whose own normal_vol is
// ignored), with the given parameters. NaN where no normal vol matches.
struct ModelVolsRequest {
  RateCurveSpec curve;
  HullWhiteParams params;
  std::vector<RateOptionQuote> quotes;
  int fixed_payments_per_year = 1;
};

absl::StatusOr<std::vector<double>> modelNormalVols(
    const ModelVolsRequest& request);

}  // namespace smileexplorer::api

#endif  // SMILEEXPLORER_API_RATES_OPTIONS_API_H_
