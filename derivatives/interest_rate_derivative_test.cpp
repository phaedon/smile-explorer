#include "interest_rate_derivative.h"

#include <gtest/gtest.h>

#include "derivative.h"
#include "trees/propagators.h"
#include "trees/stochastic_tree_model.h"

namespace smileexplorer {

double estimateNormalVol(double f, double k, double lnvol, double expiry) {
  return lnvol * ((f - k) / std::log(f / k)) *
         (1 - (lnvol * lnvol * expiry) / 24.);
}

TEST(InterestRateDerivativeTest, RoughSanityCheck) {
  // Set up a binomial tree, treating the short rate as a "lognormal asset".
  const double disc_rate = 0.04;
  StochasticTreeModel<CRRPropagator> asset(BinomialTree(3.1, 1 / 360.),
                                           CRRPropagator(disc_rate));
  Volatility flat_vol(FlatVol(0.12));
  asset.forwardPropagate(flat_vol);
  ZeroSpotCurve curve({1.0, 10.0}, {disc_rate, disc_rate});
  SingleAssetDerivative binomial_option(&asset.binomialTree(), &curve);

  // 50 bps OTM from fwd (actually spot, but we've specified a flat yield curve
  // for simplicity).
  double strike = disc_rate + 0.0001;
  VanillaOption caplet(strike, OptionPayoff::Call);
  double bsm_caplet_price = binomial_option.price(caplet, 3.0);

  // Now set up the proper trinomial tree to simulate the same conditions.
  double normalvol = estimateNormalVol(disc_rate, strike, 0.12, 3.0);
  std::cout << normalvol << "  normal vol estimate" << std::endl;
  TrinomialTree tree(3.1, 0.005, 1 / 100., normalvol);
  tree.forwardPropagate(curve);
  ShortRateTreeCurve hullwhitecurve(std::move(tree));
  InterestRateDerivative trinomial_option(&hullwhitecurve);
  double hw_caplet_price = trinomial_option.price(caplet, 3.0);

  // EXPECT_NEAR(bsm_caplet_price, hw_caplet_price, 0.0001);
  //  TODO: Add a passing test.
}

}  // namespace smileexplorer
