#ifndef SMILEEXPLORER_DERIVATIVES_TARGET_REDEMPTION_FORWARD_H_
#define SMILEEXPLORER_DERIVATIVES_TARGET_REDEMPTION_FORWARD_H_

#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

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

  // Multiplies the notional on settlements where the customer loses money.
  // TARFs are commonly sold with a leverage of 2: the customer's losses
  // accrue at twice the rate of the gains, which in turn are capped by the
  // target.
  double leverage = 1.0;
};

struct TarfPricingResult {
  double mean_npv = 0.0;
  std::vector<double> path_npvs;
};

// The paths simulated by one call to TargetRedemptionForward::simulate. Every
// path has an NPV and a knockout settlement; the first `recorded_paths` are
// also recorded in full, for plotting.
struct TarfBatch {
  int num_settlements = 0;
  int steps_per_settlement = 0;
  // Length of each timestep, in years.
  double dt = 0.0;

  // NPV of each path, in the domestic currency, discounted on the domestic
  // curve.
  std::vector<double> npvs;
  // The settlement (0 for the first) at which each path reached its target,
  // or -1 if it never did.
  std::vector<int> knockout_settlements;

  size_t recorded_paths = 0;
  // The FX rate at each timestep, including time 0: recorded_paths rows of
  // num_settlements * steps_per_settlement + 1. NaN after the knockout.
  std::vector<double> spots;
  // The (undiscounted) payment at each settlement, positive when the customer
  // gains: recorded_paths rows of num_settlements. NaN after the knockout.
  std::vector<double> payments;
  // The profit accumulated towards the target after each settlement, in the
  // same layout as `payments`.
  std::vector<double> accumulated_profits;
};

class TargetRedemptionForward {
 public:
  TargetRedemptionForward(const TarfContractSpecs& specs) : specs_(specs) {}

  // Simulates `num_paths` paths of the FX rate under flat vol, with
  // `steps_per_settlement` timesteps between settlements. The random numbers
  // come only from `seed` and `stream`, so a run split into batches (one
  // stream per batch) is reproducible however it is paused and resumed.
  TarfBatch simulate(double spot,
                     double sigma,
                     int steps_per_settlement,
                     size_t num_paths,
                     // Convention: fx rate is quoted as FOR-DOM:
                     const RatesCurve& foreign_rates,
                     const RatesCurve& domestic_rates,
                     uint64_t seed,
                     uint64_t stream = 0,
                     size_t recorded_paths = 0) const;

  // Assumes volatility is flat and constant. `dt` is rounded so that a whole
  // number of timesteps fits in each settlement period.
  TarfPricingResult price(double spot,
                          double sigma,
                          double dt,
                          size_t num_paths,
                          // Convention: fx rate is quoted as FOR-DOM:
                          const RatesCurve& foreign_rates,
                          const RatesCurve& domestic_rates,
                          uint64_t seed = 1) const;

  int numSettlements() const {
    return static_cast<int>(
        std::round(specs_.end_date_years / specs_.settlement_date_frequency));
  }

 private:
  TarfContractSpecs specs_;

  double directionFactor() const {
    return specs_.direction == FxTradeDirection::kLong ? 1.0 : -1.0;
  }
};

// The random-number generator for one stream of a seeded run.
std::mt19937_64 tarfRng(uint64_t seed, uint64_t stream);

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
// appropriate strike. Every candidate strike is priced on the same paths
// (common random numbers), so the bisection isn't misled by Monte Carlo
// noise.
double findZeroNPVStrike(const TarfContractSpecs& specs,
                         double spot,
                         double sigma,
                         const RatesCurve& foreign_rates,
                         const RatesCurve& domestic_rates,
                         size_t num_paths = 4000,
                         uint64_t seed = 1);

}  // namespace smileexplorer

#endif  //  SMILEEXPLORER_DERIVATIVES_TARGET_REDEMPTION_FORWARD_H_
