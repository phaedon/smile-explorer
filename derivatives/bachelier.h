#ifndef SMILEEXPLORER_DERIVATIVES_BACHELIER_H_
#define SMILEEXPLORER_DERIVATIVES_BACHELIER_H_

#include <algorithm>
#include <cmath>
#include <optional>

#include "bsm.h"

// The Bachelier (normal) model, in which interest rate options are quoted: the
// forward rate moves by normally distributed amounts, with a "normal vol" in
// rate units per sqrt(year) (0.01 = 100 basis points).

namespace smileexplorer {

// The undiscounted price of an option paying max(F - K, 0) (call) or
// max(K - F, 0) (put) on a forward rate F at expiry. Multiply by the annuity
// for a swaption, or by tau * P(0, payment) for a caplet.
inline double bachelier(double forward,
                        double strike,
                        double normal_vol,
                        double expiry,
                        bool is_call) {
  const double intrinsic = is_call ? std::max(forward - strike, 0.0)
                                   : std::max(strike - forward, 0.0);
  const double stddev = normal_vol * std::sqrt(expiry);
  if (!(stddev > 0)) return intrinsic;
  const double d = (forward - strike) / stddev;
  const double sign = is_call ? 1.0 : -1.0;
  return sign * (forward - strike) * normsdist(sign * d) + stddev * normpdf(d);
}

// The normal vol at which bachelier(...) equals `price`, or nullopt if the
// price is at or below intrinsic value (no vol produces it) or not finite.
inline std::optional<double> impliedNormalVol(
    double price, double forward, double strike, double expiry, bool is_call) {
  const double intrinsic = bachelier(forward, strike, 0, expiry, is_call);
  if (!std::isfinite(price) || !(price > intrinsic) || !(expiry > 0)) {
    return std::nullopt;
  }
  // The price rises with vol without bound: bracket, then bisect.
  double lo = 0.0;
  double hi = 0.01;
  while (bachelier(forward, strike, hi, expiry, is_call) < price) hi *= 2;
  for (int k = 0; k < 200 && hi - lo > 1e-15; ++k) {
    const double mid = 0.5 * (lo + hi);
    (bachelier(forward, strike, mid, expiry, is_call) < price ? lo : hi) = mid;
  }
  return 0.5 * (lo + hi);
}

}  // namespace smileexplorer

#endif  // SMILEEXPLORER_DERIVATIVES_BACHELIER_H_
