#ifndef SMILEEXPLORER_RATES_ZERO_CURVE_H_
#define SMILEEXPLORER_RATES_ZERO_CURVE_H_

#include <algorithm>
#include <vector>

#include "absl/log/log.h"
#include "rates/rates_curve.h"

namespace smileexplorer {

inline bool isIntegerMultipleOf(double candidate, double divisor) {
  if (divisor == 0) {
    return false;
  }

  double quotient = candidate / divisor;
  int rounded_quotient = std::round(quotient);

  // Hard-code this constant, because in practice the spacing should never be
  // less than a day. If we are within one hour, that should be close enough to
  // consider this an integer multiple.
  constexpr double kOneHourInYearUnits = 1. / (24 * 365.25);
  return std::abs(quotient - rounded_quotient) < kOneHourInYearUnits;
}

inline bool is_monotonically_increasing(const std::vector<double>& range) {
  return std::ranges::adjacent_find(range, std::greater_equal<>{}) ==
         range.end();
}

inline bool areAllSpacingsIntegerMultiplesOf(const std::vector<double>& grid,
                                             double spacing) {
  if (grid.size() < 2 || !is_monotonically_increasing(grid)) {
    return false;
  }

  for (size_t i = 1; i < grid.size(); ++i) {
    if (!isIntegerMultipleOf(grid[i] - grid[i - 1], spacing)) {
      return false;
    }
  }
  return true;
}

class ZeroSpotCurve : public RatesCurve {
 public:
  // Expects two matching vectors in order of increasing maturity. Assumed to be
  // zero-coupon bond (spot) yields.
  ZeroSpotCurve(std::vector<double> maturities,
                std::vector<double> rates,
                CompoundingPeriod period = CompoundingPeriod::kContinuous)
      : maturities_(maturities), rates_(rates), period_(period) {
    computeCurve();
  }
  ~ZeroSpotCurve() override = default;

  double df(double time) const override;

  // TODO: Exposed for testing.
  int findClosestMaturityIndex(double target) const;

  void updateRateAtMaturityIndex(int mat_index, double updated_rate) {
    rates_[mat_index] = updated_rate;
    computeCurve();
  }

  const std::vector<double>& getInputRates() const { return rates_; }

  void generateFineSpacedForwards(double spacing) {
    if (!areAllSpacingsIntegerMultiplesOf(maturities_, spacing)) {
      LOG(ERROR) << "Input maturities are not integer multiples of spacing: "
                 << spacing;
    }

    if (maturities_[0] > 0) {
      // A little trick/hack: if no "time 0" rate (like an overnight rate) was
      // specified to anchor the front of the curve, let's just set it by
      // subtracting out half of the difference between the first two forwards:
      fine_spaced_maturities_.push_back(0);
      double fwd_01 = getForwardRateByIndices(0, 1);
      double fwd_12 = getForwardRateByIndices(1, 2);
      double avg_first_two_fwds_diff = 0.5 * (fwd_12 - fwd_01);
      fine_spaced_forwards_.push_back(fwd_01 - avg_first_two_fwds_diff);
    }

    // TODO: Work in progress.
    // STEP 1:
    // General idea is to come up with a first approximation by subdividing the
    // range between each consecutive pair, and then step up (or down)
    // incrementally. At that point, we are not done, but we can "test" it
    // visually by recomputing the zero spot rates and graphing the resulting
    // forwards. (They should still have a stairstep pattern, but at a much
    // finer resolution.)
    //
    // STEP 2:
    // Then solve a nonlinear optimisation where we try to get as close as
    // possible to these fine-spaced forward approximations, but tweak them up
    // and down to hit the input forwards exactly. This will take a bit more
    // work.
  }

 private:
  std::vector<double> maturities_;
  std::vector<double> rates_;

  std::vector<double> discrete_dfs_;
  std::vector<double> df_maturities_;

  // A regularly spaced grid of forward rates. These will serve as approximate
  // "targets" for the curve-fitting to try to minimise some distance metric.
  // The goal is to try to find an equivalent
  std::vector<double> fine_spaced_maturities_;
  std::vector<double> fine_spaced_forwards_;

  CompoundingPeriod period_;

  // Computes the forward rate between two time indices.
  // For example, if the time spacings are 0.5, then
  // getForwardRateByIndices(2, 4) will return the 1y x 2y fwd rate.
  double getForwardRateByIndices(int start_ti, int end_ti) const;

  void computeCurve();
};

}  // namespace smileexplorer

#endif  // SMILEEXPLORER_RATES_ZERO_CURVE_H_