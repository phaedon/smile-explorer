#include "derivatives/convexity_hedge.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "derivatives/sofr_futures.h"

namespace smileexplorer {

std::mt19937_64 convexityRng(uint64_t seed, uint64_t stream) {
  std::seed_seq seq{static_cast<uint32_t>(seed),
                    static_cast<uint32_t>(seed >> 32),
                    static_cast<uint32_t>(stream),
                    static_cast<uint32_t>(stream >> 32)};
  return std::mt19937_64(seq);
}

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

// Every mark in the implied model is the exponential of something affine in
// the state x: exp(c - b x) for bond prices, exp(c + b x) for the futures'
// expected compounding. These are the coefficients at one time index.
struct Marks {
  double end_bond_c = 0.0;
  double end_bond_b = 0.0;
  // Bond to the start of the period (before it starts).
  double start_bond_c = 0.0;
  double start_bond_b = 0.0;
  double futures_c = 0.0;
  double futures_b = 0.0;
  // phi(t) in the implied model, less phi(t) in the realized one: the shift
  // from a realized state to the implied state with the same short rate.
  double state_shift = 0.0;
  // The realized model's phi(t), and its integral over the step that follows.
  double realized_phi = 0.0;
  double realized_phi_integral = 0.0;
};

// The values at one time index on one path.
struct State {
  double end_bond = 0.0;
  double compounding = 0.0;  // P(t, start) before the period, then accrued
  double value = 0.0;        // the FRA's
  double futures_rate = 0.0;
  double forward_rate = 0.0;
  double hedge = 0.0;  // the tailed hedge ratio
};

}  // namespace

