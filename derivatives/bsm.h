
#ifndef SMILE_EXPLORER_DERIVATIVES_BSM_
#define SMILE_EXPLORER_DERIVATIVES_BSM_

#include <cmath>
#include <numbers>

namespace smileexplorer {

inline double normsdist(double x) {
  return 0.5 * (1.0 + std::erf(x / std::numbers::sqrt2));
}

inline double normpdf(double x) {
  constexpr double inv_sqrt_2pi =
      std::numbers::inv_sqrtpi / std::numbers::sqrt2;
  return std::exp(-0.5 * x * x) * inv_sqrt_2pi;
}

struct BSMIntermediates {
  double d1;
  double d2;
  double ert;
  double ebt;
  double e_neg_rt;
  double e_neg_bt;
};

// Helper function to calculate d1 and d2
inline BSMIntermediates calculateBSMIntermediates(
    double S, double K, double vol, double t, double r, double div) {
  BSMIntermediates bsm_vals;
  double nu = vol * std::sqrt(t);
  bsm_vals.ert = std::exp(r * t);
  bsm_vals.ebt = std::exp(div * t);
  bsm_vals.e_neg_rt = std::exp(-r * t);
  bsm_vals.e_neg_bt = std::exp(-div * t);

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
  return bsm_vals.e_neg_bt * normsdist(bsm_vals.d1);
}

inline double put_delta(
    double S, double K, double vol, double t, double r, double div) {
  const auto bsm_vals = calculateBSMIntermediates(S, K, vol, t, r, div);
  return bsm_vals.e_neg_bt * (normsdist(bsm_vals.d1) - 1.0);
}

inline double vega(
    double S, double K, double vol, double t, double r, double div) {
  const auto bsm_vals = calculateBSMIntermediates(S, K, vol, t, r, div);
  return S * bsm_vals.e_neg_bt * normpdf(bsm_vals.d1) * std::sqrt(t) * 0.01;
}

inline double gamma(
    double S, double K, double vol, double t, double r, double div) {
  const auto bsm_vals = calculateBSMIntermediates(S, K, vol, t, r, div);
  return bsm_vals.e_neg_bt * normpdf(bsm_vals.d1) / (S * vol * std::sqrt(t));
}

inline double call_theta(
    double S, double K, double vol, double t, double r, double div) {
  const auto bsm_vals = calculateBSMIntermediates(S, K, vol, t, r, div);
  return -(S * bsm_vals.e_neg_bt * normpdf(bsm_vals.d1) * vol /
           (2.0 * std::sqrt(t))) -
         (r * K * bsm_vals.e_neg_rt * normsdist(bsm_vals.d2)) +
         (div * S * bsm_vals.e_neg_bt * normsdist(bsm_vals.d1));
}

inline double put_theta(
    double S, double K, double vol, double t, double r, double div) {
  const auto bsm_vals = calculateBSMIntermediates(S, K, vol, t, r, div);
  return -(S * bsm_vals.e_neg_bt * normpdf(bsm_vals.d1) * vol /
           (2.0 * std::sqrt(t))) +
         (r * K * bsm_vals.e_neg_rt * normsdist(-bsm_vals.d2)) -
         (div * S * bsm_vals.e_neg_bt * normsdist(-bsm_vals.d1));
}

// Second-order Greeks. Like the functions above, these take t as the time to
// expiry, but unlike vega above they are per unit of vol (not per vol point),
// and charm and colour are rates of change per year of calendar time, as the
// option ages (so t shrinks).

// Vanna: the change in delta as vol changes, d(delta)/d(vol), which is also
// d(vega)/d(spot). The same for calls and puts.
inline double vanna(
    double S, double K, double vol, double t, double r, double div) {
  const auto bsm_vals = calculateBSMIntermediates(S, K, vol, t, r, div);
  return -bsm_vals.e_neg_bt * normpdf(bsm_vals.d1) * bsm_vals.d2 / vol;
}

// Volga (or vomma): the change in vega as vol changes, d(vega)/d(vol). The
// same for calls and puts.
inline double volga(
    double S, double K, double vol, double t, double r, double div) {
  const auto bsm_vals = calculateBSMIntermediates(S, K, vol, t, r, div);
  return S * bsm_vals.e_neg_bt * normpdf(bsm_vals.d1) * std::sqrt(t) *
         bsm_vals.d1 * bsm_vals.d2 / vol;
}

// The part of charm common to calls and puts.
inline double charmCommon(const BSMIntermediates& bsm_vals,
                          double vol,
                          double t,
                          double r,
                          double div) {
  const double nu = vol * std::sqrt(t);
  return -bsm_vals.e_neg_bt * normpdf(bsm_vals.d1) *
         (2 * (r - div) * t - bsm_vals.d2 * nu) / (2 * t * nu);
}

// Charm: the change in delta as time passes, d(delta)/d(calendar time).
inline double call_charm(
    double S, double K, double vol, double t, double r, double div) {
  const auto bsm_vals = calculateBSMIntermediates(S, K, vol, t, r, div);
  return div * bsm_vals.e_neg_bt * normsdist(bsm_vals.d1) +
         charmCommon(bsm_vals, vol, t, r, div);
}

inline double put_charm(
    double S, double K, double vol, double t, double r, double div) {
  const auto bsm_vals = calculateBSMIntermediates(S, K, vol, t, r, div);
  return -div * bsm_vals.e_neg_bt * normsdist(-bsm_vals.d1) +
         charmCommon(bsm_vals, vol, t, r, div);
}

// Colour: the change in gamma as time passes, d(gamma)/d(calendar time). The
// same for calls and puts. Near expiry it is large and positive at the money,
// where gamma spikes, and negative away from it.
inline double colour(
    double S, double K, double vol, double t, double r, double div) {
  const auto bsm_vals = calculateBSMIntermediates(S, K, vol, t, r, div);
  const double nu = vol * std::sqrt(t);
  return bsm_vals.e_neg_bt * normpdf(bsm_vals.d1) / (2 * S * t * nu) *
         (2 * div * t + 1 +
          (2 * (r - div) * t - bsm_vals.d2 * nu) * bsm_vals.d1 / nu);
}

}  // namespace smileexplorer

#endif  // SMILE_EXPLORER_DERIVATIVES_BSM_