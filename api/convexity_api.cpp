#include "api/convexity_api.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "api/hull_white_tree.h"
#include "derivatives/convexity_hedge.h"
#include "derivatives/short_rate_rollback.h"
#include "derivatives/sofr_futures.h"
#include "rates/zero_curve.h"

namespace smileexplorer::api {
namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr int kMaxStripContracts = 200;
constexpr double kMaxYears = 10.0;
constexpr int kMaxSteps = 3000;
constexpr int kMaxBatchPaths = 5000;
constexpr int kMaxRecordedPaths = 200;

absl::Status validateModel(double mean_reversion,
                           double sigma,
                           const char* sigma_name) {
  if (!(mean_reversion >= 1e-3 && mean_reversion <= 2)) {
    return absl::InvalidArgumentError(
        "mean_reversion must be between 0.001 and 2");
  }
  if (!(sigma > 0 && sigma <= 0.05)) {
    return absl::InvalidArgumentError(
        absl::StrCat(sigma_name, " must be positive and at most 0.05"));
  }
  return absl::OkStatus();
}

absl::Status validatePeriod(double start, double end) {
  if (!(start >= 0) || !(end > start) || !(end <= kMaxYears)) {
    return absl::InvalidArgumentError(absl::StrCat(
        "the period must start no earlier than today and end after it "
        "starts, within ",
        kMaxYears,
        " years"));
  }
  return absl::OkStatus();
}

// The time index of `years` on a grid of step dt, if it falls on one.
std::optional<int> gridIndex(double years, double dt) {
  const double steps = years / dt;
  const double rounded = std::round(steps);
  if (std::abs(steps - rounded) > 1e-9 * std::max(1.0, steps)) {
    return std::nullopt;
  }
  return static_cast<int>(rounded);
}

}  // namespace

absl::StatusOr<FuturesStripResult> futuresStrip(
    const FuturesStripRequest& request) {
  if (absl::Status s = validateCurve(request.curve, "curve"); !s.ok()) {
    return s;
  }
  if (absl::Status s =
          validateModel(request.mean_reversion, request.sigma, "sigma");
      !s.ok()) {
    return s;
  }
  if (request.starts.empty() ||
      std::ssize(request.starts) > kMaxStripContracts) {
    return absl::InvalidArgumentError(absl::StrCat(
        "give between 1 and ", kMaxStripContracts, " contract starts"));
  }
  if (!(request.period_years > 0 && request.period_years <= 1)) {
    return absl::InvalidArgumentError(
        "period_years must be positive and at most 1");
  }
  const ZeroSpotCurve curve(request.curve.maturities, request.curve.rates);
  const double a = request.mean_reversion;
  const double sigma = request.sigma;
  FuturesStripResult result;
  for (double start : request.starts) {
    const double end = start + request.period_years;
    if (absl::Status s = validatePeriod(start, end); !s.ok()) return s;
    const double forward = overnightForwardRate(curve, start, end);
    const double futures = sofrFuturesRate(curve, a, sigma, start, end);
    const double term = termRateFuturesRate(curve, a, sigma, start, end);
    result.starts.push_back(start);
    result.ends.push_back(end);
    result.forward_rates.push_back(forward);
    result.futures_rates.push_back(futures);
    result.term_futures_rates.push_back(term);
    result.adjustments.push_back(futures - forward);
    result.term_adjustments.push_back(term - forward);
  }
  return result;
}

