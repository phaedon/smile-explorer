#include "commodities/futures_curve.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <utility>

namespace smileexplorer {

FuturesCurve::FuturesCurve(double spot,
                           std::vector<double> times,
                           std::vector<double> prices)
    : spot_(spot), times_(std::move(times)), prices_(std::move(prices)) {}

double FuturesCurve::price(double t) const {
  if (t <= 0 || times_.empty()) {
    return spot_;
  }
  // The first pillar at or after t.
  const auto k = std::distance(
      times_.begin(), std::lower_bound(times_.begin(), times_.end(), t));
  if (k == std::ssize(times_)) {
    return prices_.back();
  }
  const double t0 = k == 0 ? 0.0 : times_[k - 1];
  const double f0 = k == 0 ? spot_ : prices_[k - 1];
  const double w = (t - t0) / (times_[k] - t0);
  return std::exp((1 - w) * std::log(f0) + w * std::log(prices_[k]));
}

double FuturesCurve::impliedCarry(double t) const {
  return std::log(price(t) / spot_) / t;
}

}  // namespace smileexplorer
