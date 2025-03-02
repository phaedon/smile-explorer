#ifndef MARKETS_RATES_RATES_CURVE_H_
#define MARKETS_RATES_RATES_CURVE_H_

#include "markets/rates/curve_calculators.h"
#include "markets/time.h"

namespace markets {

/*
template <typename CurveT>
concept RatesCurve = requires(CurveT curve, double time, double dt) {
{ curve.df(time) } -> std::same_as<double>;
{ curve.getForwardRate(time, time + dt) } -> std::same_as<double>;
};
*/

class ZeroSpotCurve {
 public:
  // Expects two matching vectors in order of increasing maturity. Assumed to be
  // zero-coupon bond (spot) yields.
  ZeroSpotCurve(std::vector<double> maturities,
                std::vector<double> rates,
                CompoundingPeriod period = CompoundingPeriod::kContinuous)
      : maturities_(maturities), rates_(rates), period_(period) {
    // Populate the vectors of the discount factors.
    // DF at time 0.
    discrete_dfs_.push_back(1.0);
    df_maturities_.push_back(0.0);

    for (int i = 0; i < maturities.size(); ++i) {
      df_maturities_.push_back(maturities[i]);
      discrete_dfs_.push_back(dfByPeriod(rates_[i], maturities_[i], period));
    }
  }

  double df(double time) const {
    int ti = findClosestMaturityIndex(time);

    if (df_maturities_[ti] == time) {
      return discrete_dfs_[ti];
    }

    // Get the timestamp indices surrounding the requested time.
    int ti_left = ti - 1;
    int ti_right = ti;
    if (df_maturities_[ti] > time) {
      ++ti_left;
      ++ti_right;
    }

    double fwdrate = getForwardRateByIndices(ti_left, ti_right);
    double dt = time - df_maturities_[ti_left];
    return discrete_dfs_[ti_left] * dfByPeriod(fwdrate, dt, period_);
  }

  double getForwardRate(double start_time, double end_time) const {
    double df_start = df(start_time);
    double df_end = df(end_time);
    double dt = end_time - start_time;
    return fwdRateByPeriod(df_start, df_end, dt, period_);
  }

 private:
  std::vector<double> maturities_;
  std::vector<double> rates_;

  std::vector<double> discrete_dfs_;
  std::vector<double> df_maturities_;

  CompoundingPeriod period_;

  int findClosestMaturityIndex(double target) const {
    int closest_index = 0;
    double min_difference = std::numeric_limits<double>::max();

    for (int i = 0; i < df_maturities_.size(); ++i) {
      double difference = std::abs(df_maturities_[i] - target);
      if (difference < min_difference) {
        min_difference = difference;
        closest_index = i;
      }
    }

    return closest_index;
  }

  double getForwardRateByIndices(int start_ti, int end_ti) const {
    double df_start = discrete_dfs_[start_ti];
    double df_end = discrete_dfs_[end_ti];
    double dt = df_maturities_[end_ti] - df_maturities_[start_ti];
    return fwdRateByPeriod(df_start, df_end, dt, period_);
  }
};

// Monostate simply allows no discounting at all.
using RatesCurve = std::variant<std::monostate, ZeroSpotCurve>;

}  // namespace markets

#endif  // MARKETS_RATES_RATES_CURVE_H_