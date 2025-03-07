#include "arrow_debreu.h"

#include <gtest/gtest.h>

#include "trees/propagators.h"
#include "trees/stochastic_tree_model.h"

namespace markets {
namespace {

TEST(ArrowDebreauTest, Basic) {
  double spot_rate = 0.05;

  auto rate_tree = BinomialTree::create(std::chrono::months(12 * 11),
                                        std::chrono::days(22),
                                        YearStyle::kBusinessDays252);
  StochasticTreeModel<CRRPropagator> rate_model(std::move(rate_tree),
                                                CRRPropagator(spot_rate));

  Volatility rate_vol(FlatVol(0.10));
  rate_model.forwardPropagate(rate_vol);

  StochasticTreeModel<ArrowDebreauPropagator> arrowdeb_model(
      BinomialTree::createFrom(rate_tree),
      ArrowDebreauPropagator(rate_tree, 100));
  arrowdeb_model.forwardPropagate();

  auto timegrid = rate_vol.generateTimegrid(11, 22. / 252);
  for (int i = 0; i <= 10; ++i) {
    int ti = timegrid.getTimeIndexForExpiry(i).value();
    std::cout << "time:" << i
              << "   df:" << arrowdeb_model.binomialTree().sumAtTimestep(ti)
              << std::endl;
  }

  for (int i = 0; i < 10; ++i) {
    int ti = timegrid.getTimeIndexForExpiry(i).value();
    int ti_next = timegrid.getTimeIndexForExpiry(i + 1).value();

    double df_i = arrowdeb_model.binomialTree().sumAtTimestep(ti);
    double df_next = arrowdeb_model.binomialTree().sumAtTimestep(ti_next);

    double t = timegrid.time(ti);
    double t_next = timegrid.time(ti_next);

    double fwdrate = std::log(df_i / df_next) / (t_next - t);

    std::cout << "time:" << i << "   fwdrate:" << fwdrate << std::endl;
  }

  for (int i = 1; i <= 10; ++i) {
    int ti = timegrid.getTimeIndexForExpiry(i).value();
    double df_i = arrowdeb_model.binomialTree().sumAtTimestep(ti);
    double t = timegrid.time(ti);
    double spotrate = std::log(1 / df_i) / (t);
    std::cout << "time:" << i << "   spotrate:" << spotrate << std::endl;
  }

  EXPECT_TRUE(false);
}

}  // namespace
}  // namespace markets