absl::StatusOr<FuturesTreeResult> futuresOnTree(
    const FuturesTreeRequest& request) {
  if (absl::Status s = validatePeriod(request.start_years, request.end_years);
      !s.ok()) {
    return s;
  }
  const double dt = request.tree.dt_years;
  if (!(dt > 0)) {
    return absl::InvalidArgumentError("dt_years must be positive");
  }
  const std::optional<int> start_index = gridIndex(request.start_years, dt);
  const std::optional<int> end_index = gridIndex(request.end_years, dt);
  if (!start_index.has_value() || !end_index.has_value()) {
    return absl::InvalidArgumentError(
        "the period's start and end must fall on the tree's steps");
  }
  HullWhiteTreeSpec spec = request.tree;
  spec.horizon_years = request.end_years;
  absl::StatusOr<FittedTree> fitted = fitTree(spec);
  if (!fitted.ok()) return fitted.status();
  const TrinomialTree& tree = fitted->tree_curve->trinomialTree();
  const int s = *start_index;
  const int e = *end_index;
  if (e != std::ssize(tree.tree_) - 1) {
    return absl::InternalError("unexpected number of time slices");
  }
  const double tau = request.end_years - request.start_years;

  FuturesTreeResult result;
  result.start_index = s;
  result.end_index = e;
  const auto compounding = expectedCompounding(tree, s, e);
  const auto to_start = bondPrices(tree, s);
  const auto to_end = bondPrices(tree, e);
  const HullWhiteTreeResult& nodes = fitted->result;
  result.futures_rates.assign(nodes.slice_offsets.back(), kNaN);
  result.forward_rates.assign(nodes.slice_offsets.back(), kNaN);
  for (int t = 0; t <= s; ++t) {
    for (int i = 0; i < tree.numStatesAt(t); ++i) {
      const int n = nodes.slice_offsets[t] + i;
      result.futures_rates[n] = (compounding[t][i] - 1) / tau;
      result.forward_rates[n] = (to_start[t][i] / to_end[t][i] - 1) / tau;
    }
  }
  result.futures_rate = result.futures_rates[0];
  result.forward_rate = result.forward_rates[0];
  const ZeroSpotCurve& curve = *fitted->market_curve;
  result.analytic_futures_rate = sofrFuturesRate(curve,
                                                 spec.mean_reversion,
                                                 spec.sigma,
                                                 request.start_years,
                                                 request.end_years);
  result.analytic_forward_rate =
      overnightForwardRate(curve, request.start_years, request.end_years);
  result.tree = std::move(fitted->result);
  return result;
}

