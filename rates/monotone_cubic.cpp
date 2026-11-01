#include "rates/monotone_cubic.h"

#include <algorithm>
#include <iterator>
#include <utility>

namespace smileexplorer {

MonotoneCubicInterpolator::MonotoneCubicInterpolator(std::vector<double> x,
                                                     std::vector<double> y)
    : x_(std::move(x)), y_(std::move(y)), slopes_(x_.size()) {
  const int n = std::ssize(x_);
  const auto secant = [&](int k) {
    return (y_[k + 1] - y_[k]) / (x_[k + 1] - x_[k]);
  };
  slopes_[0] = secant(0);
  slopes_[n - 1] = secant(n - 2);
  for (int k = 1; k < n - 1; ++k) {
    const double h_prev = x_[k] - x_[k - 1];
    const double h_next = x_[k + 1] - x_[k];
    const double d_prev = secant(k - 1);
    const double d_next = secant(k);
    if (d_prev * d_next <= 0) {
      // A local extremum (or a flat side): a flat tangent avoids overshoot.
      slopes_[k] = 0;
    } else {
      const double w1 = 2 * h_next + h_prev;
      const double w2 = h_next + 2 * h_prev;
      slopes_[k] = (w1 + w2) / (w1 / d_prev + w2 / d_next);
    }
  }
}

int MonotoneCubicInterpolator::intervalOf(double x) const {
  const auto it = std::upper_bound(x_.begin(), x_.end(), x);
  const int i = static_cast<int>(std::distance(x_.begin(), it)) - 1;
  return std::clamp(i, 0, static_cast<int>(std::ssize(x_)) - 2);
}

double MonotoneCubicInterpolator::operator()(double x) const {
  const int i = intervalOf(x);
  const double dx = x_[i + 1] - x_[i];
  const double t = (x - x_[i]) / dx;
  // The cubic Hermite basis, in the same arrangement as Boost's.
  return (1 - t) * (1 - t) * (y_[i] * (1 + 2 * t) + slopes_[i] * (x - x_[i])) +
         t * t * (y_[i + 1] * (3 - 2 * t) + dx * slopes_[i + 1] * (t - 1));
}

double MonotoneCubicInterpolator::derivative(double x) const {
  const int i = intervalOf(x);
  const double dx = x_[i + 1] - x_[i];
  const double s0 = slopes_[i];
  const double d1 = (y_[i + 1] - y_[i] - s0 * dx) / (dx * dx);
  const double d2 = (slopes_[i + 1] - s0) / (2 * dx);
  const double c2 = 3 * d1 - 2 * d2;
  const double c3 = 2 * (d2 - d1) / dx;
  const double u = x - x_[i];
  return s0 + 2 * c2 * u + 3 * c3 * u * u;
}

}  // namespace smileexplorer
