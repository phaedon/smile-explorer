#ifndef SMILEEXPLORER_RATES_ZERO_CURVE_H_
#define SMILEEXPLORER_RATES_ZERO_CURVE_H_

#include <vector>

#include "rates/rates_curve.h"

namespace smileexplorer {

class ZeroSpotCurve : public RatesCurve {
 public:
  // Expects two matching vectors in order of increasing maturity. Assumed to be
  // zero-coupon bond (spot) yields.
  ZeroSpotCurve(std::vector<double> maturities,
                std::vector<double> rates,
                CompoundingPeriod period = CompoundingPeriod::kContinuous)
      : maturities_(maturities), rates_(rates), period_(period) {
    computeCurve();
  }
  ~ZeroSpotCurve() override = default;

  double df(double time) const override;

  double forwardRate(double start_time, double end_time) const override;

  // TODO: Exposed for testing.
  int findClosestMaturityIndex(double target) const;

  void updateRateAtMaturityIndex(int mat_index, double updated_rate) {
    rates_[mat_index] = updated_rate;
    computeCurve();
  }

  const std::vector<double>& getInputRates() const { return rates_; }

 private:
  std::vector<double> maturities_;
  std::vector<double> rates_;

  std::vector<double> discrete_dfs_;
  std::vector<double> df_maturities_;

  CompoundingPeriod period_;

  // Computes the forward rate between two time indices.
  // For example, if the time spacings are 0.5, then
  // getForwardRateByIndices(2, 4) will return the 1y x 2y fwd rate.
  double getForwardRateByIndices(int start_ti, int end_ti) const;

  void computeCurve();
};

}  // namespace smileexplorer

#endif  // SMILEEXPLORER_RATES_ZERO_CURVE_H_