#ifndef MARKETS_PROPAGATORS_H_
#define MARKETS_PROPAGATORS_H_

#include <cmath>

#include "absl/log/log.h"
#include "markets/binomial_tree.h"
#include "rates/rates_curve.h"

namespace markets {

struct CRRPropagator {
  CRRPropagator(double spot_price) : spot_price_(spot_price) {}

  template <typename VolatilityT>
  double operator()(const BinomialTree& tree,
                    const VolatilityT& vol_fn,
                    int t,
                    int i) const {
    if (t == 0) return spot_price_;
    double curr_time = tree.totalTimeAtIndex(t);

    double dt = tree.timestepAt(t);
    double u = vol_fn.get(curr_time) * std::sqrt(dt);

    if (i == 0) {
      double d = -u;
      return tree.nodeValue(t - 1, 0) * std::exp(d);
    }

    return tree.nodeValue(t - 1, i - 1) * std::exp(u);
  }

  void updateSpot(double spot) { spot_price_ = spot; }

 private:
  double spot_price_;
};

struct JarrowRuddPropagator {
  JarrowRuddPropagator(double expected_drift, double spot_price)
      : expected_drift_(expected_drift), spot_price_(spot_price) {}

  template <typename VolatilityT>
  double operator()(const BinomialTree& tree,
                    const VolatilityT& vol_fn,
                    int t,
                    int i) const {
    if (t == 0) return spot_price_;
    double dt = tree.timestepAt(t);
    double curr_time = tree.totalTimeAtIndex(t);

    if (i == 0) {
      double d = expected_drift_ * dt - vol_fn.get(curr_time) * std::sqrt(dt);
      return tree.nodeValue(t - 1, 0) * std::exp(d);
    } else {
      double u = expected_drift_ * dt + vol_fn.get(curr_time) * std::sqrt(dt);
      return tree.nodeValue(t - 1, i - 1) * std::exp(u);
    }
  }

  void updateSpot(double spot) { spot_price_ = spot; }

  double expected_drift_;
  double spot_price_;
};

// Note: This could also be done as a SFINAE-style override in the CRRPropagator
// (since that is the approach) but for now separating this out seems like a
// reasonable approach.
struct LocalVolatilityPropagator {
  LocalVolatilityPropagator(const ZeroSpotCurve& curve, double spot_price)
      : curve_(curve), spot_price_(spot_price) {}

  template <typename VolatilityT>
  double operator()(const BinomialTree& tree,
                    const VolatilityT& vol_fn,
                    int t,
                    int i) const {
    if (t == 0) return spot_price_;

    // Simplest case for the spine:
    if (i == t / 2 && t % 2 == 0) {
      // Odd number of nodes (even time-index)
      return spot_price_;
    }

    double curr_time = tree.totalTimeAtIndex(t);
    double prev_time = tree.totalTimeAtIndex(t - 1);

    double dt = tree.timestepAt(t - 1);

    // The higher node in the previous timestep (if it exists).
    const auto Sprev_high = tree.safeNodeValue(t - 1, i);

    // The lower node in the previous timestep (if it exists).
    const auto Sprev_low = tree.safeNodeValue(t - 1, i - 1);

    if (i == (t + 1) / 2 && t % 2 == 1) {
      // Even number of nodes (odd time-index), upper spine:
      double S = Sprev_low.value();
      double sigma = vol_fn.get(S);
      return S * std::exp(sigma * std::sqrt(dt));
    } else if (i == (t - 1) / 2 && t % 2 == 1) {
      // Even number of nodes, lower spine:
      double S = Sprev_high.value();
      double sigma = vol_fn.get(S);
      return S * std::exp(-sigma * std::sqrt(dt));
    }

    // Helper function to calculate the forward price
    auto calculateForward = [&](double S) {
      return S * curve_.df(prev_time) / curve_.df(curr_time);
    };

    // We're no longer on the spine:
    double half_t = t * 0.5;
    if (i > half_t) {
      // Populate upper nodes.
      const double S_d = tree.nodeValue(t, i - 1);
      double S = Sprev_low.value();
      const double F = calculateForward(S);
      double sigma = vol_fn.get(S);
      return F + (S * S * sigma * sigma * dt) / (F - S_d);
    } else {
      // Populate lower nodes.
      const double S_u = tree.nodeValue(t, i + 1);
      double S = Sprev_high.value();
      const double F = calculateForward(S);
      double sigma = vol_fn.get(S);
      return F - (S * S * sigma * sigma * dt) / (S_u - F);
    }
  }

 private:
  // For now, to avoid variant and templating we just start with the simplest
  // approach which still supports some kind of time-varying discount curve.
  const ZeroSpotCurve& curve_;
  double spot_price_;
};

}  // namespace markets

#endif  // MARKETS_PROPAGATORS_H_