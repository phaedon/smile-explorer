#include "api/tarf_api.h"

#include <algorithm>
#include <cmath>
#include <numeric>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "derivatives/target_redemption_forward.h"
#include "rates/zero_curve.h"

namespace smileexplorer::api {
namespace {

constexpr int kMaxFixings = 120;
constexpr int kMaxStepsPerFixing = 50;
constexpr int kMaxSteps = 2000;
constexpr int kMaxBatchPaths = 5000;
constexpr int kMaxRecordedPaths = 500;
// A valuation solves for the zero-cost strike by bisection, pricing every
// path at each of a dozen or so candidate strikes.
constexpr int kMaxValuationPaths = 20000;
constexpr double kMaxValuationPathFixings = 1e6;
constexpr int kMaxSweepValues = 20;

int numFixings(const TarfSpec& tarf) {
  return static_cast<int>(std::round(tarf.end_years / tarf.fixing_frequency));
}

absl::Status validate(const TarfSpec& tarf) {
  if (!(tarf.spot > 0) || !std::isfinite(tarf.spot)) {
    return absl::InvalidArgumentError("spot must be positive");
  }
  if (!(tarf.vol >= 0 && tarf.vol <= 2)) {
    return absl::InvalidArgumentError("vol must be between 0 and 2");
  }
  if (absl::Status status = validateCurve(tarf.domestic, "domestic");
      !status.ok()) {
    return status;
  }
  if (absl::Status status = validateCurve(tarf.foreign, "foreign");
      !status.ok()) {
    return status;
  }
  if (!(tarf.notional > 0) || !std::isfinite(tarf.notional)) {
    return absl::InvalidArgumentError("notional must be positive");
  }
  if (!(tarf.target > 0) || !std::isfinite(tarf.target)) {
    return absl::InvalidArgumentError("target must be positive");
  }
  if (!(tarf.strike > 0) || !std::isfinite(tarf.strike)) {
    return absl::InvalidArgumentError("strike must be positive");
  }
  if (!(tarf.leverage >= 1 && tarf.leverage <= 10)) {
    return absl::InvalidArgumentError("leverage must be between 1 and 10");
  }
  if (!(tarf.end_years > 0 && tarf.end_years <= 10)) {
    return absl::InvalidArgumentError(
        "end_years must be positive and at most 10");
  }
  if (!(tarf.fixing_frequency > 0)) {
    return absl::InvalidArgumentError("fixing_frequency must be positive");
  }
  const int n = numFixings(tarf);
  if (n < 1 || n > kMaxFixings ||
      std::abs(n * tarf.fixing_frequency - tarf.end_years) >
          1e-9 * tarf.end_years) {
    return absl::InvalidArgumentError(
        absl::StrCat("fixing_frequency must divide end_years into between 1 "
                     "and ",
                     kMaxFixings,
                     " fixings"));
  }
  if (tarf.steps_per_fixing < 1 || tarf.steps_per_fixing > kMaxStepsPerFixing ||
      n * tarf.steps_per_fixing > kMaxSteps) {
    return absl::InvalidArgumentError(
        absl::StrCat("steps_per_fixing must be between 1 and ",
                     kMaxStepsPerFixing,
                     ", with at most ",
                     kMaxSteps,
                     " steps in all"));
  }
  return absl::OkStatus();
}

absl::Status validate(const TarfValuationRequest& request) {
  if (absl::Status status = validate(request.tarf); !status.ok()) {
    return status;
  }
  if (request.paths < 2 || request.paths > kMaxValuationPaths ||
      static_cast<double>(request.paths) * numFixings(request.tarf) >
          kMaxValuationPathFixings) {
    return absl::InvalidArgumentError(
        absl::StrCat("paths must be between 2 and ",
                     kMaxValuationPaths,
                     ", with at most ",
                     kMaxValuationPathFixings,
                     " fixings over all paths"));
  }
  return absl::OkStatus();
}

TarfContractSpecs contractSpecs(const TarfSpec& tarf) {
  return {.notional = tarf.notional,
          .target = tarf.target,
          .strike = tarf.strike,
          .end_date_years = tarf.end_years,
          .settlement_date_frequency = tarf.fixing_frequency,
          .direction =
              tarf.is_long ? FxTradeDirection::kLong : FxTradeDirection::kShort,
          .leverage = tarf.leverage};
}

ZeroSpotCurve zeroCurve(const RateCurveSpec& curve) {
  return ZeroSpotCurve(curve.maturities, curve.rates);
}

// Values an already-validated request.
TarfValuation value(const TarfValuationRequest& request) {
  const TarfSpec& tarf = request.tarf;
  const ZeroSpotCurve domestic = zeroCurve(tarf.domestic);
  const ZeroSpotCurve foreign = zeroCurve(tarf.foreign);
  const TarfContractSpecs specs = contractSpecs(tarf);

  const std::vector<double> npvs = TargetRedemptionForward(specs)
                                       .price(tarf.spot,
                                              tarf.vol,
                                              tarf.fixing_frequency,
                                              request.paths,
                                              foreign,
                                              domestic,
                                              request.seed)
                                       .path_npvs;
  const double n = npvs.size();
  const double mean = std::accumulate(npvs.begin(), npvs.end(), 0.0) / n;
  double sum_squares = 0.0;
  for (double npv : npvs) sum_squares += (npv - mean) * (npv - mean);

  return {
      .npv = mean,
      .standard_error = std::sqrt(sum_squares / (n - 1) / n),
      .zero_cost_strike = findZeroNPVStrike(specs,
                                            tarf.spot,
                                            tarf.vol,
                                            foreign,
                                            domestic,
                                            request.paths,
                                            request.seed),
      .weighted_forward = weightedAvgForward(
          tarf.spot, tarf.end_years, tarf.fixing_frequency, foreign, domestic)};
}

}  // namespace

absl::StatusOr<TarfBatchResult> simulateTarfBatch(
    const TarfBatchRequest& request) {
  const TarfSpec& tarf = request.tarf;
  if (absl::Status status = validate(tarf); !status.ok()) {
    return status;
  }
  if (request.paths < 1 || request.paths > kMaxBatchPaths) {
    return absl::InvalidArgumentError(
        absl::StrCat("paths must be between 1 and ", kMaxBatchPaths));
  }
  if (request.batch_index < 0) {
    return absl::InvalidArgumentError("batch_index must not be negative");
  }
  if (request.recorded_paths < 0 ||
      request.recorded_paths > std::min(request.paths, kMaxRecordedPaths)) {
    return absl::InvalidArgumentError(
        absl::StrCat("recorded_paths must be between 0 and the smaller of "
                     "paths and ",
                     kMaxRecordedPaths));
  }

  const ZeroSpotCurve domestic = zeroCurve(tarf.domestic);
  const ZeroSpotCurve foreign = zeroCurve(tarf.foreign);
  TarfBatch batch = TargetRedemptionForward(contractSpecs(tarf))
                        .simulate(tarf.spot,
                                  tarf.vol,
                                  tarf.steps_per_fixing,
                                  request.paths,
                                  foreign,
                                  domestic,
                                  request.seed,
                                  static_cast<uint64_t>(request.batch_index),
                                  request.recorded_paths);

  TarfBatchResult result;
  const int steps = batch.num_settlements * batch.steps_per_settlement;
  for (int k = 0; k <= steps; ++k) result.times.push_back(k * batch.dt);
  for (int i = 1; i <= batch.num_settlements; ++i) {
    const double t = i * tarf.fixing_frequency;
    result.fixing_times.push_back(t);
    result.forwards.push_back(tarf.spot * foreign.df(t) / domestic.df(t));
    result.discount_factors.push_back(domestic.df(t));
  }
  for (double npv : batch.npvs) {
    result.npv_sum += npv;
    result.npv_sum_squares += npv * npv;
  }
  result.npvs = std::move(batch.npvs);
  result.knockout_fixings = std::move(batch.knockout_settlements);
  result.recorded_paths = static_cast<int>(batch.recorded_paths);
  result.spots = std::move(batch.spots);
  result.payments = std::move(batch.payments);
  result.accumulated_profits = std::move(batch.accumulated_profits);
  return result;
}

absl::StatusOr<TarfValuation> valueTarf(const TarfValuationRequest& request) {
  if (absl::Status status = validate(request); !status.ok()) {
    return status;
  }
  return value(request);
}

absl::StatusOr<std::vector<TarfValuation>> sweepTarf(
    const TarfSweepRequest& request) {
  if (request.values.empty() || std::ssize(request.values) > kMaxSweepValues) {
    return absl::InvalidArgumentError(absl::StrCat(
        "values must have between 1 and ", kMaxSweepValues, " entries"));
  }
  std::vector<TarfValuation> valuations;
  for (double v : request.values) {
    TarfValuationRequest point = request.valuation;
    switch (request.field) {
      case TarfSweepField::kVol:
        point.tarf.vol = v;
        break;
      case TarfSweepField::kTarget:
        point.tarf.target = v;
        break;
      case TarfSweepField::kLeverage:
        point.tarf.leverage = v;
        break;
    }
    if (absl::Status status = validate(point); !status.ok()) {
      return status;
    }
    valuations.push_back(value(point));
  }
  return valuations;
}

}  // namespace smileexplorer::api
