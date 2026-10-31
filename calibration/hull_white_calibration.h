#ifndef SMILEEXPLORER_CALIBRATION_HULL_WHITE_CALIBRATION_H_
#define SMILEEXPLORER_CALIBRATION_HULL_WHITE_CALIBRATION_H_

#include <vector>

#include "absl/status/statusor.h"
#include "rates/rates_curve.h"

// Fits the Hull-White model's mean reversion a and volatility sigma to market
// quotes for at-the-money caplets and swaptions, given in normal vols.

namespace smileexplorer {

enum class RateOptionType { kCaplet, kSwaption };

struct RateOptionQuote {
  RateOptionType type = RateOptionType::kSwaption;
  // Years to expiry: the caplet's fixing, or the swaption's swap start.
  double expiry = 1.0;
  // Years covered: the caplet's accrual period, or the underlying swap's
  // length (a whole number of fixed periods).
  double tenor = 1.0;
  // The market's at-the-money normal vol (0.01 = 100 basis points a year).
  double normal_vol = 0.01;
};

struct HullWhiteParams {
  double mean_reversion = 0.0;
  double sigma = 0.0;
};

// The closed-form Hull-White price of the quoted at-the-money option (per unit
// notional; a caplet is a cap, a swaption a payer), and the normal vol that
// gives the same price. NaN if no normal vol does.
struct ModelQuote {
  double price = 0.0;
  double normal_vol = 0.0;
};

ModelQuote hullWhiteQuote(const RatesCurve& curve,
                          const HullWhiteParams& params,
                          const RateOptionQuote& quote,
                          int fixed_payments_per_year = 1);

struct CalibrationResult {
  HullWhiteParams params;
  // Per quote, in the order given.
  std::vector<double> market_vols;
  std::vector<double> model_vols;
  // Root mean square of model_vols - market_vols.
  double rms_error = 0.0;
  int iterations = 0;
  bool converged = false;
};

absl::StatusOr<CalibrationResult> calibrateHullWhite(
    const RatesCurve& curve,
    const std::vector<RateOptionQuote>& quotes,
    int fixed_payments_per_year = 1);

}  // namespace smileexplorer

#endif  // SMILEEXPLORER_CALIBRATION_HULL_WHITE_CALIBRATION_H_
