#ifndef SMILEEXPLORER_RATES_RATES_CURVE_H_
#define SMILEEXPLORER_RATES_RATES_CURVE_H_

#include <vector>

#include "curve_calculators.h"
#include "time.h"

namespace markets {

class RatesCurve {
 public:
  virtual ~RatesCurve() = default;

  // Return discount factor at any time in the future.
  virtual double df(double time) const = 0;

  virtual double forwardRate(double start_time, double end_time) const = 0;

  double forwardDF(double start_time, double end_time) const {
    return df(end_time) / df(start_time);
  }

  double inverseForwardDF(double start_time, double end_time) const {
    return df(start_time) / df(end_time);
  }
};

// Only used for testing. This can be moved to the test lib (along with other
// such things, like fake example vol surfaces with hardcoded params).
class NoDiscountingCurve : public RatesCurve {
 public:
  ~NoDiscountingCurve() override = default;
  double df(double time) const override { return 1.0; }
  double forwardRate(double start_time, double end_time) const override {
    return 0.0;
  }
};

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

  double df(double time) const override {
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
    } else if (ti >= df_maturities_.size() - 1) {
      // Bug fix to extrapolate past the end of the curve.
      ti_left = df_maturities_.size() - 2;
      ti_right = ti_left + 1;
    }

    double fwdrate = getForwardRateByIndices(ti_left, ti_right);
    double dt = time - df_maturities_[ti_left];
    return discrete_dfs_[ti_left] * dfByPeriod(fwdrate, dt, period_);
  }

  double forwardRate(double start_time, double end_time) const override {
    double df_start = df(start_time);
    double df_end = df(end_time);
    double dt = end_time - start_time;
    return fwdRateByPeriod(df_start, df_end, dt, period_);
  }

  // TODO: Exposed for testing.
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

  void updateRateAtMaturityIndex(int mat_index, double updated_rate) {
    rates_[mat_index] = updated_rate;
    computeCurve();
  }

  const std::vector<double>& getInputRates() const { return rates_; }

 private:
  std::vector<double> maturities_;
  std::vector<double> rates_;

  std::vector<double> discrete_dfs_;
  std::vector<double> df_maturities_;

  CompoundingPeriod period_;

  double getForwardRateByIndices(int start_ti, int end_ti) const {
    double df_start = discrete_dfs_[start_ti];
    double df_end = discrete_dfs_[end_ti];
    double dt = df_maturities_[end_ti] - df_maturities_[start_ti];
    return fwdRateByPeriod(df_start, df_end, dt, period_);
  }

  void computeCurve() {
    discrete_dfs_.clear();
    df_maturities_.clear();

    discrete_dfs_.reserve(maturities_.size() + 1);
    df_maturities_.reserve(maturities_.size() + 1);

    // Populate the vectors of the discount factors.
    // DF at time 0.
    discrete_dfs_.push_back(1.0);
    df_maturities_.push_back(0.0);

    for (int i = 0; i < maturities_.size(); ++i) {
      df_maturities_.push_back(maturities_[i]);
      discrete_dfs_.push_back(dfByPeriod(rates_[i], maturities_[i], period_));
    }
  }
};

}  // namespace markets

#endif  // SMILEEXPLORER_RATES_RATES_CURVE_H_