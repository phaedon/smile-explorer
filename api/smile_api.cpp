#include "api/smile_api.h"

#include <cmath>
#include <limits>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "api/pricing_api.h"
#include "derivatives/derivative.h"
#include "rates/zero_curve.h"
#include "trees/binomial_tree.h"
#include "trees/propagators.h"
#include "trees/stochastic_tree_model.h"
#include "volatility/dupire_local_vol.h"
#include "volatility/ssvi.h"
#include "volatility/volatility.h"

namespace smileexplorer::api {
namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr int kMaxGridPoints = 200;
constexpr int kMaxSteps = 1000;

bool positiveIncreasing(const std::vector<double>& xs) {
  for (size_t i = 0; i < xs.size(); ++i) {
    if (!(xs[i] > (i == 0 ? 0.0 : xs[i - 1]))) return false;
  }
  return true;
}

absl::Status validate(const SmileGridRequest& r) {
  if (!(r.spot > 0) || !std::isfinite(r.rate)) {
    return absl::InvalidArgumentError(
        "spot must be positive and the rate finite");
  }
  const SsviSpec& s = r.ssvi;
  if (!(std::abs(s.rho) < 1) || !(s.eta > 0) || !(s.gamma > 0 && s.gamma < 1)) {
    return absl::InvalidArgumentError(
        "SSVI needs rho in (-1, 1), eta > 0 and gamma in (0, 1)");
  }
  if (s.expiries.empty() || s.expiries.size() != s.thetas.size() ||
      !positiveIncreasing(s.expiries) || !positiveIncreasing(s.thetas)) {
    return absl::InvalidArgumentError(
        "SSVI needs matching expiries and at-the-money total variances, "
        "both positive and increasing");
  }
  if (r.strikes.empty() || r.times.empty() ||
      std::ssize(r.strikes) > kMaxGridPoints ||
      std::ssize(r.times) > kMaxGridPoints) {
    return absl::InvalidArgumentError(
        absl::StrCat("strikes and times must each have between 1 and ",
                     kMaxGridPoints,
                     " points"));
  }
  for (double k : r.strikes) {
    if (!(k > 0)) return absl::InvalidArgumentError("strikes must be positive");
  }
  for (double t : r.times) {
    if (!(t > 0)) return absl::InvalidArgumentError("times must be positive");
  }
  return absl::OkStatus();
}

SsviSurface surfaceOf(const SsviSpec& s) {
  return SsviSurface(s.rho, s.eta, s.gamma, s.expiries, s.thetas);
}

}  // namespace

absl::StatusOr<SmileFit> calibrateSmile(const SmileQuotes& quotes) {
  return calibrateSsvi(quotes);
}

absl::StatusOr<SmileSurfaces> smileSurfaces(const SmileGridRequest& r) {
  if (absl::Status status = validate(r); !status.ok()) return status;
  const SsviSurface surface = surfaceOf(r.ssvi);
  const DupireLocalVol local(surface, r.spot, r.rate);
  SmileSurfaces result;
  for (double t : r.times) {
    const double forward = r.spot * std::exp(r.rate * t);
    for (double k : r.strikes) {
      result.implied_vols.push_back(
          surface.impliedVol(std::log(k / forward), t));
      result.local_vols.push_back(local(k, t));
      result.clamped += local.isClamped(k, t);
    }
  }
  return result;
}

absl::StatusOr<RepriceResult> repriceSmile(const RepriceRequest& request) {
  const SmileGridRequest& r = request.grid;
  if (absl::Status status = validate(r); !status.ok()) return status;
  if (request.steps_per_year < 2 || request.steps_per_year > kMaxSteps) {
    return absl::InvalidArgumentError(
        absl::StrCat("steps_per_year must be between 2 and ", kMaxSteps));
  }
  const double dt = 1.0 / request.steps_per_year;
  const double horizon = *std::max_element(r.times.begin(), r.times.end());
  if (horizon / dt > kMaxSteps + 1e-9) {
    return absl::InvalidArgumentError(
        absl::StrCat("the tree would need more than ", kMaxSteps, " steps"));
  }

  const SsviSurface surface = surfaceOf(r.ssvi);
  const ZeroSpotCurve curve({1.0, 10.0}, {r.rate, r.rate});
  StochasticTreeModel<LocalVolatilityPropagator> asset(
      BinomialTree(horizon * (1 - 1e-12), dt),
      LocalVolatilityPropagator(curve, r.spot));
  asset.forwardPropagate(Volatility(DupireLocalVol(surface, r.spot, r.rate)));
  const BinomialTree& tree = asset.binomialTree();
  SingleAssetDerivative derivative(&tree, &curve);
  const BinomialTree& arrow_debreu = derivative.arrowDebreuTree();
  const Timegrid& grid = tree.getTimegrid();

  RepriceResult result;
  double squares = 0.0;
  int count = 0;
  for (double t : r.times) {
    const auto index = grid.getTimeIndexForExpiry(t);
    if (!index.has_value() || std::abs(grid.time(*index) - t) > 1e-9) {
      return absl::InvalidArgumentError(absl::StrCat(
          "the time ", t, " doesn't fall on one of the tree's steps"));
    }
    const double forward = r.spot * std::exp(r.rate * t);
    for (double strike : r.strikes) {
      // Out of the money: a put below the forward, a call above.
      const bool is_call = strike >= forward;
      double price = 0.0;
      for (int i = 0; i <= *index; ++i) {
        const double s = tree.nodeValue(*index, i);
        price +=
            arrow_debreu.nodeValue(*index, i) *
            (is_call ? std::max(s - strike, 0.0) : std::max(strike - s, 0.0));
      }
      const auto vol = impliedVolatility({.price = price,
                                          .spot = r.spot,
                                          .strike = strike,
                                          .expiry_years = t,
                                          .rate = r.rate,
                                          .is_call = is_call});
      const double tree_vol = vol.ok() ? *vol : kNaN;
      const double surface_vol =
          surface.impliedVol(std::log(strike / forward), t);
      result.tree_vols.push_back(tree_vol);
      result.surface_vols.push_back(surface_vol);
      result.residuals.push_back(tree_vol - surface_vol);
      if (std::isfinite(tree_vol)) {
        squares += std::pow(tree_vol - surface_vol, 2);
        ++count;
      }
    }
  }
  result.rms_error = count > 0 ? std::sqrt(squares / count) : kNaN;
  return result;
}

absl::StatusOr<std::vector<double>> impliedDensity(const SmileGridRequest& r) {
  if (absl::Status status = validate(r); !status.ok()) return status;
  const SsviSurface surface = surfaceOf(r.ssvi);
  std::vector<double> density;
  for (double t : r.times) {
    const double forward = r.spot * std::exp(r.rate * t);
    for (double k : r.strikes)
      density.push_back(surface.density(k, forward, t));
  }
  return density;
}

}  // namespace smileexplorer::api
