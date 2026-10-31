#ifndef SMILEEXPLORER_CALIBRATION_NELDER_MEAD_H_
#define SMILEEXPLORER_CALIBRATION_NELDER_MEAD_H_

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

namespace smileexplorer {

template <size_t N>
struct NelderMeadResult {
  std::array<double, N> x;
  double value;
  int iterations;
};

// Minimises f over N variables by the Nelder-Mead simplex method, from
// `start`, with an initial simplex extending `step` along each axis. Stops
// after `max_iterations`, or when the simplex's values differ by less than
// `tolerance` and its points by less than 1e-9.
//
// Deterministic and derivative-free: suited to the small, smooth calibration
// problems here (a few parameters, cheap objective).
template <size_t N, typename F>
NelderMeadResult<N> nelderMead(F f,
                               std::array<double, N> start,
                               std::array<double, N> step,
                               int max_iterations,
                               double tolerance) {
  using Point = std::array<double, N>;
  std::array<Point, N + 1> x;
  std::array<double, N + 1> fx;
  x[0] = start;
  for (size_t k = 0; k < N; ++k) {
    x[k + 1] = start;
    x[k + 1][k] += step[k];
  }
  for (size_t k = 0; k <= N; ++k) fx[k] = f(x[k]);

  // a + t (b - a)
  const auto combine = [](const Point& a, const Point& b, double t) {
    Point p;
    for (size_t k = 0; k < N; ++k) p[k] = a[k] + t * (b[k] - a[k]);
    return p;
  };

  int iteration = 0;
  for (; iteration < max_iterations; ++iteration) {
    // Order: best first, worst last.
    std::array<size_t, N + 1> order;
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](size_t i, size_t j) {
      return fx[i] < fx[j];
    });
    std::array<Point, N + 1> sorted_x;
    std::array<double, N + 1> sorted_fx;
    for (size_t k = 0; k <= N; ++k) {
      sorted_x[k] = x[order[k]];
      sorted_fx[k] = fx[order[k]];
    }
    x = sorted_x;
    fx = sorted_fx;

    double spread = 0.0;
    for (size_t k = 0; k < N; ++k) spread += std::abs(x[N][k] - x[0][k]);
    if (std::abs(fx[N] - fx[0]) < tolerance && spread < 1e-9) break;

    // The centroid of all but the worst point.
    Point centroid{};
    for (size_t k = 0; k < N; ++k) {
      for (size_t d = 0; d < N; ++d) centroid[d] += x[k][d] / N;
    }
    const Point reflected = combine(centroid, x[N], -1.0);
    const double f_reflected = f(reflected);
    if (f_reflected < fx[0]) {
      const Point expanded = combine(centroid, x[N], -2.0);
      const double f_expanded = f(expanded);
      if (f_expanded < f_reflected) {
        x[N] = expanded;
        fx[N] = f_expanded;
      } else {
        x[N] = reflected;
        fx[N] = f_reflected;
      }
    } else if (f_reflected < fx[N - 1]) {
      x[N] = reflected;
      fx[N] = f_reflected;
    } else {
      const bool outside = f_reflected < fx[N];
      const Point contracted =
          combine(centroid, outside ? reflected : x[N], 0.5);
      const double f_contracted = f(contracted);
      if (f_contracted < std::min(f_reflected, fx[N])) {
        x[N] = contracted;
        fx[N] = f_contracted;
      } else {
        // Shrink towards the best point.
        for (size_t k = 1; k <= N; ++k) {
          x[k] = combine(x[0], x[k], 0.5);
          fx[k] = f(x[k]);
        }
      }
    }
  }
  const size_t best = std::min_element(fx.begin(), fx.end()) - fx.begin();
  return {x[best], fx[best], iteration};
}

}  // namespace smileexplorer

#endif  // SMILEEXPLORER_CALIBRATION_NELDER_MEAD_H_
