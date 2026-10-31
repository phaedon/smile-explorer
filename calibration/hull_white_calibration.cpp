#include "calibration/hull_white_calibration.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "derivatives/bachelier.h"
#include "derivatives/hull_white_analytic.h"
#include "rates/swap_rates.h"

namespace smileexplorer {
namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr int kMaxQuotes = 40;

// Bounds on the parameters searched; outside them the objective is huge.
constexpr double kMinMeanReversion = 1e-4;
constexpr double kMaxMeanReversion = 3.0;
constexpr double kMinSigma = 1e-5;
constexpr double kMaxSigma = 0.2;

std::vector<double> paymentTimes(const RateOptionQuote& quote,
                                 int payments_per_year) {
  const int n = static_cast<int>(std::lround(quote.tenor * payments_per_year));
  std::vector<double> times;
  for (int k = 1; k <= n; ++k) {
    times.push_back(quote.expiry + static_cast<double>(k) / payments_per_year);
  }
  return times;
}

// Nelder-Mead minimisation of f over two variables, from `start` with initial
// steps `step`.
template <typename F>
std::pair<std::array<double, 2>, int> nelderMead(F f,
                                                 std::array<double, 2> start,
                                                 std::array<double, 2> step,
                                                 int max_iterations,
                                                 double tolerance) {
  using Point = std::array<double, 2>;
  std::array<Point, 3> x = {start,
                            Point{start[0] + step[0], start[1]},
                            Point{start[0], start[1] + step[1]}};
  std::array<double, 3> fx = {f(x[0]), f(x[1]), f(x[2])};
  const auto combine = [](const Point& a, const Point& b, double t) {
    return Point{a[0] + t * (b[0] - a[0]), a[1] + t * (b[1] - a[1])};
  };
  int iteration = 0;
  for (; iteration < max_iterations; ++iteration) {
    // Order: best first.
    std::array<int, 3> order = {0, 1, 2};
    std::sort(order.begin(), order.end(), [&](int i, int j) {
      return fx[i] < fx[j];
    });
    x = {x[order[0]], x[order[1]], x[order[2]]};
    fx = {fx[order[0]], fx[order[1]], fx[order[2]]};
    if (std::abs(fx[2] - fx[0]) < tolerance &&
        std::abs(x[2][0] - x[0][0]) + std::abs(x[2][1] - x[0][1]) < 1e-9) {
      break;
    }
    const Point centroid = combine(x[0], x[1], 0.5);
    const Point reflected = combine(centroid, x[2], -1.0);
    const double f_reflected = f(reflected);
    if (f_reflected < fx[0]) {
      const Point expanded = combine(centroid, x[2], -2.0);
      const double f_expanded = f(expanded);
      if (f_expanded < f_reflected) {
        x[2] = expanded;
        fx[2] = f_expanded;
      } else {
        x[2] = reflected;
        fx[2] = f_reflected;
      }
    } else if (f_reflected < fx[1]) {
      x[2] = reflected;
      fx[2] = f_reflected;
    } else {
      const bool outside = f_reflected < fx[2];
      const Point contracted =
          combine(centroid, outside ? reflected : x[2], 0.5);
      const double f_contracted = f(contracted);
      if (f_contracted < std::min(f_reflected, fx[2])) {
        x[2] = contracted;
        fx[2] = f_contracted;
      } else {
        // Shrink towards the best point.
        for (int k = 1; k < 3; ++k) {
          x[k] = combine(x[0], x[k], 0.5);
          fx[k] = f(x[k]);
        }
      }
    }
  }
  const int best = std::min_element(fx.begin(), fx.end()) - fx.begin();
  return {x[best], iteration};
}

}  // namespace

ModelQuote hullWhiteQuote(const RatesCurve& curve,
                          const HullWhiteParams& params,
                          const RateOptionQuote& quote,
                          int fixed_payments_per_year) {
  const double a = params.mean_reversion;
  const double sigma = params.sigma;
  double price;
  double forward;
  double scale;
  if (quote.type == RateOptionType::kCaplet) {
    const double end = quote.expiry + quote.tenor;
    forward = simpleForwardRate(curve, quote.expiry, end);
    price =
        hullWhiteCaplet(curve, a, sigma, quote.expiry, end, forward, 1, true);
    scale = quote.tenor * curve.df(end);
  } else {
    const std::vector<double> times =
        paymentTimes(quote, fixed_payments_per_year);
    forward = parSwapRate(curve, quote.expiry, times);
    price = hullWhiteSwaption(
        curve, a, sigma, quote.expiry, times, forward, 1, true);
    scale = annuity(curve, quote.expiry, times);
  }
  const auto vol =
      impliedNormalVol(price / scale, forward, forward, quote.expiry, true);
  return {.price = price, .normal_vol = vol.value_or(kNaN)};
}

