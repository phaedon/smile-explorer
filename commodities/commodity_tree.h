#ifndef SMILEEXPLORER_COMMODITIES_COMMODITY_TREE_H_
#define SMILEEXPLORER_COMMODITIES_COMMODITY_TREE_H_

#include <iterator>
#include <vector>

#include "commodities/futures_curve.h"
#include "trees/hull_white_propagator.h"
#include "trees/trinomial_tree.h"

// A trinomial tree for a mean-reverting commodity price, fitted to today's
// futures curve. This is the one-factor model in John Hull, "Options,
// Futures, and Other Derivatives", in the chapter on commodity derivatives:
//
//   ln S(t) = X(t) + alpha(t),   dX = -a X dt + sigma dz,   X(0) = 0.
//
// X is built exactly like the Hull-White short-rate tree (same spacing,
// branching and probabilities), and alpha(t) plays the part of the short-rate
// tree's fitted shift: at each time slice it is chosen so that the tree's
// expected spot equals the futures price for delivery then.

namespace smileexplorer {

// An option on the futures contract delivering at time slice
// `delivery_index`, expiring at `expiry_index` (no later than delivery).
struct OptionOnFuturesSpec {
  int delivery_index;
  int expiry_index;
  double strike;
  bool is_call = true;
  bool is_american = false;
};

struct OptionOnFuturesValues {
  // The option as specified, and its European counterpart.
  double price = 0.0;
  double european_price = 0.0;
  // Per time slice up to expiry, per node (lowest price first):
  //   - the futures price,
  //   - the option's value,
  //   - whether the holder exercises there (always false for a European
  //     option before expiry; at expiry, whether it ends in the money).
  std::vector<std::vector<double>> futures;
  std::vector<std::vector<double>> values;
  std::vector<std::vector<bool>> exercised;
};

class CommodityTree {
 public:
  // The tree covers [0, horizon_years], rounded up to a whole number of
  // steps of dt. The mean reversion and sigma must be positive.
  CommodityTree(const FuturesCurve& curve,
                double mean_reversion,
                double sigma,
                double dt,
                double horizon_years);

  int numSlices() const { return std::ssize(tree_.tree_); }
  int numStates(int ti) const { return tree_.numStatesAt(ti); }
  double dt() const { return dt_; }
  double time(int ti) const { return ti * dt_; }
  // Spacing between neighbouring values of X: sigma * sqrt(3 dt).
  double dx() const { return dR(sigma_, dt_); }
  int jMax() const { return propagator_.jMax(); }

  const TrinomialTree& tree() const { return tree_; }

  // The fitted shift at slice ti: ln S = x + alpha.
  double alpha(int ti) const { return alphas_[ti]; }
  // X at a node, before fitting.
  double x(int ti, int i) const { return tree_.nodeValue(ti, i); }
  double spot(int ti, int i) const;
  // Probability of reaching the node from today.
  double probability(int ti, int i) const {
    return tree_.node(ti, i).arrow_debreu;
  }
  // Index, in slice ti + 1, of the node's middle successor (the up and down
  // successors are one above and one below it).
  int successorMid(int ti, int i) const { return successor_mid_[ti][i]; }

  // The tree's expected spot at slice ti, which the fit makes equal to the
  // futures curve there.
  double expectedSpot(int ti) const;

  // The price of the futures contract delivering at slice `delivery_index`,
  // at every node up to then. The futures price is the expected spot at
  // delivery (a martingale), so this is backward induction with no
  // discounting.
  std::vector<std::vector<double>> futuresValues(int delivery_index) const;

  // Prices an option on futures by backward induction, discounting at a flat,
  // continuously compounded rate.
  OptionOnFuturesValues optionOnFutures(const OptionOnFuturesSpec& spec,
                                        double rate) const;

 private:
  double sigma_;
  double dt_;
  HullWhitePropagator propagator_;
  TrinomialTree tree_;
  std::vector<double> alphas_;
  std::vector<std::vector<int>> successor_mid_;

  // The probability-weighted value of the node's three successors.
  double expectNext(int ti, int i, const std::vector<double>& next) const;
};

// The volatility of the futures price for delivery in tau years: in this
// model, near contracts move with the spot, and far ones much less (the
// Samuelson effect).
double instantaneousFuturesVol(double mean_reversion, double sigma, double tau);

// The Black-76 volatility of an option expiring at `expiry` on the futures
// delivering at `delivery`: the root mean square of the futures' volatility
// over the option's life.
double samuelsonImpliedVol(double mean_reversion,
                           double sigma,
                           double expiry,
                           double delivery);

}  // namespace smileexplorer

#endif  // SMILEEXPLORER_COMMODITIES_COMMODITY_TREE_H_
