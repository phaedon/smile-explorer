#include "target_redemption_forward.h"

#include <algorithm>
#include <limits>

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

std::mt19937_64 tarfRng(uint64_t seed, uint64_t stream) {
  std::seed_seq seq{static_cast<uint32_t>(seed),
                    static_cast<uint32_t>(seed >> 32),
                    static_cast<uint32_t>(stream),
                    static_cast<uint32_t>(stream >> 32)};
  return std::mt19937_64(seq);
}

TarfBatch TargetRedemptionForward::simulate(double spot,
                                            double sigma,
                                            int steps_per_settlement,
                                            size_t num_paths,
                                            const RatesCurve& foreign_rates,
                                            const RatesCurve& domestic_rates,
                                            uint64_t seed,
                                            uint64_t stream,
                                            size_t recorded_paths) const {
  constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
  TarfBatch batch;
  const int n = numSettlements();
  const int m = std::max(1, steps_per_settlement);
  const double period = specs_.settlement_date_frequency;
  const double dt = period / m;
  const size_t row = static_cast<size_t>(n) * m + 1;
  recorded_paths = std::min(recorded_paths, num_paths);

  batch.num_settlements = n;
  batch.steps_per_settlement = m;
  batch.dt = dt;
  batch.npvs.resize(num_paths);
  batch.knockout_settlements.assign(num_paths, -1);
  batch.recorded_paths = recorded_paths;
  batch.spots.assign(recorded_paths * row, kNaN);
  batch.payments.assign(recorded_paths * n, kNaN);
  batch.accumulated_profits.assign(recorded_paths * n, kNaN);

  // The drift of log(FX) per timestep, from the forward rates over each
  // settlement period (looked up once per period rather than per step), and
  // the discount factor to each settlement. Note that in a production system,
  // we would use a real calendar which would cause these settlement periods
  // to vary slightly due to weekends, holidays, and variable month lengths.
  std::vector<double> log_drift(n);
  std::vector<double> df(n);
  for (int i = 0; i < n; ++i) {
    const double t_init = i * period;
    const double t_final = (i + 1) * period;
    const double rd = domestic_rates.forwardRate(t_init, t_final);
    const double rf = foreign_rates.forwardRate(t_init, t_final);
    log_drift[i] = (rd - rf - 0.5 * sigma * sigma) * dt;
    df[i] = domestic_rates.df(t_final);
  }
  const double diffusion = sigma * std::sqrt(dt);

  std::mt19937_64 rng = tarfRng(seed, stream);
  std::normal_distribution<double> normal;
  for (size_t p = 0; p < num_paths; ++p) {
    const bool record = p < recorded_paths;
    double fx = spot;
    double cumulative_profit = 0.0;
    double npv = 0.0;
    if (record) batch.spots[p * row] = spot;

    for (int i = 0; i < n; ++i) {
      for (int j = 0; j < m; ++j) {
        fx *= std::exp(log_drift[i] + diffusion * normal(rng));
        if (record) batch.spots[p * row + i * m + j + 1] = fx;
      }

      const double gain = directionFactor() * (fx - specs_.strike);
      double payment =
          specs_.notional * gain * (gain < 0 ? specs_.leverage : 1.0);

      // If we reach the target on this payment date, then the current payment
      // is truncated to deliver the exact amount remaining to hit the target.
      bool knocked_out = false;
      if (cumulative_profit + payment > specs_.target) {
        knocked_out = true;
        payment = specs_.target - cumulative_profit;
      }
      // Only gains count towards the target.
      if (payment > 0) cumulative_profit += payment;
      npv += payment * df[i];

      if (record) {
        batch.payments[p * n + i] = payment;
        batch.accumulated_profits[p * n + i] = cumulative_profit;
      }
      if (knocked_out) {
        batch.knockout_settlements[p] = i;
        break;
      }
    }
    batch.npvs[p] = npv;
  }
  return batch;
}

TarfPricingResult TargetRedemptionForward::price(
    double spot,
    double sigma,
    double dt,
    size_t num_paths,
    // Convention: fx rate is quoted as FOR-DOM:
    const RatesCurve& foreign_rates,
    const RatesCurve& domestic_rates,
    uint64_t seed) const {
  TarfPricingResult result;
  if (num_paths == 0) return result;

  // dt shouldn't be larger than the periodic settlements (for example if the
  // settlements are quarterly, then dt <= 0.25), and is rounded so that it
  // divides each settlement period exactly.
  const double period = specs_.settlement_date_frequency;
  const int steps_per_settlement =
      dt > 0 ? std::max(1, static_cast<int>(std::round(period / dt))) : 1;

  result.path_npvs = simulate(spot,
                              sigma,
                              steps_per_settlement,
                              num_paths,
                              foreign_rates,
                              domestic_rates,
                              seed)
                         .npvs;
  for (size_t i = 0; i < num_paths; ++i) {
    // Compute the online mean at each step for numerical stability.
    result.mean_npv +=
        (result.path_npvs[i] - result.mean_npv) / static_cast<double>(i + 1);
  }
  return result;
}

double findZeroNPVStrike(const TarfContractSpecs& specs,
                         double spot,
                         double sigma,
                         const RatesCurve& foreign_rates,
                         const RatesCurve& domestic_rates,
                         size_t num_paths,
                         uint64_t seed) {
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

  // One timestep per settlement: the payments depend only on the FX rate at
  // the settlements, and each step of the simulation is exact for
  // lognormal FX, so finer steps would only cost time.
  double dt = specs.settlement_date_frequency;

  // Initial method: bisection.
  while (std::abs(k_high / k_low - 1) > tolerance_pct) {
    TargetRedemptionForward tarf_mid(k_mid_specs);

    double npv_mid =
        tarf_mid
            .price(
                spot, sigma, dt, num_paths, foreign_rates, domestic_rates, seed)
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
