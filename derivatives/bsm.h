
#ifndef SMILE_EXPLORER_DERIVATIVES_BSM_
#define SMILE_EXPLORER_DERIVATIVES_BSM_

#include <cmath>
#include <complex>

namespace markets {

inline double normsdist(double x) {
  return 0.5 * (1.0 + std::erf(x / std::sqrt(2.0)));
}

// BSM with risk-free rate and continuous (known) dividend yield.
inline double call(
    double S, double K, double vol, double t, double r, double div) {
  double nu = vol * std::sqrt(t);
  double ert = std::exp(r * t);
  double ebt = std::exp(div * t);
  double erbt = std::exp((r - div) * t);
  double d1 = std::log(S * erbt / K) / nu + nu / 2;
  double d2 = std::log(S * erbt / K) / nu - nu / 2;
  return S * normsdist(d1) / ebt - K * normsdist(d2) / ert;
}

// BSM with a risk-free rate.
inline double call(double S, double K, double vol, double t, double r) {
  return call(S, K, vol, t, r, 0.0);
}

// Simplest version: zero rates, no dividends.
inline double call(double S, double K, double vol, double t) {
  return call(S, K, vol, t, 0.0);
}

inline double call_delta(
    double S, double K, double vol, double t, double r, double div) {
  double nu = vol * std::sqrt(t);
  double erbt = std::exp((r - div) * t);
  double d1 = std::log(S * erbt / K) / nu + nu / 2;
  return normsdist(d1);
}

}  // namespace markets

#endif  // SMILE_EXPLORER_DERIVATIVES_BSM_