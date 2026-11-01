#ifndef SMILEEXPLORER_API_TARF_API_H_
#define SMILEEXPLORER_API_TARF_API_H_

#include <cstdint>
#include <vector>

#include "absl/status/statusor.h"
#include "api/pricing_api.h"

// The runtime API for target redemption forwards (TARFs), priced by Monte
// Carlo under flat vol. A run is split into batches, each seeded by the run's
// seed and its own index, so that a caller can simulate it a batch at a time
// (and pause between batches) with the same result as in one go.

namespace smileexplorer::api {

// A TARF on an exchange rate quoted FOR-DOM, from the customer's side. At
// each fixing the customer exchanges `notional` units of the foreign currency
// at `strike`: buying them if `is_long`, selling them otherwise. Fixings that
// gain count towards `target` (in the domestic currency), and the contract
// ends at the fixing that reaches it, with that payment cut to land exactly on
// the target. Fixings that lose pay `leverage` times the notional.
struct TarfSpec {
  double spot = 1.1;
  double vol = 0.08;
  RateCurveSpec domestic;
  RateCurveSpec foreign;

  double notional = 1e6;
  double target = 5e4;
  double strike = 1.1;
  double leverage = 2.0;
  double end_years = 1.0;
  // Years between fixings; must divide end_years.
  double fixing_frequency = 1 / 12.;
  bool is_long = true;

  // Timesteps between fixings. Prices depend only on the fixings (each step
  // is exact for lognormal FX), so more steps only draw smoother paths.
  int steps_per_fixing = 1;
};

struct TarfBatchRequest {
  TarfSpec tarf;
  int paths = 250;
  uint64_t seed = 1;
  // Which batch of the run this is (0 for the first).
  int batch_index = 0;
  // The number of paths, from the first, to return in full.
  int recorded_paths = 0;
};

struct TarfBatchResult {
  // Each timestep, from 0, and each fixing, in years.
  std::vector<double> times;
  std::vector<double> fixing_times;
  // The forward FX rate and the domestic discount factor at each fixing.
  std::vector<double> forwards;
  std::vector<double> discount_factors;

  // Per path: the NPV (domestic currency) and the fixing (0 for the first) at
  // which the target was reached, or -1.
  std::vector<double> npvs;
  std::vector<int> knockout_fixings;
  // Sums over the batch's NPVs and their squares, to combine batches.
  double npv_sum = 0.0;
  double npv_sum_squares = 0.0;

  // The recorded paths, row by row: the FX rate at each of `times` (NaN after
  // the knockout), and the payment and the profit accumulated towards the
  // target at each fixing (NaN after the knockout).
  int recorded_paths = 0;
  std::vector<double> spots;
  std::vector<double> payments;
  std::vector<double> accumulated_profits;
};

absl::StatusOr<TarfBatchResult> simulateTarfBatch(
    const TarfBatchRequest& request);

struct TarfValuationRequest {
  TarfSpec tarf;
  int paths = 4000;
  uint64_t seed = 1;
};

struct TarfValuation {
  // At the spec's strike: the NPV and its Monte Carlo standard error.
  double npv = 0.0;
  double standard_error = 0.0;
  // The strike at which the NPV is zero, and the discount-weighted average of
  // the forwards (the zero-cost strike of a plain strip of forwards).
  double zero_cost_strike = 0.0;
  double weighted_forward = 0.0;
};

// Prices on the same paths for every strike (common random numbers).
absl::StatusOr<TarfValuation> valueTarf(const TarfValuationRequest& request);

enum class TarfSweepField { kVol, kTarget, kLeverage };

// Values the TARF with one field set to each of `values` in turn, every time
// on the same random numbers, so that the differences between them aren't
// Monte Carlo noise.
struct TarfSweepRequest {
  TarfValuationRequest valuation;
  TarfSweepField field = TarfSweepField::kVol;
  std::vector<double> values;
};

absl::StatusOr<std::vector<TarfValuation>> sweepTarf(
    const TarfSweepRequest& request);

}  // namespace smileexplorer::api

#endif  // SMILEEXPLORER_API_TARF_API_H_
