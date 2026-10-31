#include "calibration/smile_calibration.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "calibration/nelder_mead.h"

namespace smileexplorer {
namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr int kMaxExpiries = 20;
constexpr int kMaxStrikes = 50;

bool strictlyIncreasingPositive(const std::vector<double>& xs) {
  for (size_t i = 0; i < xs.size(); ++i) {
    if (!(xs[i] > (i == 0 ? 0.0 : xs[i - 1]))) return false;
  }
  return true;
}

// The unconstrained search variables, mapped to SSVI's parameters:
// rho = tanh(x0) in (-1, 1), eta = exp(x1) > 0, gamma = logistic(x2) in
// (0, 1).
struct Params {
  double rho;
  double eta;
  double gamma;
};
Params fromSearch(const std::array<double, 3>& x) {
  return {std::tanh(x[0]), std::exp(x[1]), 1 / (1 + std::exp(-x[2]))};
}

}  // namespace

absl::StatusOr<SmileFit> calibrateSsvi(const SmileQuotes& q) {
  if (!(q.spot > 0) || !std::isfinite(q.rate) ||
      !std::isfinite(q.dividend_yield)) {
    return absl::InvalidArgumentError(
        "spot must be positive, and the rate and dividend yield finite");
  }
  if (q.expiries.empty() || std::ssize(q.expiries) > kMaxExpiries ||
      q.strikes.size() < 2 || std::ssize(q.strikes) > kMaxStrikes ||
      !strictlyIncreasingPositive(q.expiries) ||
      !strictlyIncreasingPositive(q.strikes)) {
    return absl::InvalidArgumentError(
        absl::StrCat("expiries (1 to ",
                     kMaxExpiries,
                     ") and strikes (2 to ",
                     kMaxStrikes,
                     ") must be positive and strictly increasing"));
  }
  if (q.vols.size() != q.expiries.size() * q.strikes.size()) {
    return absl::InvalidArgumentError(
        "vols must have one entry per expiry and strike");
  }
  int num_quotes = 0;
  for (double v : q.vols) {
    if (std::isnan(v)) continue;
    if (!(v > 0) || !(v < 5)) {
      return absl::InvalidArgumentError(
          "quoted vols must be positive (and below 500%)");
    }
    ++num_quotes;
  }
  if (num_quotes < 4) {
    return absl::InvalidArgumentError(
        "fitting three parameters needs at least four quotes");
  }

  const size_t num_strikes = q.strikes.size();
  const auto forward = [&](double t) {
    return q.spot * std::exp((q.rate - q.dividend_yield) * t);
  };

  // At-the-money total variance per expiry: the quoted vols interpolated
  // linearly in log-moneyness at the forward.
  SmileFit fit;
  for (size_t i = 0; i < q.expiries.size(); ++i) {
    const double t = q.expiries[i];
    const double f = forward(t);
    double below_k = -INFINITY, below_v = kNaN, above_k = INFINITY,
           above_v = kNaN;
    for (size_t j = 0; j < num_strikes; ++j) {
      const double v = q.vols[i * num_strikes + j];
      if (std::isnan(v)) continue;
      const double k = std::log(q.strikes[j] / f);
      if (k <= 0 && k > below_k) {
        below_k = k;
        below_v = v;
      }
      if (k >= 0 && k < above_k) {
        above_k = k;
        above_v = v;
      }
    }
    if (std::isnan(below_v) || std::isnan(above_v)) {
      return absl::InvalidArgumentError(absl::StrCat(
          "the quotes at ",
          t,
          " years don't bracket the forward, so its at-the-money vol is "
          "unknown"));
    }
    const double atm_vol = above_k == below_k
                               ? below_v
                               : below_v + (above_v - below_v) * (0 - below_k) /
                                               (above_k - below_k);
    double theta = atm_vol * atm_vol * t;
    if (!fit.thetas.empty() && theta < fit.thetas.back()) {
      // Total variance can't fall with expiry (calendar arbitrage).
      theta = fit.thetas.back();
      fit.thetas_adjusted = true;
    }
    fit.thetas.push_back(theta);
  }

  const auto model_vols = [&](const Params& p) {
    const SsviSurface surface(p.rho, p.eta, p.gamma, q.expiries, fit.thetas);
    std::vector<double> vols(q.vols.size(), kNaN);
    for (size_t i = 0; i < q.expiries.size(); ++i) {
      const double t = q.expiries[i];
      const double f = forward(t);
      for (size_t j = 0; j < num_strikes; ++j) {
        if (std::isnan(q.vols[i * num_strikes + j])) continue;
        vols[i * num_strikes + j] =
            surface.impliedVol(std::log(q.strikes[j] / f), t);
      }
    }
    return vols;
  };

  // Least squares in vol, plus a penalty for breaking the no-butterfly
  // conditions, steep enough that the fit respects them.
  const auto objective = [&](const std::array<double, 3>& x) {
    const Params p = fromSearch(x);
    const std::vector<double> vols = model_vols(p);
    double sum = 0.0;
    for (size_t n = 0; n < vols.size(); ++n) {
      if (!std::isnan(q.vols[n])) sum += std::pow(vols[n] - q.vols[n], 2);
    }
    const SsviSurface surface(p.rho, p.eta, p.gamma, q.expiries, fit.thetas);
    for (double theta : fit.thetas) {
      const double phi = surface.phi(theta);
      const double scale = 1 + std::abs(p.rho);
      sum += 1e2 * std::pow(std::max(0.0, theta * phi * scale - 4 + 1e-3), 2);
      sum += 1e2 *
             std::pow(std::max(0.0, theta * phi * phi * scale - 4 + 1e-3), 2);
    }
    return std::isfinite(sum) ? sum : 1e10;
  };

  std::array<double, 3> best_x{};
  double best_f = INFINITY;
  for (double rho0 : {-0.5, 0.0, 0.5}) {
    const auto result = nelderMead<3>(objective,
                                      {std::atanh(rho0), std::log(1.0), 0.0},
                                      {0.5, 0.5, 0.5},
                                      /*max_iterations=*/1500,
                                      /*tolerance=*/1e-20);
    fit.iterations += result.iterations;
    if (result.value < best_f) {
      best_f = result.value;
      best_x = result.x;
    }
  }

  const Params p = fromSearch(best_x);
  fit.rho = p.rho;
  fit.eta = p.eta;
  fit.gamma = p.gamma;
  fit.fitted_vols = model_vols(p);
  double squares = 0.0;
  fit.residuals.assign(q.vols.size(), kNaN);
  for (size_t n = 0; n < q.vols.size(); ++n) {
    if (std::isnan(q.vols[n])) continue;
    fit.residuals[n] = fit.fitted_vols[n] - q.vols[n];
    squares += fit.residuals[n] * fit.residuals[n];
  }
  fit.rms_error = std::sqrt(squares / num_quotes);
  const SsviSurface surface = fit.surface(q.expiries);
  fit.butterfly_ok = surface.butterflyOk();
  fit.calendar_ok = surface.calendarOk();
  return fit;
}

}  // namespace smileexplorer
