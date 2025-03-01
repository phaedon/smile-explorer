#include "arrow_debreu.h"

#include <gtest/gtest.h>

#include "markets/propagators.h"

namespace markets {
namespace {

TEST(ArrowDebreauTest, Basic) {
  double spot_rate = 0.05;
  CRRPropagator crr_prop(spot_rate);
  auto rate_tree = BinomialTree::create(std::chrono::months(12 * 11),
                                        std::chrono::days(22),
                                        YearStyle::kBusinessDays252);
  Volatility rate_vol(FlatVol(0.10));
  rate_tree.forwardPropagate(crr_prop, rate_vol);

  ArrowDebreauPropagator arrowdeb_prop(rate_tree, 100);
  auto arrowdeb_tree = BinomialTree::createFrom(rate_tree);
  arrowdeb_tree.forwardPropagate(arrowdeb_prop);

  auto timegrid = rate_vol.generateTimegrid(11, 22. / 252);
  for (int i = 0; i <= 10; ++i) {
    int ti = timegrid.getTimeIndexForExpiry(i).value();
    std::cout << "time:" << i << "   df:" << arrowdeb_tree.sumAtTimestep(ti)
              << std::endl;
  }

  for (int i = 0; i < 10; ++i) {
    int ti = timegrid.getTimeIndexForExpiry(i).value();
    int ti_next = timegrid.getTimeIndexForExpiry(i + 1).value();

    double df_i = arrowdeb_tree.sumAtTimestep(ti);
    double df_next = arrowdeb_tree.sumAtTimestep(ti_next);

    double t = timegrid.time(ti);
    double t_next = timegrid.time(ti_next);

    double fwdrate = std::log(df_i / df_next) / (t_next - t);

    std::cout << "time:" << i << "   fwdrate:" << fwdrate << std::endl;
  }

  for (int i = 1; i <= 10; ++i) {
    int ti = timegrid.getTimeIndexForExpiry(i).value();
    double df_i = arrowdeb_tree.sumAtTimestep(ti);
    double t = timegrid.time(ti);
    double spotrate = std::log(1 / df_i) / (t);
    std::cout << "time:" << i << "   spotrate:" << spotrate << std::endl;
  }

  EXPECT_TRUE(false);
}

}  // namespace
}  // namespace markets
