#ifndef MARKETS_PROPAGATORS_H_
#define MARKETS_PROPAGATORS_H_

#include <memory>

#include "markets/binomial_tree.h"
#include "markets/volatility.h"

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
  JarrowRuddPropagator(double expected_drift,
                       double annualized_vol,
                       double spot_price)
      : expected_drift_(expected_drift),
        annualized_vol_(annualized_vol),
        spot_price_(spot_price) {}

  double operator()(const BinomialTree& tree, int t, int i) const {
    if (t == 0) return spot_price_;
    double dt = tree.timestepAt(t);

    if (i == 0) {
      double d = expected_drift_ * dt - annualized_vol_ * std::sqrt(dt);
      return tree.nodeValue(t - 1, 0) * std::exp(d);
    } else {
      double u = expected_drift_ * dt + annualized_vol_ * std::sqrt(dt);
      return tree.nodeValue(t - 1, i - 1) * std::exp(u);
    }
  }

  void updateVol(double vol) { annualized_vol_ = vol; }
  void updateSpot(double spot) { spot_price_ = spot; }

  double expected_drift_;
  double annualized_vol_;
  double spot_price_;
};

}  // namespace markets

#endif  // MARKETS_PROPAGATORS_H_