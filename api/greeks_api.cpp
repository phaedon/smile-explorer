#include "api/greeks_api.h"

#include <cmath>
#include <limits>
#include <random>
#include <string_view>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "derivatives/bsm.h"

namespace smileexplorer::api {
namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr int kMaxSurfacePoints = 200;
constexpr int kMaxRebalances = 2000;
constexpr int kMaxPaths = 20'000;
constexpr long kMaxSimulationSteps = 2'000'000;

// Black-Scholes, with no dividends, for the option's side.
struct ClosedForm {
  double strike;
  double vol;
  double rate;
  bool is_call;

  double price(double S, double t) const {
    return is_call ? call(S, strike, vol, t, rate, 0)
                   : put(S, strike, vol, t, rate, 0);
  }
  double delta(double S, double t) const {
    return is_call ? call_delta(S, strike, vol, t, rate, 0)
                   : put_delta(S, strike, vol, t, rate, 0);
  }
  double gamma(double S, double t) const {
    return smileexplorer::gamma(S, strike, vol, t, rate, 0);
  }
  double theta(double S, double t) const {
    return is_call ? call_theta(S, strike, vol, t, rate, 0)
                   : put_theta(S, strike, vol, t, rate, 0);
  }
  // bsm.h's vega is per vol point; this is per unit of vol.
  double vega(double S, double t) const {
    return 100 * smileexplorer::vega(S, strike, vol, t, rate, 0);
  }
  double rho(double S, double t) const {
    const auto v = calculateBSMIntermediates(S, strike, vol, t, rate, 0);
    return is_call ? strike * t * v.e_neg_rt * normsdist(v.d2)
                   : -strike * t * v.e_neg_rt * normsdist(-v.d2);
  }
  double vanna(double S, double t) const {
    return smileexplorer::vanna(S, strike, vol, t, rate, 0);
  }
  double volga(double S, double t) const {
    return smileexplorer::volga(S, strike, vol, t, rate, 0);
  }
  double charm(double S, double t) const {
    return is_call ? call_charm(S, strike, vol, t, rate, 0)
                   : put_charm(S, strike, vol, t, rate, 0);
  }
  double colour(double S, double t) const {
    return smileexplorer::colour(S, strike, vol, t, rate, 0);
  }

  double greek(Greek g, double S, double t) const {
    switch (g) {
      case Greek::kPrice:
        return price(S, t);
      case Greek::kDelta:
        return delta(S, t);
      case Greek::kGamma:
        return gamma(S, t);
      case Greek::kTheta:
        return theta(S, t);
      case Greek::kVega:
        return vega(S, t);
      case Greek::kVanna:
        return vanna(S, t);
      case Greek::kVolga:
        return volga(S, t);
      case Greek::kCharm:
        return charm(S, t);
      case Greek::kColour:
        return colour(S, t);
    }
    return kNaN;
  }
};

bool isKnownGreek(Greek g) {
  return static_cast<int>(g) >= static_cast<int>(Greek::kPrice) &&
         static_cast<int>(g) <= static_cast<int>(Greek::kColour);
}

// The central difference of the tree price, with one input bumped.
template <typename BumpFn>
absl::StatusOr<double> bumpedDerivative(const VanillaPricingRequest& request,
                                        double bump,
                                        BumpFn apply) {
  VanillaPricingRequest up = request;
  VanillaPricingRequest down = request;
  apply(up, bump);
  apply(down, -bump);
  const auto up_result = priceVanilla(up);
  if (!up_result.ok()) return up_result.status();
  const auto down_result = priceVanilla(down);
  if (!down_result.ok()) return down_result.status();
  return (up_result->price - down_result->price) / (2 * bump);
}

}  // namespace

