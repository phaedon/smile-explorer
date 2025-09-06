#ifndef SMILEEXPLORER_DERIVATIVES_TARGET_REDEMPTION_FORWARD_H_
#define SMILEEXPLORER_DERIVATIVES_TARGET_REDEMPTION_FORWARD_H_

#include <cmath>

#include "absl/log/log.h"
#include "absl/random/random.h"
#include "rates/rates_curve.h"

// Pricing model for Target Redemption Forward (TARF)

namespace smileexplorer {

struct TarfPathInfo {
  bool target_reached;
  double target_duration;
  double npv;
};

enum class FxTradeDirection { kLong, kShort };

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

  // Initial implementation: given a flat volatility and a specified forward,
  // price the scenarios using the provided discount curve. (It is up to the
  // client to ensure that the current discount curve is provided, otherwise a
  // quanto adjustment would be required.)
  double path(double spot,
              double sigma,
              double dt,
              const RatesCurve& foreign_rates,
              const RatesCurve& domestic_rates) const {
    // Each path should return not just the NPV, but also
    // - the distribution of payments (right?)
    // - whether it got knocked out, and when

    const double direction_multiplier =
        (direction_ == FxTradeDirection::kLong) ? 1.0 : -1.0;

    double cumulative_profit = 0.;
    double npv = 0.;

    if (dt > settlement_date_frequency_) {
      // This ensures that dt is (at most) the maximum sensible value. It
      // shouldn't be larger than the periodic settlements. For example if the
      // settlements are quarterly (0.25) then dt <= 0.25.
      dt = settlement_date_frequency_;
    }

    // Ensure that dt is an integer fraction of the settlement date frequency
    // so that it aligns precisely. Note that in a production system, we would
    // use a real calendar which would cause these settlement periods to vary
    // slightly due to weekends, holidays, and variable month lengths.
    auto num_timesteps_in_period = std::round(settlement_date_frequency_ / dt);
    dt = settlement_date_frequency_ / num_timesteps_in_period;

    double fx = spot;
    double t = 0;
    double timesteps_taken = 0;
    bool trigger_reached = false;

    double r_d = domestic_rates.forwardRate(t, t + settlement_date_frequency_);
    double r_f = foreign_rates.forwardRate(t, t + settlement_date_frequency_);

    while (t < end_date_years_ && !trigger_reached) {
      const double z = absl::Gaussian<double>(bitgen_, 0, 1);
      const double stoch_term = sigma * std::sqrt(dt) * z;
      //   double r_d = domestic_rates.forwardRate(t, t + dt);
      //   double r_f = foreign_rates.forwardRate(t, t + dt);
      const double drift_term = (r_d - r_f - 0.5 * sigma * sigma) * dt;

      t += dt;
      ++timesteps_taken;
      fx *= std::exp(drift_term + stoch_term);

      if (timesteps_taken == num_timesteps_in_period) {
        double payment_amount =
            direction_multiplier * notional_ * (fx - strike_);

        // If we reach the target on this payment date, then the current
        // payment is truncated to deliver the exact amount remaining
        // to hit the target.
        if (cumulative_profit + payment_amount > target_) {
          trigger_reached = true;
          payment_amount = target_ - cumulative_profit;
        }

        if (payment_amount > 0) {
          cumulative_profit += payment_amount;
        }

        // Discount the payment amount on the domestic curve.
        const double discounted_pmt = payment_amount * domestic_rates.df(t);
        npv += discounted_pmt;

        // Reset to the next period.
        timesteps_taken = 0;

        // And look up the forward interest rates for the next simulation
        // period (we don't do this at each time step to avoid computing these
        // excessively, in case dt is very small).
        r_d = domestic_rates.forwardRate(t, t + settlement_date_frequency_);
        r_f = foreign_rates.forwardRate(t, t + settlement_date_frequency_);
      }
    }
    return npv;
  }

  double price(double spot,
               double sigma,
               double dt,
               size_t num_paths,
               // Convention: fx rate is quoted as FOR-DOM:
               const RatesCurve& foreign_rates,
               const RatesCurve& domestic_rates) const {
    if (num_paths == 0) return 0.;

    double mean_npv = 0.;
    for (size_t i = 1; i <= num_paths; ++i) {
      double path_npv = path(spot, sigma, dt, foreign_rates, domestic_rates);
      // Compute the online mean at each step for numerical stability.
      mean_npv += (path_npv - mean_npv) / static_cast<double>(i);
    }
    return mean_npv;
  }

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

}  // namespace smileexplorer

#endif  //  SMILEEXPLORER_DERIVATIVES_TARGET_REDEMPTION_FORWARD_H_