#ifndef MARKETS_RATES_RATES_CURVE_H_
#define MARKETS_RATES_RATES_CURVE_H_

#include <variant>

#include "markets/binomial_tree.h"
#include "markets/propagators.h"
#include "markets/rates/arrow_debreu.h"
#include "markets/time.h"
#include "markets/volatility.h"

namespace markets {

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

    rate_tree_.printUpTo(6);
    arrowdeb_tree_.printUpTo(6);
  }

  double getForwardRateByIndices(int start_ti, int end_ti) const {
    double df_start = arrowdeb_tree_.sumAtTimestep(start_ti);
    double df_end = arrowdeb_tree_.sumAtTimestep(end_ti);
    return std::log(df_start / df_end) /
           (timegrid_.time(end_ti) - timegrid_.time(start_ti));
  }

  double getForwardRate(double start_time, double end_time) {
    // TODO Add some error handling for cases like end_time <= start_time, etc.
    double df_start = df(start_time);
    double df_end = df(end_time);
    double df_ratio = df_start / df_end;
    double dt = end_time - start_time;
    // if (period_ == CompoundingPeriod::kContinuous) {
    return std::log(df_ratio) / dt;
    // } else if (period_ == CompoundingPeriod::kAnnual) {
    //   return std::pow(df_ratio, 1. / dt) - 1;
    // }
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
    return arrowdeb_tree_.sumAtTimestep(ti_left) *
           std::exp(-fwdrate * (time - timegrid_.time(ti_left)));
  }

 private:
  BinomialTree rate_tree_;
  BinomialTree arrowdeb_tree_;
  Timegrid timegrid_;
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
      if (period == CompoundingPeriod::kContinuous) {
        discrete_dfs_.push_back(std::exp(-rates_[i] * maturities_[i]));
      } else if (period == CompoundingPeriod::kAnnual) {
        discrete_dfs_.push_back(1.0 / std::pow(1 + rates_[i], maturities_[i]));
      }
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
    if (period_ == CompoundingPeriod::kContinuous) {
      return discrete_dfs_[ti_left] * std::exp(-fwdrate * dt);
    } else if (period_ == CompoundingPeriod::kAnnual) {
      return discrete_dfs_[ti_left] / std::pow(1 + fwdrate, dt);
    }
  }

  double getForwardRate(double start_time, double end_time) {
    double df_start = df(start_time);
    double df_end = df(end_time);
    double df_ratio = df_start / df_end;
    double dt = end_time - start_time;
    if (period_ == CompoundingPeriod::kContinuous) {
      return std::log(df_ratio) / dt;
    } else if (period_ == CompoundingPeriod::kAnnual) {
      return std::pow(df_ratio, 1. / dt) - 1;
    }
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
    double df_ratio = df_start / df_end;
    double dt = df_maturities_[end_ti] - df_maturities_[start_ti];
    if (period_ == CompoundingPeriod::kContinuous) {
      return std::log(df_ratio) / dt;
    } else if (period_ == CompoundingPeriod::kAnnual) {
      return std::pow(df_ratio, 1. / dt) - 1;
    }
  }
};

using RatesCurve =
    std::variant<SimpleUncalibratedShortRatesCurve, ZeroSpotCurve>;

}  // namespace markets

#endif  // MARKETS_RATES_RATES_CURVE_H_