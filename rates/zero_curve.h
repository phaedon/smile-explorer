#ifndef SMILEEXPLORER_RATES_ZERO_CURVE_H_
#define SMILEEXPLORER_RATES_ZERO_CURVE_H_

#include <algorithm>
#include <boost/math/interpolators/pchip.hpp>
#include <memory>
#include <optional>
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

using SplineInterpolator =
    boost::math::interpolators::pchip<std::vector<double>>;

class ZeroSpotCurve : public RatesCurve {
 public:
  // Expects two matching vectors in order of increasing maturity. Assumed to be
  // zero-coupon bond (spot) yields.
  ZeroSpotCurve(std::vector<double> maturities,
                std::vector<double> rates,
                CompoundingPeriod period = CompoundingPeriod::kContinuous,
                CurveInterpolationStyle interp_style =
                    CurveInterpolationStyle::kConstantForwards)
      : maturities_(maturities),
        rates_(rates),
        period_(period),
        interp_style_(interp_style) {
    updateSpline();
    computeCurve();
  }
  ~ZeroSpotCurve() override = default;

  double df(double time) const override;

  // TODO: Exposed for testing.
  int findClosestMaturityIndex(double target) const;

  void updateRateAtMaturityIndex(int mat_index, double updated_rate) {
    rates_[mat_index] = updated_rate;
    updateSpline();
    computeCurve();
  }

  const std::vector<double>& getInputRates() const { return rates_; }

 private:
  std::vector<double> maturities_;
  std::vector<double> rates_;

  std::vector<double> discrete_dfs_;
  std::vector<double> df_maturities_;

  CompoundingPeriod period_;
  CurveInterpolationStyle interp_style_;

  std::unique_ptr<SplineInterpolator> spline_;

  // Computes the forward rate between two time indices.
  // For example, if the time spacings are 0.5, then
  // getForwardRateByIndices(2, 4) will return the 1y x 2y fwd rate.
  double getForwardRateByIndices(int start_ti, int end_ti) const;

  void computeCurve();

  void updateSpline() {
    if (maturities_.size() >= 4 && rates_.size() >= 4) {
      std::vector<double> spline_maturities(maturities_);
      std::vector<double> spline_rates(rates_);
      spline_ = std::make_unique<SplineInterpolator>(
          std::move(spline_maturities), std::move(spline_rates));
    }
  }
};

}  // namespace smileexplorer

#endif  // SMILEEXPLORER_RATES_ZERO_CURVE_H_