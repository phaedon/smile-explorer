#ifndef SMILEEXPLORER_RATES_MONOTONE_CUBIC_H_
#define SMILEEXPLORER_RATES_MONOTONE_CUBIC_H_

#include <vector>

namespace smileexplorer {

// Piecewise cubic Hermite interpolation that preserves the shape of the data
// (PCHIP): where the data are monotone between two knots, so is the curve, and
// it never overshoots a local extremum.
//
// The slope at each interior knot is the weighted harmonic mean of the
// neighbouring secant slopes (Fritsch & Butland, 1984), or zero where they
// differ in sign or either is zero, which satisfies the monotonicity
// conditions of Fritsch & Carlson (1980). See also Moler, "Numerical
// Computing with MATLAB", chapter 3. The slope at each end is the adjacent
// secant. These are the same choices as boost::math::interpolators::pchip.
class MonotoneCubicInterpolator {
 public:
  // The x's must be strictly increasing, with one y each, and at least two.
  MonotoneCubicInterpolator(std::vector<double> x, std::vector<double> y);

  // The interpolated value and its derivative. Outside [x.front(), x.back()],
  // the cubic of the nearest interval is extrapolated.
  double operator()(double x) const;
  double derivative(double x) const;

 private:
  std::vector<double> x_;
  std::vector<double> y_;
  std::vector<double> slopes_;

  // The index i of the interval [x_[i], x_[i + 1]] to evaluate at x.
  int intervalOf(double x) const;
};

}  // namespace smileexplorer

#endif  // SMILEEXPLORER_RATES_MONOTONE_CUBIC_H_
