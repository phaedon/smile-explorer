#include "api/rates_options_api.h"

#include <cmath>
#include <limits>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "api/hull_white_tree.h"
#include "derivatives/bachelier.h"
#include "derivatives/hull_white_analytic.h"
#include "derivatives/interest_rate_derivative.h"
#include "derivatives/vanilla_option.h"
#include "instruments/swaps/interest_rate_swap.h"
#include "rates/swap_rates.h"

namespace smileexplorer::api {
namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr int kMaxPeriods = 200;

// The number of whole periods of 1 / per_year in `years`, or an error.
absl::StatusOr<int> wholePeriods(double years,
                                 int per_year,
                                 std::string_view what) {
  if (per_year < 1 || per_year > 12) {
    return absl::InvalidArgumentError(
        absl::StrCat(what, ": payments per year must be between 1 and 12"));
  }
  const double periods = years * per_year;
  if (!(periods > 0.5) || !(periods < kMaxPeriods + 0.5) ||
      std::abs(periods - std::round(periods)) > 1e-9) {
    return absl::InvalidArgumentError(
        absl::StrCat(what,
                     " must be a whole number of payment periods (at most ",
                     kMaxPeriods,
                     ")"));
  }
  return static_cast<int>(std::lround(periods));
}

double normalVol(
    double price, double forward, double strike, double expiry, bool is_call) {
  return impliedNormalVol(price, forward, strike, expiry, is_call)
      .value_or(kNaN);
}

}  // namespace

absl::StatusOr<SwaptionResult> priceSwaption(const SwaptionRequest& request) {
  const SwaptionSpec& s = request.swaption;
  if (!(s.expiry_years > 0)) {
    return absl::InvalidArgumentError("expiry_years must be positive");
  }
  if (!(s.notional > 0)) {
    return absl::InvalidArgumentError("notional must be positive");
  }
  if (!s.at_the_money && !std::isfinite(s.fixed_rate)) {
    return absl::InvalidArgumentError("fixed_rate must be finite");
  }
  const absl::StatusOr<int> periods =
      wholePeriods(s.tenor_years, s.fixed_payments_per_year, "tenor_years");
  if (!periods.ok()) return periods.status();

  HullWhiteTreeSpec tree_spec = request.tree;
  tree_spec.horizon_years = s.expiry_years + s.tenor_years;
  tree_spec.bond_face = s.notional;
  if (absl::Status status = evaluateCurve(tree_spec.curve, {}).status();
      !status.ok()) {
    return status;
  }

  SwaptionResult result;
  for (int k = 1; k <= *periods; ++k) {
    result.payment_times.push_back(
        s.expiry_years + static_cast<double>(k) / s.fixed_payments_per_year);
  }
  const ZeroSpotCurve curve(tree_spec.curve.maturities, tree_spec.curve.rates);
  result.annuity = annuity(curve, s.expiry_years, result.payment_times);
  result.par_rate = parSwapRate(curve, s.expiry_years, result.payment_times);
  result.fixed_rate = s.at_the_money ? result.par_rate : s.fixed_rate;

  // The fixed leg plus notional: a bond, worth par at expiry exactly when the
  // swap is.
  std::vector<Cashflow> cashflows;
  double previous = s.expiry_years;
  for (double t : result.payment_times) {
    cashflows.push_back(
        {.time_years = t,
         .amount = result.fixed_rate * (t - previous) * s.notional});
    previous = t;
  }
  cashflows.back().amount += s.notional;

  absl::StatusOr<FittedTree> fitted = fitTree(tree_spec, cashflows);
  if (!fitted.ok()) return fitted.status();
  const double expiry_on_grid = [&] {
    const auto& times = fitted->result.times;
    double best = times[0];
    for (double t : times) {
      if (std::abs(t - s.expiry_years) < std::abs(best - s.expiry_years)) {
        best = t;
      }
    }
    return best;
  }();
  if (std::abs(expiry_on_grid - s.expiry_years) > 1e-9) {
    return absl::InvalidArgumentError(
        "the expiry doesn't fall on one of the tree's steps");
  }

  absl::StatusOr<BondOptionResult> option =
      optionOnBond(*fitted,
                   {.strike = s.notional,
                    .expiry_years = s.expiry_years,
                    .is_call = !s.is_payer},
                   tree_spec.dt_years);
  if (!option.ok()) return option.status();
  std::vector<double> amounts;
  for (const Cashflow& c : cashflows) amounts.push_back(c.amount);
  option->analytic_price = hullWhiteCouponBondOption(curve,
                                                     tree_spec.mean_reversion,
                                                     tree_spec.sigma,
                                                     s.expiry_years,
                                                     result.payment_times,
                                                     amounts,
                                                     s.notional,
                                                     !s.is_payer);
  option->tree = std::move(fitted->result);

  // A payer swaption is a call on the swap rate.
  const double scale = s.notional * result.annuity;
  result.normal_vol = normalVol(option->price / scale,
                                result.par_rate,
                                result.fixed_rate,
                                s.expiry_years,
                                s.is_payer);
  result.analytic_normal_vol = normalVol(option->analytic_price / scale,
                                         result.par_rate,
                                         result.fixed_rate,
                                         s.expiry_years,
                                         s.is_payer);
  result.option = std::move(*option);
  return result;
}

