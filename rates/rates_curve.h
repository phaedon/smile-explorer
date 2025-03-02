#ifndef MARKETS_RATES_RATES_CURVE_H_
#define MARKETS_RATES_RATES_CURVE_H_

#include <variant>

#include "markets/binomial_tree.h"
#include "markets/propagators.h"
#include "markets/rates/arrow_debreu.h"
#include "markets/time.h"
#include "markets/volatility.h"

namespace markets {

inline double dfByPeriod(double r, double dt, CompoundingPeriod period) {
  switch (period) {
    case CompoundingPeriod::kContinuous:
      return std::exp(-r * dt);
    case CompoundingPeriod::kAnnual:
      return 1. / std::pow(1 + r, dt);
    case CompoundingPeriod::kSemi:
      return 1. / std::pow(1 + 0.5 * r, dt * 2);
    case CompoundingPeriod::kQuarterly:
      return 1. / std::pow(1 + 0.25 * r, dt * 4);
    case CompoundingPeriod::kMonthly:
      return 1. / std::pow(1 + (1. / 12) * r, dt * 12);
  }
}

inline double fwdRateByPeriod(double df_start,
                              double df_end,
                              double dt,
                              CompoundingPeriod period) {
  double df_ratio = df_start / df_end;
  switch (period) {
    case CompoundingPeriod::kContinuous:
      return std::log(df_ratio) / dt;
    case CompoundingPeriod::kAnnual:
      return std::pow(df_ratio, 1. / dt) - 1;
    case CompoundingPeriod::kSemi:
      return 2 * (std::pow(df_ratio, 1. / (2 * dt)) - 1);
    case CompoundingPeriod::kQuarterly:
      return 4 * (std::pow(df_ratio, 1. / (4 * dt)) - 1);
    case CompoundingPeriod::kMonthly:
      return 12 * (std::pow(df_ratio, 1. / (12 * dt)) - 1);
  }
}

class SimpleUncalibratedShortRatesCurve {
 public:
  SimpleUncalibratedShortRatesCurve(double time_span_years, double timestep)
      : rate_tree_(time_span_years, timestep) {
    // Default hard-coded constants for initialisation.
    updateCurve(0.05, 0.10);
  }

  void updateCurve(double spot_rate, double vol) {
    FlatVol flat_vol(vol);
    Volatility rate_vol(flat_vol);
    CRRPropagator crr_prop(spot_rate);
    rate_tree_.forwardPropagate(crr_prop, rate_vol);

    ArrowDebreauPropagator arrowdeb_prop(rate_tree_, rate_tree_.numTimesteps());
    arrowdeb_tree_ = BinomialTree::createFrom(rate_tree_);
    arrowdeb_tree_.forwardPropagate(arrowdeb_prop);

    timegrid_ = rate_tree_.getTimegrid();
  }

  double getForwardRate(double start_time, double end_time) const {
    // TODO Add some error handling for cases like end_time <= start_time, etc.
    double df_start = df(start_time);
    double df_end = df(end_time);
    double dt = end_time - start_time;
    return fwdRateByPeriod(
        df_start, df_end, dt, CompoundingPeriod::kContinuous);
  }

  double df(double time) const {
    // TODO add error handling
    int ti = timegrid_.getTimeIndexForExpiry(time).value();

    if (timegrid_.time(ti) == time) {
      return arrowdeb_tree_.sumAtTimestep(ti);
    }

    // Get the timestamp indices surrounding the requested time.
    int ti_left = ti - 1;
    int ti_right = ti;
    if (timegrid_.time(ti) > time) {
      ++ti_left;
      ++ti_right;
    }

    double fwdrate = getForwardRateByIndices(ti_left, ti_right);
    double dt = time - timegrid_.time(ti_left);
    return arrowdeb_tree_.sumAtTimestep(ti_left) *
           dfByPeriod(fwdrate, dt, CompoundingPeriod::kContinuous);
  }

 private:
  BinomialTree rate_tree_;
  BinomialTree arrowdeb_tree_;
  Timegrid timegrid_;

  double getForwardRateByIndices(int start_ti, int end_ti) const {
    double df_start = arrowdeb_tree_.sumAtTimestep(start_ti);
    double df_end = arrowdeb_tree_.sumAtTimestep(end_ti);
    double dt = timegrid_.time(end_ti) - timegrid_.time(start_ti);
    return fwdRateByPeriod(
        df_start, df_end, dt, CompoundingPeriod::kContinuous);
  }
};

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

using RatesCurve =
    std::variant<SimpleUncalibratedShortRatesCurve, ZeroSpotCurve>;

}  // namespace markets

#endif  // MARKETS_RATES_RATES_CURVE_H_