ConvexityBatch simulateConvexityHedge(const RatesCurve& curve,
                                      const ConvexityTradeSpec& spec,
                                      size_t num_paths,
                                      uint64_t seed,
                                      uint64_t stream,
                                      size_t recorded_paths) {
  using namespace hull_white_internal;
  const double a = spec.mean_reversion;
  const double si = spec.implied_sigma;
  const double sr = spec.realized_sigma;
  const double dt = 1.0 / std::max(1, spec.steps_per_year);
  const int n = std::max(1, static_cast<int>(std::round(spec.end / dt)));
  const int s_idx =
      std::clamp(static_cast<int>(std::round(spec.start / dt)), 0, n - 1);
  const double start = s_idx * dt;
  const double end = n * dt;
  const double tau = end - start;
  const double notional = spec.notional;
  const size_t row = n + 1;
  recorded_paths = std::min(recorded_paths, num_paths);

  ConvexityBatch batch;
  batch.num_steps = n;
  batch.dt = dt;
  batch.start_index = s_idx;
  batch.forward_rate = overnightForwardRate(curve, start, end);
  batch.futures_rate = sofrFuturesRate(curve, a, si, start, end);
  const double k_fixed =
      std::isnan(spec.fixed_rate) ? batch.forward_rate : spec.fixed_rate;
  batch.fixed_rate = k_fixed;
  batch.fra_value =
      notional * ((1 + tau * k_fixed) * curve.df(end) - curve.df(start));
  batch.fra_pnls.resize(num_paths);
  batch.no_pai_pnls.resize(num_paths);
  batch.futures_pnls.resize(num_paths);
  batch.pnls.resize(num_paths);
  batch.recorded_paths = recorded_paths;
  for (auto* v : {&batch.short_rates,
                  &batch.futures_rates,
                  &batch.forward_rates,
                  &batch.hedge_ratios,
                  &batch.fra_values,
                  &batch.fra_cash,
                  &batch.no_pai_cash,
                  &batch.futures_cash}) {
    v->assign(recorded_paths * row, kNaN);
  }

  std::vector<Marks> marks(row);
  for (int k = 0; k <= n; ++k) {
    const double t = k * dt;
    Marks& m = marks[k];
    const GaussianMoments to_end =
        hullWhiteIntegralMoments(curve, a, si, t, 0.0, t, end);
    m.end_bond_c = -to_end.mean + to_end.variance / 2;
    m.end_bond_b = decay(a, end - t);
    if (k <= s_idx) {
      const GaussianMoments to_start =
          hullWhiteIntegralMoments(curve, a, si, t, 0.0, t, start);
      m.start_bond_c = -to_start.mean + to_start.variance / 2;
      m.start_bond_b = decay(a, start - t);
    }
    const double u = std::max(t, start);
    const GaussianMoments fut =
        hullWhiteIntegralMoments(curve, a, si, t, 0.0, u, end);
    m.futures_c = fut.mean + fut.variance / 2;
    m.futures_b = std::exp(-a * (u - t)) * decay(a, end - u);
    m.realized_phi = hullWhiteShortRate(curve, a, sr, t, 0.0);
    m.state_shift = m.realized_phi - hullWhiteShortRate(curve, a, si, t, 0.0);
    if (k < n) {
      m.realized_phi_integral =
          std::log(curve.df(t) / curve.df(t + dt)) +
          sr * sr / (2 * a * a) * phiCorrectionIntegral(a, t, t + dt);
    }
  }

  // Marks the contracts at time index k, given the realized state x and the
  // log of the compounding accrued since the start of the period.
  const auto mark = [&](int k, double x, double accrued) {
    const Marks& m = marks[k];
    const double xi = x + m.state_shift;
    State s;
    s.end_bond = std::exp(m.end_bond_c - m.end_bond_b * xi);
    double compounding_slope = 0.0;
    if (k <= s_idx) {
      s.compounding = std::exp(m.start_bond_c - m.start_bond_b * xi);
      compounding_slope = -m.start_bond_b * s.compounding;
    } else {
      s.compounding = std::exp(accrued);
    }
    s.value = notional * ((1 + tau * k_fixed) * s.end_bond - s.compounding);
    const double log_growth =
        (k > s_idx ? accrued : 0.0) + m.futures_c + m.futures_b * xi;
    const double growth = std::exp(log_growth);
    s.futures_rate = std::expm1(log_growth) / tau;
    s.forward_rate = (s.compounding / s.end_bond - 1) / tau;
    // Sensitivities to x of the FRA and of one future's margin; sell enough
    // futures to cancel the FRA's.
    const double value_slope =
        notional *
        (-(1 + tau * k_fixed) * m.end_bond_b * s.end_bond - compounding_slope);
    const double futures_slope = notional * growth * m.futures_b;
    s.hedge = futures_slope > 0 ? -value_slope / futures_slope : 0.0;
    return s;
  };

  // The step's shocks to x and to its integral are jointly Gaussian.
  const double decay_x = std::exp(-a * dt);
  const double b_step = decay(a, dt);
  const double var_x = sr * sr * decay(2 * a, dt);
  const double var_i = sr * sr * squaredDecayIntegral(a, dt);
  const double cov = sr * sr / a * (decay(a, dt) - decay(2 * a, dt));
  const double sd_x = std::sqrt(var_x);
  const double load_i = cov / sd_x;
  const double sd_i = std::sqrt(std::max(0.0, var_i - load_i * load_i));

  std::mt19937_64 rng = convexityRng(seed, stream);
  std::normal_distribution<double> normal;
  const State first = mark(0, 0.0, 0.0);
  for (size_t p = 0; p < num_paths; ++p) {
    const bool record = p < recorded_paths;
    double x = 0.0;
    double accrued = 0.0;
    double log_bank = 0.0;
    State s = first;
    // The FRA's margin at inception is its value (zero at market).
    double fra_cash = s.value;
    double no_pai_cash = s.value;
    double futures_cash = 0.0;
    const auto hedgeAt = [&](const State& st) {
      switch (spec.hedge) {
        case FuturesHedgeStyle::kTailed:
          return st.hedge;
        case FuturesHedgeStyle::kUntailed:
          return st.hedge / st.end_bond;
        case FuturesHedgeStyle::kStatic:
          return first.hedge;
        case FuturesHedgeStyle::kNone:
          return 0.0;
      }
      return 0.0;
    };
    const auto recordAt = [&](int k, const State& st, double h) {
      const size_t i = p * row + k;
      batch.short_rates[i] = x + marks[k].realized_phi;
      batch.futures_rates[i] = st.futures_rate;
      batch.forward_rates[i] = st.forward_rate;
      batch.hedge_ratios[i] = h;
      batch.fra_values[i] = st.value;
      batch.fra_cash[i] = fra_cash;
      batch.no_pai_cash[i] = no_pai_cash;
      batch.futures_cash[i] = futures_cash;
    };

    for (int k = 0; k < n; ++k) {
      const double h = hedgeAt(s);
      if (record) recordAt(k, s, h);
      const double z1 = normal(rng);
      const double z2 = normal(rng);
      const double integral =
          marks[k].realized_phi_integral + x * b_step + load_i * z1 + sd_i * z2;
      x = x * decay_x + sd_x * z1;
      log_bank += integral;
      if (k >= s_idx) accrued += integral;
      const State next = mark(k + 1, x, accrued);
      const double growth = std::exp(integral);
      // Margin moves the FRA's value across; PAI charges interest on the
      // balance held, which was the old value.
      fra_cash = fra_cash * growth + next.value - s.value * growth;
      no_pai_cash = no_pai_cash * growth + next.value - s.value;
      // Short futures gain when the futures rate rises.
      futures_cash = futures_cash * growth +
                     h * notional * tau * (next.futures_rate - s.futures_rate);
      s = next;
    }
    if (record) recordAt(n, s, 0.0);

    const double deflator = std::exp(-log_bank);
    batch.fra_pnls[p] = fra_cash * deflator;
    batch.no_pai_pnls[p] = no_pai_cash * deflator;
    batch.futures_pnls[p] = futures_cash * deflator;
    batch.pnls[p] = (fra_cash + futures_cash) * deflator;
  }
  return batch;
}

}  // namespace smileexplorer