absl::StatusOr<CapResult> priceCap(const CapRequest& request) {
  const CapSpec& c = request.cap;
  if (!(c.notional > 0)) {
    return absl::InvalidArgumentError("notional must be positive");
  }
  if (!c.at_the_money && !std::isfinite(c.strike)) {
    return absl::InvalidArgumentError("strike must be finite");
  }
  const absl::StatusOr<int> periods =
      wholePeriods(c.maturity_years, c.payments_per_year, "maturity_years");
  if (!periods.ok()) return periods.status();
  if (*periods < 2) {
    return absl::InvalidArgumentError(
        "a cap needs at least two periods (the first has already fixed)");
  }

  HullWhiteTreeSpec tree_spec = request.tree;
  tree_spec.horizon_years = c.maturity_years;
  absl::StatusOr<FittedTree> fitted = fitTree(tree_spec);
  if (!fitted.ok()) return fitted.status();
  const ZeroSpotCurve& curve = *fitted->market_curve;
  const ShortRateTreeCurve* tree_curve = fitted->tree_curve.get();
  const auto& grid = tree_curve->trinomialTree().getTimegrid();

  CapResult result;
  const double tau = 1.0 / c.payments_per_year;
  for (int k = 1; k < *periods; ++k) {
    result.fixing_times.push_back(k * tau);
    result.payment_times.push_back((k + 1) * tau);
  }
  result.strike =
      c.at_the_money
          ? parSwapRate(curve, result.fixing_times[0], result.payment_times)
          : c.strike;

  const OptionPayoff payoff = c.is_cap ? OptionPayoff::Put : OptionPayoff::Call;
  for (size_t k = 0; k < result.fixing_times.size(); ++k) {
    const double fixing = result.fixing_times[k];
    const double payment = result.payment_times[k];
    for (double t : {fixing, payment}) {
      const auto index = grid.getTimeIndexForExpiry(t);
      if (!index.has_value() || std::abs(grid.time(*index) - t) > 1e-9) {
        return absl::InvalidArgumentError(
            "the caplets' dates don't fall on the tree's steps");
      }
    }
    // A caplet is a put on the zero-coupon bond from fixing to payment.
    FixedCashflowInstrument bond(tree_curve);
    if (absl::Status status = bond.setCashflows(
            {{.time_years = payment,
              .amount = c.notional * (1 + result.strike * tau)}});
        !status.ok()) {
      return status;
    }
    bond.price();
    const InterestRateSwap underlying = InterestRateSwap::createBond(bond);
    InterestRateDerivative caplet(tree_curve, &underlying);
    const double price =
        caplet.price(VanillaOption(c.notional, payoff), fixing);
    const double analytic = hullWhiteCaplet(curve,
                                            tree_spec.mean_reversion,
                                            tree_spec.sigma,
                                            fixing,
                                            payment,
                                            result.strike,
                                            c.notional,
                                            c.is_cap);
    const double forward = simpleForwardRate(curve, fixing, payment);
    result.forward_rates.push_back(forward);
    result.prices.push_back(price);
    result.analytic_prices.push_back(analytic);
    result.normal_vols.push_back(
        normalVol(analytic / (c.notional * tau * curve.df(payment)),
                  forward,
                  result.strike,
                  fixing,
                  c.is_cap));
    result.price += price;
    result.analytic_price += analytic;
  }
  return result;
}

absl::StatusOr<CalibrationResult> calibrateHullWhite(
    const CalibrationRequest& request) {
  if (absl::Status status = evaluateCurve(request.curve, {}).status();
      !status.ok()) {
    return status;
  }
  const ZeroSpotCurve curve(request.curve.maturities, request.curve.rates);
  return smileexplorer::calibrateHullWhite(
      curve, request.quotes, request.fixed_payments_per_year);
}

absl::StatusOr<std::vector<double>> modelNormalVols(
    const ModelVolsRequest& request) {
  if (absl::Status status = evaluateCurve(request.curve, {}).status();
      !status.ok()) {
    return status;
  }
  if (!(request.params.mean_reversion > 0) || !(request.params.sigma > 0)) {
    return absl::InvalidArgumentError(
        "mean_reversion and sigma must be positive");
  }
  if (std::ssize(request.quotes) > 400) {
    return absl::InvalidArgumentError("at most 400 quotes");
  }
  const ZeroSpotCurve curve(request.curve.maturities, request.curve.rates);
  std::vector<double> vols;
  for (const RateOptionQuote& q : request.quotes) {
    if (!(q.expiry > 0) || !(q.tenor > 0)) {
      return absl::InvalidArgumentError(
          "each quote needs a positive expiry and tenor");
    }
    vols.push_back(
        hullWhiteQuote(
            curve, request.params, q, request.fixed_payments_per_year)
            .normal_vol);
  }
  return vols;
}

}  // namespace smileexplorer::api
