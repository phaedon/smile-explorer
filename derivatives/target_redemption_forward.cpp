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

void TargetRedemptionForward::processSettlement(
    PathState& state,
    const RatesCurve& foreign_rates,
    const RatesCurve& domestic_rates) const {
  double payment_amount =
      directionFactor() * specs_.notional * (state.current_fx - specs_.strike);

  // If we reach the target on this payment date, then the current payment is
  // truncated to deliver the exact amount remaining to hit the target.
  if (state.cumulative_profit + payment_amount > specs_.target) {
    state.trigger_reached = true;
    payment_amount = specs_.target - state.cumulative_profit;
  }

  if (payment_amount > 0) {
    state.cumulative_profit += payment_amount;
  }

  // Discount the payment amount on the domestic curve.
  const double discounted_pmt =
      payment_amount * domestic_rates.df(state.current_time);
  state.npv += discounted_pmt;

  // Reset to the next period.
  state.timesteps_since_last_settlement = 0;

  // And look up the forward interest rates for the next simulation period (we
  // don't do this at each time step to avoid computing these excessively, in
  // case dt is very small).
  state.fwd_int_rate_domestic = domestic_rates.forwardRate(
      state.current_time,
      state.current_time + specs_.settlement_date_frequency);
  state.fwd_int_rate_foreign = foreign_rates.forwardRate(
      state.current_time,
      state.current_time + specs_.settlement_date_frequency);
}

double TargetRedemptionForward::path(double spot,
                                     double sigma,
                                     double dt,
                                     const RatesCurve& foreign_rates,
                                     const RatesCurve& domestic_rates) const {
  PathState state;
  state.current_fx = spot;

  if (dt > specs_.settlement_date_frequency) {
    // This ensures that dt is (at most) the maximum sensible value. It
    // shouldn't be larger than the periodic settlements. For example if the
    // settlements are quarterly (0.25) then dt <= 0.25.
    dt = specs_.settlement_date_frequency;
  }

  // Ensure that dt is an integer fraction of the settlement date frequency
  // so that it aligns precisely. Note that in a production system, we would
  // use a real calendar which would cause these settlement periods to vary
  // slightly due to weekends, holidays, and variable month lengths.
  auto num_timesteps_in_period =
      std::round(specs_.settlement_date_frequency / dt);
  dt = specs_.settlement_date_frequency / num_timesteps_in_period;

  state.fwd_int_rate_domestic = domestic_rates.forwardRate(
      state.current_time,
      state.current_time + specs_.settlement_date_frequency);
  state.fwd_int_rate_foreign = foreign_rates.forwardRate(
      state.current_time,
      state.current_time + specs_.settlement_date_frequency);

  while (state.current_time < specs_.end_date_years && !state.trigger_reached) {
    const double z = absl::Gaussian<double>(bitgen_, 0, 1);
    const double stoch_term = sigma * std::sqrt(dt) * z;
    const double drift_term =
        (state.fwd_int_rate_domestic - state.fwd_int_rate_foreign -
         0.5 * sigma * sigma) *
        dt;

    state.current_time += dt;
    ++state.timesteps_since_last_settlement;
    state.current_fx *= std::exp(drift_term + stoch_term);

    if (state.timesteps_since_last_settlement == num_timesteps_in_period) {
      processSettlement(state, foreign_rates, domestic_rates);
    }
  }
  return state.npv;
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

double findZeroNPVStrike(const TarfContractSpecs& specs,
                         double spot,
                         double sigma,
                         const RatesCurve& foreign_rates,
                         const RatesCurve& domestic_rates,
                         size_t num_paths) {
  // TODO: This implementation is more of a placeholder for a more generic
  // bisection strategy that is not tightly coupled to a specific product. As
  // such, this has some hard-coded values (tolerances, etc) which actually need
  // to be made robust. The focus in this initial sprint was on getting the
  // mechanics and pricing of TARF to be correct.
  double atm_fwd = weightedAvgForward(spot,
                                      specs.end_date_years,
                                      specs.settlement_date_frequency,
                                      foreign_rates,
                                      domestic_rates);
  double k_low = atm_fwd * 0.5;
  double k_high = atm_fwd * 1.5;

  TarfContractSpecs k_mid_specs = specs;
  k_mid_specs.strike = 0.5 * (k_low + k_high);

  double tolerance_pct =
      0.0001;  // 0.01% difference for starters. Do not hard-code!

  // Relatively coarse timesteps.
  double dt = specs.settlement_date_frequency * 0.2;

  // Initial method: bisection.
  while (std::abs(k_high / k_low - 1) > tolerance_pct) {
    TargetRedemptionForward tarf_mid(k_mid_specs);

    double npv_mid =
        tarf_mid
            .price(spot, sigma, dt, num_paths, foreign_rates, domestic_rates)
            .mean_npv;

    if (specs.direction == FxTradeDirection::kLong) {
      if (npv_mid > 0) {
        k_low = k_mid_specs.strike;
      } else {
        k_high = k_mid_specs.strike;
      }
    } else {  // FxTradeDirection::kShort
      if (npv_mid > 0) {
        k_high = k_mid_specs.strike;
      } else {
        k_low = k_mid_specs.strike;
      }
    }
    k_mid_specs.strike = 0.5 * (k_low + k_high);
  }
  return k_mid_specs.strike;
}

}  // namespace smileexplorer
