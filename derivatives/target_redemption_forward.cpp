#include "target_redemption_forward.h"
namespace smileexplorer {

/*
As a simple example, suppose spot is at 100 and there are 3 payment periods,
with the following forward FX rates and the following discount factors from the
`domestic_rates` curve:

t (yrs)   fwd     df
-------   ---     ---
  1       102     0.95
  2       104     0.90
  3       106     0.85

Then this function would return ~103.9259
or in spreadsheet pseudocode:
    = sumproduct(fwd, df)/sum(df)
*/
double weightedAvgForward(double spot,
                          double end_date_years,
                          double settlement_date_frequency,
                          const RatesCurve& foreign_rates,
                          const RatesCurve& domestic_rates) {
  double sumproduct = 0.;
  double fx = spot;
  double df_sum = 0.;

  int num_payments = std::round(end_date_years / settlement_date_frequency);
  for (int i = 1; i <= num_payments; ++i) {
    double t_init = (i - 1) * settlement_date_frequency;
    double t_final = i * settlement_date_frequency;
    double rd = domestic_rates.forwardRate(t_init, t_final);
    double rf = foreign_rates.forwardRate(t_init, t_final);
    fx *= std::exp((rd - rf) * settlement_date_frequency);
    sumproduct += fx * domestic_rates.df(t_final);
    df_sum += domestic_rates.df(t_final);
  }

  // Sanity check in case of degenerate case.
  if (df_sum <= 0.) return 0.;

  return sumproduct / df_sum;
}

double TargetRedemptionForward::path(double spot,
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
    const double drift_term = (r_d - r_f - 0.5 * sigma * sigma) * dt;

    t += dt;
    ++timesteps_taken;
    fx *= std::exp(drift_term + stoch_term);

    if (timesteps_taken == num_timesteps_in_period) {
      double payment_amount = direction_multiplier * notional_ * (fx - strike_);

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

TarfPricingResult TargetRedemptionForward::price(
    double spot,
    double sigma,
    double dt,
    size_t num_paths,
    // Convention: fx rate is quoted as FOR-DOM:
    const RatesCurve& foreign_rates,
    const RatesCurve& domestic_rates) const {
  TarfPricingResult result;
  if (num_paths == 0) return result;

  result.path_npvs.reserve(num_paths);
  result.mean_npv = 0.;
  for (size_t i = 1; i <= num_paths; ++i) {
    double path_npv = path(spot, sigma, dt, foreign_rates, domestic_rates);
    result.path_npvs.push_back(path_npv);
    // Compute the online mean at each step for numerical stability.
    result.mean_npv += (path_npv - result.mean_npv) / static_cast<double>(i);
  }
  return result;
}

}  // namespace smileexplorer