absl::StatusOr<GreeksResult> computeGreeks(const VanillaPricingRequest& request,
                                           double vol_bump,
                                           double rate_bump) {
  if (request.tree.model == TreeModel::kLocalVolatility ||
      request.tree.vol.model != VolModel::kFlat) {
    return absl::InvalidArgumentError(
        "Greeks need a Cox-Ross-Rubinstein or Jarrow-Rudd tree with flat vol");
  }
  if (!(vol_bump > 0) || !(vol_bump < request.tree.vol.vol) ||
      !(rate_bump > 0)) {
    return absl::InvalidArgumentError(
        "the vol bump must be positive and smaller than the vol, and the "
        "rate bump positive");
  }
  const auto priced = priceVanilla(request);
  if (!priced.ok()) return priced.status();
  const VanillaPricingResult& r = *priced;
  if (r.expiry_index < 2) {
    return absl::InvalidArgumentError(
        "Greeks need a tree with at least two steps to expiry");
  }

  const auto S = [&](int t, int i) { return r.asset_values[nodeIndex(t, i)]; };
  const auto V = [&](int t, int i) { return r.option_values[nodeIndex(t, i)]; };

  GreeksResult result;
  result.price = r.price;
  result.vol_bump = vol_bump;
  result.rate_bump = rate_bump;
  result.tree.delta = (V(1, 1) - V(1, 0)) / (S(1, 1) - S(1, 0));
  const double delta_up = (V(2, 2) - V(2, 1)) / (S(2, 2) - S(2, 1));
  const double delta_down = (V(2, 1) - V(2, 0)) / (S(2, 1) - S(2, 0));
  result.tree.gamma = (delta_up - delta_down) / ((S(2, 2) - S(2, 0)) / 2);
  result.tree.theta = (V(2, 1) - V(0, 0)) / r.times[2];

  const auto vega = bumpedDerivative(
      request, vol_bump, [](VanillaPricingRequest& req, double bump) {
        req.tree.vol.vol += bump;
      });
  if (!vega.ok()) return vega.status();
  result.tree.vega = *vega;
  const auto rho = bumpedDerivative(
      request, rate_bump, [](VanillaPricingRequest& req, double bump) {
        req.tree.rate += bump;
      });
  if (!rho.ok()) return rho.status();
  result.tree.rho = *rho;

  if (request.option.is_american) {
    result.closed_form = {kNaN, kNaN, kNaN, kNaN, kNaN};
    result.vanna = result.volga = result.charm = result.colour = kNaN;
    return result;
  }
  const ClosedForm bs{.strike = request.option.strike,
                      .vol = request.tree.vol.vol,
                      .rate = request.tree.rate,
                      .is_call = request.option.is_call};
  const double spot = request.tree.spot;
  const double t = r.times[r.expiry_index];
  result.closed_form = {.delta = bs.delta(spot, t),
                        .gamma = bs.gamma(spot, t),
                        .theta = bs.theta(spot, t),
                        .vega = bs.vega(spot, t),
                        .rho = bs.rho(spot, t)};
  result.vanna = bs.vanna(spot, t);
  result.volga = bs.volga(spot, t);
  result.charm = bs.charm(spot, t);
  result.colour = bs.colour(spot, t);
  return result;
}

absl::StatusOr<std::vector<double>> greekSurface(
    const GreekSurfaceRequest& request) {
  if (!isKnownGreek(request.greek)) {
    return absl::InvalidArgumentError("unknown greek");
  }
  if (!(request.strike > 0) || !(request.vol > 0) ||
      !std::isfinite(request.rate)) {
    return absl::InvalidArgumentError(
        "strike and vol must be positive, and the rate finite");
  }
  if (request.spots.empty() || request.expiries.empty() ||
      std::ssize(request.spots) > kMaxSurfacePoints ||
      std::ssize(request.expiries) > kMaxSurfacePoints) {
    return absl::InvalidArgumentError(
        absl::StrCat("spots and expiries must each have between 1 and ",
                     kMaxSurfacePoints,
                     " points"));
  }
  for (double s : request.spots) {
    if (!(s > 0)) {
      return absl::InvalidArgumentError("spots must be positive");
    }
  }
  for (double t : request.expiries) {
    if (!(t > 0)) {
      return absl::InvalidArgumentError("expiries must be positive");
    }
  }
  const ClosedForm bs{.strike = request.strike,
                      .vol = request.vol,
                      .rate = request.rate,
                      .is_call = request.is_call};
  std::vector<double> values;
  values.reserve(request.spots.size() * request.expiries.size());
  for (double t : request.expiries) {
    for (double s : request.spots) {
      values.push_back(bs.greek(request.greek, s, t));
    }
  }
  return values;
}