absl::StatusOr<ConvexityHedgeResult> simulateConvexityHedgeBatch(
    const ConvexityHedgeRequest& request) {
  const ConvexityHedgeSpec& spec = request.spec;
  if (absl::Status s = validateCurve(spec.curve, "curve"); !s.ok()) return s;
  if (absl::Status s = validateModel(
          spec.mean_reversion, spec.implied_sigma, "implied_sigma");
      !s.ok()) {
    return s;
  }
  if (absl::Status s = validateModel(
          spec.mean_reversion, spec.realized_sigma, "realized_sigma");
      !s.ok()) {
    return s;
  }
  if (absl::Status s = validatePeriod(spec.start_years, spec.end_years);
      !s.ok()) {
    return s;
  }
  if (!(spec.notional > 0) || !std::isfinite(spec.notional)) {
    return absl::InvalidArgumentError("notional must be positive");
  }
  if (spec.hedge_style < 0 || spec.hedge_style > 3) {
    return absl::InvalidArgumentError("hedge_style must be between 0 and 3");
  }
  if (spec.steps_per_year < 4 || spec.steps_per_year > 365 ||
      spec.end_years * spec.steps_per_year > kMaxSteps) {
    return absl::InvalidArgumentError(
        absl::StrCat("steps_per_year must be between 4 and 365, with at most ",
                     kMaxSteps,
                     " steps in all"));
  }
  // The period must span at least one step.
  if (std::round(spec.end_years * spec.steps_per_year) <=
      std::round(spec.start_years * spec.steps_per_year)) {
    return absl::InvalidArgumentError(
        "the period must be at least one step long");
  }
  if (request.paths < 1 || request.paths > kMaxBatchPaths) {
    return absl::InvalidArgumentError(
        absl::StrCat("paths must be between 1 and ", kMaxBatchPaths));
  }
  if (request.recorded_paths < 0 ||
      request.recorded_paths > std::min(request.paths, kMaxRecordedPaths)) {
    return absl::InvalidArgumentError(absl::StrCat(
        "recorded_paths must be between 0 and the number of paths, and at "
        "most ",
        kMaxRecordedPaths));
  }
  if (request.record_every < 1) {
    return absl::InvalidArgumentError("record_every must be at least 1");
  }
  if (request.batch_index < 0) {
    return absl::InvalidArgumentError("batch_index must not be negative");
  }

  const ZeroSpotCurve curve(spec.curve.maturities, spec.curve.rates);
  ConvexityTradeSpec trade;
  trade.mean_reversion = spec.mean_reversion;
  trade.implied_sigma = spec.implied_sigma;
  trade.realized_sigma = spec.realized_sigma;
  trade.start = spec.start_years;
  trade.end = spec.end_years;
  trade.notional = spec.notional;
  if (spec.fixed_at_futures_rate) {
    // On the simulation's grid, as the batch will snap the period.
    const double dt = 1.0 / spec.steps_per_year;
    trade.fixed_rate = sofrFuturesRate(curve,
                                       spec.mean_reversion,
                                       spec.implied_sigma,
                                       std::round(spec.start_years / dt) * dt,
                                       std::round(spec.end_years / dt) * dt);
  }
  trade.hedge = static_cast<FuturesHedgeStyle>(spec.hedge_style);
  trade.steps_per_year = spec.steps_per_year;

  ConvexityBatch batch = simulateConvexityHedge(curve,
                                                trade,
                                                request.paths,
                                                request.seed,
                                                request.batch_index,
                                                request.recorded_paths);

  ConvexityHedgeResult result;
  result.forward_rate = batch.forward_rate;
  result.futures_rate = batch.futures_rate;
  result.fixed_rate = batch.fixed_rate;
  result.fra_value = batch.fra_value;
  result.start_years = batch.start_index * batch.dt;
  result.end_years = batch.num_steps * batch.dt;
  result.end_discount_factor = curve.df(result.end_years);
  result.adjustment_value =
      spec.notional * (result.end_years - result.start_years) *
      (batch.futures_rate - batch.forward_rate) * result.end_discount_factor;
  for (size_t p = 0; p < batch.pnls.size(); ++p) {
    const double pnl = batch.pnls[p];
    const double pai = batch.no_pai_pnls[p] - batch.fra_pnls[p];
    result.pnl_sum += pnl;
    result.pnl_sum_squares += pnl * pnl;
    result.pai_value_sum += pai;
    result.pai_value_sum_squares += pai * pai;
  }
  result.pnls = std::move(batch.pnls);
  result.fra_pnls = std::move(batch.fra_pnls);
  result.futures_pnls = std::move(batch.futures_pnls);
  result.no_pai_pnls = std::move(batch.no_pai_pnls);

  // Thin the recorded rows to every record_every steps, and the last.
  std::vector<int> kept;
  for (int k = 0; k < batch.num_steps; k += request.record_every) {
    kept.push_back(k);
  }
  kept.push_back(batch.num_steps);
  for (int k : kept) result.times.push_back(k * batch.dt);
  result.recorded_paths = static_cast<int>(batch.recorded_paths);
  const size_t row = batch.num_steps + 1;
  const auto thin = [&](const std::vector<double>& from,
                        std::vector<double>& to) {
    to.reserve(batch.recorded_paths * kept.size());
    for (size_t p = 0; p < batch.recorded_paths; ++p) {
      for (int k : kept) to.push_back(from[p * row + k]);
    }
  };
  thin(batch.short_rates, result.short_rates);
  thin(batch.futures_rates, result.futures_rates);
  thin(batch.forward_rates, result.forward_rates);
  thin(batch.hedge_ratios, result.hedge_ratios);
  thin(batch.fra_values, result.fra_values);
  thin(batch.fra_cash, result.fra_cash);
  thin(batch.no_pai_cash, result.no_pai_cash);
  thin(batch.futures_cash, result.futures_cash);
  return result;
}

}  // namespace smileexplorer::api
