#ifndef SMILEEXPLORER_DERIVATIVES_TARGET_REDEMPTION_FORWARD_H_
#define SMILEEXPLORER_DERIVATIVES_TARGET_REDEMPTION_FORWARD_H_

#include <cmath>

#include "absl/log/log.h"
#include "absl/random/random.h"
#include "rates/rates_curve.h"

// Pricing model for Target Redemption Forward (TARF)

namespace smileexplorer {

// Returns the weighted average of the forward FX rate, weighted by the
// discount factors.
double weightedAvgForward(double spot,
                          double end_date_years,
                          double settlement_date_frequency,
                          const RatesCurve& foreign_rates,
                          const RatesCurve& domestic_rates);

struct TarfPathInfo {
  bool target_reached;
  double target_duration;
  double npv;
};

enum class FxTradeDirection { kLong, kShort };

struct TarfPricingResult {
  double mean_npv;
  std::vector<double> path_npvs;
};

class TargetRedemptionForward {
 public:
  TargetRedemptionForward(double notional,
                          double target,
                          double strike,
                          double end_date_years,
                          double settlement_date_frequency,
                          FxTradeDirection direction)
      : notional_(notional),
        target_(target),
        strike_(strike),
        end_date_years_(end_date_years),
        settlement_date_frequency_(settlement_date_frequency),
        direction_(direction) {}

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
  double notional_;
  double target_;
  double strike_;
  double end_date_years_;
  double settlement_date_frequency_;
  FxTradeDirection direction_;

  // This is set to "mutable" to enable the pricing methods to retain their
  // const annotation, while still being able to access the random number
  // generator (whose internal state is updated every time a random number is
  // invoked).
  mutable absl::BitGen bitgen_;
};

inline double findZeroNPVStrike(double notional,
                                double target,
                                double end_date_years,
                                double settlement_date_frequency,
                                FxTradeDirection direction,
                                double spot,
                                double sigma,
                                const RatesCurve& foreign_rates,
                                const RatesCurve& domestic_rates,
                                size_t num_paths = 2000) {
  // TODO: ALSO then verify that the value of one is positive and the other is
  // negative.
  double atm_fwd = weightedAvgForward(spot,
                                      end_date_years,
                                      settlement_date_frequency,
                                      foreign_rates,
                                      domestic_rates);
  double k_low = atm_fwd * 0.5;
  double k_high = atm_fwd * 1.1;
  double k_mid = 0.5 * (k_low + k_high);

  double tolerance_pct =
      0.0001;  // 0.01% difference for starters. Do not hard-code!

  // Relatively coarse timesteps.
  double dt = settlement_date_frequency * 0.2;

  // Initial method: bisection.
  while (std::abs(k_high / k_low - 1) > tolerance_pct) {
    TargetRedemptionForward tarf_mid(notional,
                                     target,
                                     k_mid,
                                     end_date_years,
                                     settlement_date_frequency,
                                     direction);

    double npv_mid = tarf_mid.price(spot, sigma, dt, num_paths, foreign_rates, domestic_rates).mean_npv;

    if (npv_mid > 0) {
      k_low = k_mid;
    } else if (npv_mid < 0) {
      k_high = k_mid;
    }
    k_mid = 0.5 * (k_low + k_high);
  }
  return k_mid;
}

}  // namespace smileexplorer

#endif  //  SMILEEXPLORER_DERIVATIVES_TARGET_REDEMPTION_FORWARD_H_