absl::StatusOr<DeltaHedgeResult> simulateDeltaHedge(
    const DeltaHedgeRequest& request) {
  const DeltaHedgeRequest& q = request;
  if (!(q.spot > 0) || !(q.strike > 0) || !(q.expiry_years > 0)) {
    return absl::InvalidArgumentError(
        "spot, strike and expiry_years must be positive");
  }
  if (!(q.implied_vol > 0) || !(q.realised_vol > 0)) {
    return absl::InvalidArgumentError(
        "implied_vol and realised_vol must be positive");
  }
  if (!std::isfinite(q.rate)) {
    return absl::InvalidArgumentError("rate must be finite");
  }
  if (q.rebalances < 1 || q.rebalances > kMaxRebalances || q.paths < 1 ||
      q.paths > kMaxPaths ||
      static_cast<long>(q.rebalances) * q.paths > kMaxSimulationSteps) {
    return absl::InvalidArgumentError(
        absl::StrCat("rebalances must be between 1 and ",
                     kMaxRebalances,
                     ", paths between 1 and ",
                     kMaxPaths,
                     ", and their product at most ",
                     kMaxSimulationSteps));
  }

  const ClosedForm bs{.strike = q.strike,
                      .vol = q.implied_vol,
                      .rate = q.rate,
                      .is_call = q.is_call};
  const int n = q.rebalances;
  const double dt = q.expiry_years / n;
  const double drift = (q.rate - q.realised_vol * q.realised_vol / 2) * dt;
  const double diffusion = q.realised_vol * std::sqrt(dt);
  const double growth = std::exp(q.rate * dt);

  DeltaHedgeResult result;
  result.premium = bs.price(q.spot, q.expiry_years);
  result.pnl.reserve(q.paths);

  std::mt19937_64 rng(q.seed);
  std::normal_distribution<double> normal;
  for (int path = 0; path < q.paths; ++path) {
    const bool record = path == 0;
    double spot = q.spot;
    double delta = bs.delta(spot, q.expiry_years);
    // Sell the option and buy the hedge.
    double cash = result.premium - delta * spot;
    double theta_pnl = 0;
    double gamma_pnl = 0;
    if (record) {
      result.times.push_back(0);
      result.spots.push_back(spot);
      result.deltas.push_back(delta);
      result.cumulative_pnl.push_back(0);
      result.cumulative_theta.push_back(0);
      result.cumulative_gamma.push_back(0);
    }
    for (int k = 1; k <= n; ++k) {
      const double expiry_before = q.expiry_years - (k - 1) * dt;
      const double next_spot = spot * std::exp(drift + diffusion * normal(rng));
      if (record) {
        // The short option earns theta and pays gamma.
        theta_pnl -= bs.theta(spot, expiry_before) * dt;
        gamma_pnl -=
            bs.gamma(spot, expiry_before) * std::pow(next_spot - spot, 2) / 2;
      }
      spot = next_spot;
      cash *= growth;
      double option_value;
      if (k < n) {
        const double expiry_after = q.expiry_years - k * dt;
        const double new_delta = bs.delta(spot, expiry_after);
        cash -= (new_delta - delta) * spot;
        delta = new_delta;
        option_value = bs.price(spot, expiry_after);
      } else {
        option_value = q.is_call ? std::max(spot - q.strike, 0.0)
                                 : std::max(q.strike - spot, 0.0);
      }
      const double pnl = cash + delta * spot - option_value;
      if (record) {
        result.times.push_back(k * dt);
        result.spots.push_back(spot);
        result.deltas.push_back(k < n ? delta : kNaN);
        result.cumulative_pnl.push_back(pnl);
        result.cumulative_theta.push_back(theta_pnl);
        result.cumulative_gamma.push_back(gamma_pnl);
      }
      if (k == n) result.pnl.push_back(pnl);
    }
  }

  double sum = 0;
  for (double pnl : result.pnl) sum += pnl;
  result.mean = sum / q.paths;
  double squares = 0;
  for (double pnl : result.pnl) squares += std::pow(pnl - result.mean, 2);
  result.stddev = q.paths > 1 ? std::sqrt(squares / (q.paths - 1)) : 0.0;
  return result;
}

}  // namespace smileexplorer::api
