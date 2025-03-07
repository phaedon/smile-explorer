#ifndef MARKETS_RATES_SWAPS_H_
#define MARKETS_RATES_SWAPS_H_

#include "time.h"
#include "trees/binomial_tree.h"

namespace markets {

template <CompoundingPeriod>
double swapRate(const BinomialTree& adtree, std::chrono::years maturity);

template <>
inline double swapRate<CompoundingPeriod::kAnnual>(
    const BinomialTree& adtree, std::chrono::years maturity) {
  double df_sum = 0;
  double final_df = 0;
  for (int t = 1; t <= maturity.count(); ++t) {
    int timestep_index = t / adtree.exactTimestepInYears();
    double df = adtree.sumAtTimestep(timestep_index);
    // std::cout << "df at time:" << t << " timestep_index:" << timestep_index
    //           << " df:" << df << std::endl;
    df_sum += df;

    if (t == maturity.count()) {
      final_df = df;
    }
  }

  return (1 - final_df) / df_sum;
}

}  // namespace markets

#endif  // MARKETS_RATES_SWAPS_H_