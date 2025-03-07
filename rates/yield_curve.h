#include <algorithm>
#include <boost/math/interpolators/pchip.hpp>
#include <cmath>  // For std::exp if using continuous compounding
#include <vector>

class YieldCurve {
 public:
  YieldCurve(std::vector<double> maturities, std::vector<double> rates)
      : min_maturity_(maturities[0]),
        min_rate_(rates[0]),
        spline_(std::move(maturities), std::move(rates)) {}

  double getRate(double t) const {
    if (t < min_maturity_) {
      return min_rate_;
    }
    return spline_(t);
  }

 private:
  double min_maturity_;
  double min_rate_;
  boost::math::interpolators::pchip<std::vector<double>> spline_;
  // std::vector<double> maturities_;
  // std::vector<double> rates_;
  // double compounding_frequency_;  // For adjusting rates if necessary
};