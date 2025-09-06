#ifndef SMILEEXPLORER_DERIVATIVES_TARGET_REDEMPTION_FORWARD_H_
#define SMILEEXPLORER_DERIVATIVES_TARGET_REDEMPTION_FORWARD_H_

#include <cmath>

#include "absl/log/log.h"
#include "absl/random/random.h"

// Pricing model for Target Redemption Forward (TARF)

namespace smileexplorer {

struct TarfPathInfo {
  bool target_reached;
  double target_duration;
  double npv;
};

class TargetRedemptionForward {
 public:
  TargetRedemptionForward(float notional,
                          float target,
                          float strike,
                          double end_date_years,
                          double settlement_date_frequency)
      : notional_(notional),
        target_(target),
        strike_(strike),
        end_date_years_(end_date_years),
        settlement_date_frequency_(settlement_date_frequency) {}

  // Initial implementation: given a flat volatility and a specified forward,
  // price the scenarios using the provided discount curve. (It is up to the
  // client to ensure that the current discount curve is provided, otherwise a
  // quanto adjustment would be required.)
  double path(double spot,
              double sigma,
              double dt,
              absl::BitGen& bitgen) const {
    // Each path should return not just the NPV, but also
    // - the distribution of payments (right?)
    // - whether it got knocked out, and when

    double cumulative_profit = 0.;
    double npv = 0.;

    // Temporary placeholder: hardcode the rates.
    double r_f = 0.04;
    double r_d = 0.08;

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
    while (t < end_date_years_ && !trigger_reached) {
      double z = absl::Gaussian<double>(bitgen, 0, 1);
      double stoch_term = sigma * std::sqrt(dt) * z;
      double drift_term = (r_d - r_f - 0.5 * sigma * sigma) * dt;

      t += dt;
      ++timesteps_taken;
      fx *= std::exp(drift_term + stoch_term);

      if (timesteps_taken == num_timesteps_in_period) {
        float payment_amount = notional_ * (fx - strike_);

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
        double discounted_pmt = payment_amount * std::exp(-r_d * t);
        npv += discounted_pmt;

        // LOG(INFO) << " t:" << t << "  fx:" << fx
        //           << "   pmt amt:" << payment_amount
        //           << "   discounted:" << discounted_pmt;

        // Reset to the next period.
        timesteps_taken = 0;
      }
    }
    return npv;
  }

  double price(double spot, double sigma, double dt, size_t num_paths) const {
    absl::BitGen bitgen;
    if (num_paths == 0) return 0.;

    double mean_npv = 0.;
    for (size_t i = 1; i <= num_paths; ++i) {
      double path_npv = path(spot, sigma, dt, bitgen);
      // Compute the online mean at each step for numerical stability.
      mean_npv += (path_npv - mean_npv) / static_cast<double>(i);
    }
    return mean_npv;
  }

 private:
  float notional_;
  float target_;
  float strike_;
  double end_date_years_;
  double settlement_date_frequency_;
};

}  // namespace smileexplorer

#endif  //  SMILEEXPLORER_DERIVATIVES_TARGET_REDEMPTION_FORWARD_H_