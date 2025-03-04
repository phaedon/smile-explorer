#ifndef MARKETS_PROPAGATORS_H_
#define MARKETS_PROPAGATORS_H_

#include "markets/binomial_tree.h"

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

}  // namespace markets

#endif  // MARKETS_PROPAGATORS_H_