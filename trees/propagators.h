#ifndef SMILEEXPLORER_TREES_PROPAGATORS_H_
#define SMILEEXPLORER_TREES_PROPAGATORS_H_

#include <algorithm>
#include <cmath>
#include <limits>

#include "rates/rates_curve.h"
#include "trees/binomial_tree.h"
#include "volatility/volatility.h"

namespace smileexplorer {

// CRR = Cox-Ross-Rubinstein convention for forward-propagation of a stochastic
// variable in a binomial tree.
// The main characteristic is that up_move == -down_move.
struct CRRPropagator {
  CRRPropagator(double spot_price) : spot_price_(spot_price) {}

  template <typename VolatilityT>
  double operator()(const BinomialTree& tree,
                    const VolatilityT& vol_fn,
                    int t,
                    int i) const {
    if (t == 0) return spot_price_;
    // The move into time index t spans the timestep that starts at t - 1.
    double prev_time = tree.totalTimeAtIndex(t - 1);

    double dt = tree.timestepAt(t - 1);
    double u = vol_fn.get(prev_time) * std::sqrt(dt);

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

// Jarrow-Rudd convention for forward-propagation of a stochastic variable in a
// binomial tree.
//
// It sets the branching probability (not risk-neutral) to 0.5 at each node, so
// the evolution of the asset follows some mean return/drift.
struct JarrowRuddPropagator {
  JarrowRuddPropagator(double expected_drift, double spot_price)
      : expected_drift_(expected_drift), spot_price_(spot_price) {}

  template <typename VolatilityT>
  double operator()(const BinomialTree& tree,
                    const VolatilityT& vol_fn,
                    int t,
                    int i) const {
    if (t == 0) return spot_price_;
    // The move into time index t spans the timestep that starts at t - 1.
    double dt = tree.timestepAt(t - 1);
    double prev_time = tree.totalTimeAtIndex(t - 1);

    if (i == 0) {
      double d = expected_drift_ * dt - vol_fn.get(prev_time) * std::sqrt(dt);
      return tree.nodeValue(t - 1, 0) * std::exp(d);
    } else {
      double u = expected_drift_ * dt + vol_fn.get(prev_time) * std::sqrt(dt);
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
  LocalVolatilityPropagator(const RatesCurve& curve, double spot_price)
      : curve_(curve), spot_price_(spot_price) {}

  template <typename VolatilityT>
  double operator()(const BinomialTree& tree,
                    const VolatilityT& vol_fn,
                    int t,
                    int i) const {
    if (t == 0) return spot_price_;

    // ===========================
    // Set the spine of the tree:
    // ===========================

    // Simplest case for the spine:
    if (i == t / 2 && t % 2 == 0) {
      // Odd number of nodes (even time-index)
      return spot_price_;
    }

    double dt = tree.timestepAt(t - 1);

    // The higher node in the previous timestep (if it exists).
    const auto Sprev_high = tree.safeNodeValue(t - 1, i);

    // The lower node in the previous timestep (if it exists).
    const auto Sprev_low = tree.safeNodeValue(t - 1, i - 1);

    double prev_time = tree.totalTimeAtIndex(t - 1);
    if (i == (t + 1) / 2 && t % 2 == 1) {
      // Even number of nodes (odd time-index), upper spine:
      double S = Sprev_low.value();
      double sigma = localVol(vol_fn, S, prev_time);
      return S * std::exp(sigma * std::sqrt(dt));
    } else if (i == (t - 1) / 2 && t % 2 == 1) {
      // Even number of nodes, lower spine:
      double S = Sprev_high.value();
      double sigma = localVol(vol_fn, S, prev_time);
      return S * std::exp(-sigma * std::sqrt(dt));
    }

    // ======================================================
    // Set the nodes of the tree above or below the spine.
    // ======================================================

    double curr_time = tree.totalTimeAtIndex(t);

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
      double sigma = localVol(vol_fn, S, prev_time);
      const double S_u = F + (S * S * sigma * sigma * dt) / (F - S_d);
      // The node must lie above its parent's forward and, if there is a node
      // above the parent, below that one's forward: otherwise one of the
      // two branching probabilities leaves (0, 1). Where it doesn't, override
      // it as Derman and Kani do, keeping both forwards bracketed.
      const double F_above = Sprev_high.has_value()
                                 ? calculateForward(Sprev_high.value())
                                 : std::numeric_limits<double>::infinity();
      if (S_u > F && S_u < F_above && std::isfinite(S_u)) return S_u;
      return std::isfinite(F_above)
                 ? (F + F_above) / 2
                 : std::max(F, S_d) * std::exp(sigma * std::sqrt(dt));
    } else {
      // Populate lower nodes.
      const double S_u = tree.nodeValue(t, i + 1);
      double S = Sprev_high.value();
      const double F = calculateForward(S);
      double sigma = localVol(vol_fn, S, prev_time);
      const double S_d = F - (S * S * sigma * sigma * dt) / (S_u - F);
      // As above: below the parent's forward, and above the forward of the
      // node below the parent (if any).
      const double F_below =
          Sprev_low.has_value() ? calculateForward(Sprev_low.value()) : 0.0;
      if (S_d < F && S_d > F_below && std::isfinite(S_d)) return S_d;
      return Sprev_low.has_value()
                 ? (F + F_below) / 2
                 : std::min(F, S_u) * std::exp(-sigma * std::sqrt(dt));
    }
  }

  void updateSpot(double spot) { spot_price_ = spot; }

 private:
  // Local vol at asset price s, at time t if the surface depends on time.
  template <typename VolatilityT>
  static double localVol(const VolatilityT& vol_fn, double s, double t) {
    if constexpr (VolatilityT::type ==
                  VolSurfaceFnType::kTimeVaryingSkewSmile) {
      return vol_fn.get(s, t);
    } else {
      return vol_fn.get(s);
    }
  }

  const RatesCurve& curve_;
  double spot_price_;
};

}  // namespace smileexplorer

#endif  // SMILEEXPLORER_TREES_PROPAGATORS_H_