absl::StatusOr<CalibrationResult> calibrateHullWhite(
    const RatesCurve& curve,
    const std::vector<RateOptionQuote>& quotes,
    int fixed_payments_per_year) {
  if (std::ssize(quotes) < 2 || std::ssize(quotes) > kMaxQuotes) {
    return absl::InvalidArgumentError(
        absl::StrCat("calibrating two parameters needs between 2 and ",
                     kMaxQuotes,
                     " quotes"));
  }
  if (fixed_payments_per_year < 1 || fixed_payments_per_year > 12) {
    return absl::InvalidArgumentError(
        "fixed_payments_per_year must be between 1 and 12");
  }
  for (const RateOptionQuote& q : quotes) {
    if (!(q.expiry > 0) || !(q.tenor > 0) || !(q.normal_vol > 0)) {
      return absl::InvalidArgumentError(
          "each quote needs a positive expiry, tenor and normal vol");
    }
    if (q.type == RateOptionType::kSwaption) {
      const double periods = q.tenor * fixed_payments_per_year;
      if (std::abs(periods - std::round(periods)) > 1e-9) {
        return absl::InvalidArgumentError(
            "a swaption's tenor must be a whole number of fixed periods");
      }
    } else if (q.type != RateOptionType::kCaplet) {
      return absl::InvalidArgumentError("unknown option type");
    }
  }

  const auto model_vols = [&](const HullWhiteParams& params) {
    std::vector<double> vols;
    for (const RateOptionQuote& q : quotes) {
      vols.push_back(
          hullWhiteQuote(curve, params, q, fixed_payments_per_year).normal_vol);
    }
    return vols;
  };
  // Least squares in vol, over log-parameters (which keeps them positive).
  const auto objective = [&](const std::array<double, 2>& x) {
    const HullWhiteParams params{.mean_reversion = std::exp(x[0]),
                                 .sigma = std::exp(x[1])};
    if (params.mean_reversion < kMinMeanReversion ||
        params.mean_reversion > kMaxMeanReversion || params.sigma < kMinSigma ||
        params.sigma > kMaxSigma) {
      return 1e10;
    }
    double sum = 0.0;
    const std::vector<double> vols = model_vols(params);
    for (size_t i = 0; i < quotes.size(); ++i) {
      if (!std::isfinite(vols[i])) return 1e10;
      sum += std::pow(vols[i] - quotes[i].normal_vol, 2);
    }
    return sum;
  };

  // Start from a few mean reversions, with sigma near the quoted vols (which
  // it equals for short expiries), and keep the best fit.
  double mean_vol = 0.0;
  for (const RateOptionQuote& q : quotes) mean_vol += q.normal_vol;
  mean_vol /= quotes.size();
  std::array<double, 2> best_x{};
  double best_f = std::numeric_limits<double>::infinity();
  int iterations = 0;
  for (double a0 : {0.02, 0.1, 0.5}) {
    const auto [x, n] = nelderMead(objective,
                                   {std::log(a0), std::log(mean_vol)},
                                   {0.5, 0.2},
                                   /*max_iterations=*/400,
                                   /*tolerance=*/1e-18);
    iterations += n;
    const double fx = objective(x);
    if (fx < best_f) {
      best_f = fx;
      best_x = x;
    }
  }
  if (!(best_f < 1e10)) {
    return absl::InvalidArgumentError(
        "no Hull-White parameters reproduce these quotes");
  }

  CalibrationResult result;
  result.params = {.mean_reversion = std::exp(best_x[0]),
                   .sigma = std::exp(best_x[1])};
  result.model_vols = model_vols(result.params);
  for (const RateOptionQuote& q : quotes) {
    result.market_vols.push_back(q.normal_vol);
  }
  result.rms_error = std::sqrt(best_f / quotes.size());
  result.iterations = iterations;
  // Converged unless pinned at a bound.
  result.converged = result.params.mean_reversion > kMinMeanReversion * 1.01 &&
                     result.params.mean_reversion < kMaxMeanReversion * 0.99 &&
                     result.params.sigma > kMinSigma * 1.01 &&
                     result.params.sigma < kMaxSigma * 0.99;
  return result;
}

}  // namespace smileexplorer
