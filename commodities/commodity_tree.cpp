#include "commodities/commodity_tree.h"

#include <algorithm>
#include <cmath>

namespace smileexplorer {

CommodityTree::CommodityTree(const FuturesCurve& curve,
                             double mean_reversion,
                             double sigma,
                             double dt,
                             double horizon_years)
    : sigma_(sigma),
      dt_(dt),
      propagator_(mean_reversion, sigma, dt),
      // TrinomialTree rounds its duration up to a whole number of steps, so
      // pass slightly less than num_steps * dt to get exactly num_steps
      // (allowing for rounding error, so that 0.3 / 0.1 is 3 steps, not 4).
      tree_(std::ceil(horizon_years / dt - 1e-9) * dt * (1 - 1e-12), dt) {
  const int num_slices = std::ssize(tree_.tree_);
  for (int ti = 0; ti < num_slices; ++ti) {
    for (int i = 0; i < propagator_.numStatesAtTimeIndex(ti); ++i) {
      tree_.tree_[ti].push_back(propagator_(ti, i));
    }
  }

  // Carry the probabilities of reaching each node forward, slice by slice,
  // fitting alpha as each slice is completed. Unlike the short-rate tree's
  // Arrow-Debreu prices, nothing is discounted.
  alphas_.resize(num_slices);
  successor_mid_.resize(num_slices);
  tree_.tree_[0][0].arrow_debreu = 1.0;
  for (int ti = 0; ti < num_slices; ++ti) {
    double expected_exp_x = 0.0;
    for (int i = 0; i < numStates(ti); ++i) {
      const TrinomialNode& node = tree_.node(ti, i);
      expected_exp_x += node.arrow_debreu * std::exp(node.state_value);
    }
    alphas_[ti] = std::log(curve.price(time(ti)) / expected_exp_x);

    if (ti + 1 == num_slices) break;
    for (int i = 0; i < numStates(ti); ++i) {
      TrinomialNode& node = tree_.tree_[ti][i];
      auto next = tree_.getSuccessorNodes(node, ti, i);
      next.up.arrow_debreu += node.branch_probs.pu * node.arrow_debreu;
      next.mid.arrow_debreu += node.branch_probs.pm * node.arrow_debreu;
      next.down.arrow_debreu += node.branch_probs.pd * node.arrow_debreu;
      successor_mid_[ti].push_back(
          static_cast<int>(&next.mid - &tree_.tree_[ti + 1][0]));
    }
  }
}

double CommodityTree::spot(int ti, int i) const {
  return std::exp(alphas_[ti] + x(ti, i));
}

double CommodityTree::expectedSpot(int ti) const {
  double expected = 0.0;
  for (int i = 0; i < numStates(ti); ++i) {
    expected += probability(ti, i) * spot(ti, i);
  }
  return expected;
}

double CommodityTree::expectNext(int ti,
                                 int i,
                                 const std::vector<double>& next) const {
  const BranchProbabilities& p = tree_.node(ti, i).branch_probs;
  const int m = successor_mid_[ti][i];
  return p.pu * next[m + 1] + p.pm * next[m] + p.pd * next[m - 1];
}

std::vector<std::vector<double>> CommodityTree::futuresValues(
    int delivery_index) const {
  std::vector<std::vector<double>> futures(delivery_index + 1);
  for (int i = 0; i < numStates(delivery_index); ++i) {
    futures[delivery_index].push_back(spot(delivery_index, i));
  }
  for (int ti = delivery_index - 1; ti >= 0; --ti) {
    for (int i = 0; i < numStates(ti); ++i) {
      futures[ti].push_back(expectNext(ti, i, futures[ti + 1]));
    }
  }
  return futures;
}

OptionOnFuturesValues CommodityTree::optionOnFutures(
    const OptionOnFuturesSpec& spec, double rate) const {
  OptionOnFuturesValues result;
  result.futures = futuresValues(spec.delivery_index);
  result.futures.resize(spec.expiry_index + 1);

  const auto payoff = [&](double f) {
    return std::max(spec.is_call ? f - spec.strike : spec.strike - f, 0.0);
  };
  const double df = std::exp(-rate * dt_);
  const int T = spec.expiry_index;
  result.values.resize(T + 1);
  result.exercised.resize(T + 1);
  std::vector<double> european;
  for (double f : result.futures[T]) {
    result.values[T].push_back(payoff(f));
    result.exercised[T].push_back(payoff(f) > 0);
    european.push_back(payoff(f));
  }
  for (int ti = T - 1; ti >= 0; --ti) {
    std::vector<double> european_here;
    for (int i = 0; i < numStates(ti); ++i) {
      european_here.push_back(df * expectNext(ti, i, european));
      const double continuation = df * expectNext(ti, i, result.values[ti + 1]);
      const double exercise = payoff(result.futures[ti][i]);
      // Exercise only when it's worth strictly more, beyond rounding: deep in
      // the money at zero rates, the two agree but for the last bit.
      const bool exercised =
          spec.is_american && exercise - continuation > 1e-12 * spec.strike;
      result.values[ti].push_back(exercised ? exercise : continuation);
      result.exercised[ti].push_back(exercised);
    }
    european = std::move(european_here);
  }
  result.price = result.values[0][0];
  result.european_price = european[0];
  return result;
}

double instantaneousFuturesVol(double mean_reversion,
                               double sigma,
                               double tau) {
  return sigma * std::exp(-mean_reversion * tau);
}

double samuelsonImpliedVol(double mean_reversion,
                           double sigma,
                           double expiry,
                           double delivery) {
  // The variance of ln F over [0, expiry] is
  //   sigma^2 exp(-2a (T - t0)) (1 - exp(-2a t0)) / (2a),
  // and the factor (1 - exp(-x)) / x tends to 1 as x -> 0.
  const double x = 2 * mean_reversion * expiry;
  const double shrink = x < 1e-12 ? 1.0 : -std::expm1(-x) / x;
  return instantaneousFuturesVol(mean_reversion, sigma, delivery - expiry) *
         std::sqrt(shrink);
}

}  // namespace smileexplorer
