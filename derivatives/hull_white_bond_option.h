#ifndef SMILEEXPLORER_DERIVATIVES_HULL_WHITE_BOND_OPTION_H_
#define SMILEEXPLORER_DERIVATIVES_HULL_WHITE_BOND_OPTION_H_

#include <cmath>

#include "bsm.h"
#include "rates/rates_curve.h"

namespace smileexplorer {

// The closed-form price, in the Hull-White model, of a European option
// expiring at `expiry` on a zero-coupon bond paying `face` at `maturity`
// (with expiry < maturity), given today's discount curve. See Hull, "Options,
// Futures, and Other Derivatives", section 32.3: the bond's forward price is
// lognormal, with volatility
//   sigma_P = sigma / a * (1 - exp(-a (maturity - expiry)))
//             * sqrt((1 - exp(-2 a expiry)) / (2 a)).
inline double hullWhiteZeroBondOption(const RatesCurve& curve,
                                      double mean_reversion,
                                      double sigma,
                                      double expiry,
                                      double maturity,
                                      double face,
                                      double strike,
                                      bool is_call) {
  const double a = mean_reversion;
  const double sigma_p = sigma / a * (1 - std::exp(-a * (maturity - expiry))) *
                         std::sqrt((1 - std::exp(-2 * a * expiry)) / (2 * a));
  const double bond = face * curve.df(maturity);
  const double pv_strike = strike * curve.df(expiry);
  const double h = std::log(bond / pv_strike) / sigma_p + sigma_p / 2;
  return is_call ? bond * normsdist(h) - pv_strike * normsdist(h - sigma_p)
                 : pv_strike * normsdist(sigma_p - h) - bond * normsdist(-h);
}

}  // namespace smileexplorer

#endif  // SMILEEXPLORER_DERIVATIVES_HULL_WHITE_BOND_OPTION_H_
