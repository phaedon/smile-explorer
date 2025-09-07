#ifndef SMILEEXPLORER_DERIVATIVES_TARGET_REDEMPTION_FORWARD_H_
#define SMILEEXPLORER_DERIVATIVES_TARGET_REDEMPTION_FORWARD_H_

#include <cmath>

#include "absl/log/log.h"
#include "absl/random/random.h"
#include "rates/rates_curve.h"

// Pricing model for Target Redemption Forward (TARF)

namespace smileexplorer {

// "Long" and "short" here denotes the position in the underlying FOR-DOM FX
// rate, from the perspective of the TARF customer (typically, the client of the
// bank who is the "buyer" of the TARF feature, giving up potential upside and
// locking in an advantageous forward rate in exchange).
enum class FxTradeDirection { kLong, kShort };

struct TarfContractSpecs {
  // Notional of the contract, denominated in the "foreign" currency (where the
  // FX rate is quoted as FOR-DOM).
  double notional;

  // Target cumulative profit of the contract, denominated in the "domestic"
  // currency (FX rate: FOR-DOM)
  double target;

  double strike;
  double end_date_years;

  // Periodic settlement/payment duration, expressed in units of years.
  // For example:
  //  = 0.25 for quarterly
  //  = 0.5 for semiannual
  //  = 1.0 for annual
  double settlement_date_frequency;

  FxTradeDirection direction;
};

struct TarfPricingResult {
  double mean_npv;
  std::vector<double> path_npvs;
};

class TargetRedemptionForward {
 public:
  TargetRedemptionForward(const TarfContractSpecs& specs) : specs_(specs) {}

  // Initial implementation: Assumes volatility is flat and constant.
  double path(double spot,
              double sigma,
              double dt,
              const RatesCurve& foreign_rates,
              const RatesCurve& domestic_rates) const;

  TarfPricingResult price(double spot,
                          double sigma,
                          double dt,
                          size_t num_paths,
                          // Convention: fx rate is quoted as FOR-DOM:
                          const RatesCurve& foreign_rates,
                          const RatesCurve& domestic_rates) const;

 private:
  TarfContractSpecs specs_;

  // This is set to "mutable" to enable the pricing methods to retain their
  // const annotation, while still being able to access the random number
  // generator (whose internal state is updated every time a random number is
  // invoked).
  mutable absl::BitGen bitgen_;

  // Stores the internal state of a single MC simulation path.
  struct PathState {
    double cumulative_profit = 0.0;
    double npv = 0.0;
    double current_fx = 0.0;
    double current_time = 0.0;
    size_t timesteps_since_last_settlement = 0;
    bool trigger_reached = false;
  };
  void processSettlement(PathState& state,
                         const RatesCurve& domestic_rates) const;
};

// Returns the weighted average of the forward FX rate, weighted by the
// discount factors.
double weightedAvgForward(double spot,
                          double end_date_years,
                          double settlement_date_frequency,
                          const RatesCurve& foreign_rates,
                          const RatesCurve& domestic_rates);

// Returns the (estimated) strike at which the TARF contract would have
// a zero NPV at the time of the trade.
//
// Note that `specs.strike` is ignored, since the goal is to discover the
// appropriate strike.
double findZeroNPVStrike(const TarfContractSpecs& specs,
                         double spot,
                         double sigma,
                         const RatesCurve& foreign_rates,
                         const RatesCurve& domestic_rates,
                         size_t num_paths = 4000);

}  // namespace smileexplorer

#endif  //  SMILEEXPLORER_DERIVATIVES_TARGET_REDEMPTION_FORWARD_H_