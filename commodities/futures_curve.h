#ifndef SMILEEXPLORER_COMMODITIES_FUTURES_CURVE_H_
#define SMILEEXPLORER_COMMODITIES_FUTURES_CURVE_H_

#include <vector>

namespace smileexplorer {

// Today's futures curve for a commodity: the spot price, and futures prices
// for delivery at a set of times (in years). Between those times the curve is
// linear in the log of the price, starting from spot at t = 0, and it is flat
// after the last one.
class FuturesCurve {
 public:
  // The times must be positive and increasing, with one positive price each,
  // and the spot must be positive.
  FuturesCurve(double spot,
               std::vector<double> times,
               std::vector<double> prices);

  double spot() const { return spot_; }
  const std::vector<double>& times() const { return times_; }
  const std::vector<double>& prices() const { return prices_; }

  // The futures price for delivery at time t (the spot for t <= 0).
  double price(double t) const;

  // ln(F(t) / S) / t, for t > 0: the cost of carry the curve implies. Under
  // F = S exp((r + u - y) t), it is the financing rate r plus the storage cost
  // u, less the convenience yield y.
  double impliedCarry(double t) const;

 private:
  double spot_;
  std::vector<double> times_;
  std::vector<double> prices_;
};

}  // namespace smileexplorer

#endif  // SMILEEXPLORER_COMMODITIES_FUTURES_CURVE_H_
