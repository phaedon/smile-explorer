
#ifndef SMILE_EXPLORER_DERIVATIVES_BSM_
#define SMILE_EXPLORER_DERIVATIVES_BSM_

#include <cmath>

namespace smileexplorer {

inline double normsdist(double x) {
  return 0.5 * (1.0 + std::erf(x / std::sqrt(2.0)));
}

struct BSMIntermediates {
  double d1;
  double d2;
  double ert;
  double ebt;
};

// Helper function to calculate d1 and d2
inline BSMIntermediates calculateBSMIntermediates(
    double S, double K, double vol, double t, double r, double div) {
  BSMIntermediates bsm_vals;
  double nu = vol * std::sqrt(t);
  bsm_vals.ert = std::exp(r * t);
  bsm_vals.ebt = std::exp(div * t);
  double erbt = std::exp((r - div) * t);
  bsm_vals.d1 = std::log(S * erbt / K) / nu + nu / 2;
  bsm_vals.d2 = std::log(S * erbt / K) / nu - nu / 2;
  return bsm_vals;
}

// BSM with risk-free rate and continuous (known) dividend yield.
inline double call(
    double S, double K, double vol, double t, double r, double div) {
  const auto bsm_vals = calculateBSMIntermediates(S, K, vol, t, r, div);
  return S * normsdist(bsm_vals.d1) / bsm_vals.ebt -
         K * normsdist(bsm_vals.d2) / bsm_vals.ert;
}

// BSM put with risk-free rate and continuous (known) dividend yield.
inline double put(
    double S, double K, double vol, double t, double r, double div) {
  const auto bsm_vals = calculateBSMIntermediates(S, K, vol, t, r, div);
  return K * normsdist(-bsm_vals.d2) / bsm_vals.ert -
         S * normsdist(-bsm_vals.d1) / bsm_vals.ebt;
}

inline double call_delta(
    double S, double K, double vol, double t, double r, double div) {
  const auto bsm_vals = calculateBSMIntermediates(S, K, vol, t, r, div);
  return std::exp(-div * t) * normsdist(bsm_vals.d1);
}

inline double put_delta(
    double S, double K, double vol, double t, double r, double div) {
  const auto bsm_vals = calculateBSMIntermediates(S, K, vol, t, r, div);
  return std::exp(-div * t) * (normsdist(bsm_vals.d1) - 1.0);
}

}  // namespace smileexplorer

#endif  // SMILE_EXPLORER_DERIVATIVES_BSM_