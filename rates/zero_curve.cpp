#include "rates/zero_curve.h"

namespace smileexplorer {

// double ZeroSpotCurve::forwardRate(double start_time, double end_time) const {
//   if (end_time == 0) {
//     // Hard-code to 1 month just to prevent division by 0.
//     end_time = 1 / 12.;
//   }
//   double df_start = df(start_time);
//   double df_end = df(end_time);
//   double dt = end_time - start_time;
//   return fwdRateByPeriod(df_start, df_end, dt, period_);
// }

double ZeroSpotCurve::df(double time) const {
  int ti = findClosestMaturityIndex(time);

  if (df_maturities_[ti] == time) {
    return discrete_dfs_[ti];
  }

  // Get the timestamp indices surrounding the requested time.
  int ti_left = ti;
  int ti_right = ti + 1;
  if (df_maturities_[ti] > time) {
    --ti_left;
    --ti_right;
  } else if (ti >= std::ssize(df_maturities_) - 1) {
    // Bug fix to extrapolate past the end of the curve.
    ti_left = df_maturities_.size() - 2;
    ti_right = ti_left + 1;
  }

  double fwdrate = getForwardRateByIndices(ti_left, ti_right);
  double dt = time - df_maturities_[ti_left];
  return discrete_dfs_[ti_left] * dfByPeriod(fwdrate, dt, period_);
}

double ZeroSpotCurve::getForwardRateByIndices(int start_ti, int end_ti) const {
  double df_start = discrete_dfs_[start_ti];
  double df_end = discrete_dfs_[end_ti];
  double dt = df_maturities_[end_ti] - df_maturities_[start_ti];
  return fwdRateByPeriod(df_start, df_end, dt, period_);
}

int ZeroSpotCurve::findClosestMaturityIndex(double target) const {
  int closest_index = 0;
  double min_difference = std::numeric_limits<double>::max();

  for (int i = 0; i < std::ssize(df_maturities_); ++i) {
    double difference = std::abs(df_maturities_[i] - target);
    if (difference < min_difference) {
      min_difference = difference;
      closest_index = i;
    }
  }

  return closest_index;
}

void ZeroSpotCurve::computeCurve() {
  discrete_dfs_.clear();
  df_maturities_.clear();

  discrete_dfs_.reserve(maturities_.size() + 1);
  df_maturities_.reserve(maturities_.size() + 1);

  // Populate the vectors of the discount factors.
  // DF at time 0.
  discrete_dfs_.push_back(1.0);
  df_maturities_.push_back(0.0);

  for (size_t i = 0; i < maturities_.size(); ++i) {
    df_maturities_.push_back(maturities_[i]);
    discrete_dfs_.push_back(dfByPeriod(rates_[i], maturities_[i], period_));
  }
}

}  // namespace smileexplorer
