#include "calibration/hull_white_calibration.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "calibration/nelder_mead.h"
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
    const auto [x, fx, n] = nelderMead<2>(objective,
                                          {std::log(a0), std::log(mean_vol)},
                                          {0.5, 0.2},
                                          /*max_iterations=*/400,
                                          /*tolerance=*/1e-18);
    iterations += n;
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
