#ifndef SMILEEXPLORER_VOLATILITY_SSVI_H_
#define SMILEEXPLORER_VOLATILITY_SSVI_H_

#include <algorithm>
#include <cmath>
#include <numbers>
#include <tuple>
#include <utility>
#include <vector>

namespace smileexplorer {

// A surface of implied volatility in the SSVI ("surface SVI") form of
// Gatheral and Jacquier, "Arbitrage-free SVI volatility surfaces" (2014).
// In terms of log-moneyness k = ln(K / F_T) and the at-the-money total
// implied variance theta(T) = sigma_ATM(T)^2 T, the total implied variance is
//
//   w(k, T) = theta / 2 (1 + rho phi k + sqrt((phi k + rho)^2 + 1 - rho^2))
//
// with phi(theta) = eta / theta^gamma. rho sets the skew, eta the curvature,
// and gamma how quickly the smile flattens with expiry.
//
// theta(T) is linear between the quoted expiries (and from 0 at T = 0), and
// continues the last segment's slope beyond the last expiry: so with no smile
// (eta -> 0), the forward variance is constant between expiries, as in
// PiecewiseTermStructureVol.
class SsviSurface {
 public:
  // Expects matching, non-empty vectors, with strictly increasing positive
  // maturities and positive total variances.
  SsviSurface(double rho,
              double eta,
              double gamma,
              std::vector<double> maturities,
              std::vector<double> atm_total_variances)
      : rho_(rho),
        eta_(eta),
        gamma_(gamma),
        maturities_(std::move(maturities)),
        thetas_(std::move(atm_total_variances)) {}

  double rho() const { return rho_; }
  double eta() const { return eta_; }
  double gamma() const { return gamma_; }
  const std::vector<double>& maturities() const { return maturities_; }
  const std::vector<double>& atmTotalVariances() const { return thetas_; }

  // The at-the-money total variance at T, and its rate of change (the forward
  // at-the-money variance), taken from the segment starting at or before T.
  double theta(double t) const {
    const auto [t0, theta0, slope] = segment(t);
    return theta0 + slope * (t - t0);
  }
  double thetaSlope(double t) const { return std::get<2>(segment(t)); }

  double phi(double theta) const { return eta_ / std::pow(theta, gamma_); }

  double totalVariance(double k, double t) const {
    const double th = theta(t);
    const double ph = phi(th);
    return th / 2 * (1 + rho_ * ph * k + root(ph * k));
  }

  double impliedVol(double k, double t) const {
    return std::sqrt(totalVariance(k, t) / t);
  }

  // Derivatives of w in k (at fixed T) and in T (at fixed k).
  double dwdk(double k, double t) const {
    const double th = theta(t);
    const double ph = phi(th);
    return th / 2 * ph * (rho_ + (ph * k + rho_) / root(ph * k));
  }
  double d2wdk2(double k, double t) const {
    const double th = theta(t);
    const double ph = phi(th);
    return th / 2 * ph * ph * (1 - rho_ * rho_) / std::pow(root(ph * k), 3);
  }
  double dwdt(double k, double t) const {
    const double th = theta(t);
    const double ph = phi(th);
    const double z = ph * k + rho_;
    // w = theta / 2 g(phi(theta) k), with dphi / dtheta = -gamma phi / theta.
    const double dw_dtheta =
        (1 + rho_ * ph * k + root(ph * k)) / 2 +
        th / 2 * (rho_ * k + k * z / root(ph * k)) * (-gamma_ * ph / th);
    return dw_dtheta * thetaSlope(t);
  }

  // Gatheral and Jacquier's sufficient conditions for no butterfly arbitrage
  // (theorem 4.2), at each quoted expiry: theta phi (1 + |rho|) <= 4 and
  // theta phi^2 (1 + |rho|) <= 4.
  bool butterflyOk() const {
    for (double th : thetas_) {
      const double ph = phi(th);
      if (th * ph * (1 + std::abs(rho_)) > 4 ||
          th * ph * ph * (1 + std::abs(rho_)) > 4) {
        return false;
      }
    }
    return true;
  }

  // No calendar arbitrage: theta non-decreasing in T. (With
  // phi = eta theta^-gamma and 0 < gamma < 1, theta phi rises with theta and
  // its other condition always holds.)
  bool calendarOk() const {
    for (size_t i = 1; i < thetas_.size(); ++i) {
      if (thetas_[i] < thetas_[i - 1]) return false;
    }
    return gamma_ > 0 && gamma_ < 1;
  }

  // The risk-neutral density of the asset price at T, from the second
  // derivative of call prices in strike (Breeden-Litzenberger), written in
  // terms of w: p(K) = g(k) n(d_-) / (K sqrt(w)), where
  //   g(k) = (1 - k w' / (2 w))^2 - w'^2 / 4 (1 / w + 1 / 4) + w'' / 2.
  // Negative where the surface admits butterfly arbitrage.
  double density(double strike, double forward, double t) const {
    const double k = std::log(strike / forward);
    const double w = totalVariance(k, t);
    const double w1 = dwdk(k, t);
    const double w2 = d2wdk2(k, t);
    const double g = std::pow(1 - k * w1 / (2 * w), 2) -
                     w1 * w1 / 4 * (1 / w + 0.25) + w2 / 2;
    const double d_minus = -k / std::sqrt(w) - std::sqrt(w) / 2;
    const double n =
        std::exp(-d_minus * d_minus / 2) / std::sqrt(2 * std::numbers::pi);
    return g * n / (strike * std::sqrt(w));
  }

 private:
  double rho_;
  double eta_;
  double gamma_;
  std::vector<double> maturities_;
  std::vector<double> thetas_;

  double root(double phi_k) const {
    const double z = phi_k + rho_;
    return std::sqrt(z * z + 1 - rho_ * rho_);
  }

  // The segment of theta(T) used at t: its start, theta there, and slope.
  std::tuple<double, double, double> segment(double t) const {
    double t0 = 0.0;
    double theta0 = 0.0;
    size_t i = 0;
    while (i < maturities_.size() && maturities_[i] <= t) {
      t0 = maturities_[i];
      theta0 = thetas_[i];
      ++i;
    }
    if (i == maturities_.size()) {
      // Beyond the last expiry: continue the last segment.
      if (maturities_.size() == 1) {
        return {t0, theta0, theta0 / t0};
      }
      const size_t n = maturities_.size();
      return {t0,
              theta0,
              (thetas_[n - 1] - thetas_[n - 2]) /
                  (maturities_[n - 1] - maturities_[n - 2])};
    }
    return {t0, theta0, (thetas_[i] - theta0) / (maturities_[i] - t0)};
  }
};

}  // namespace smileexplorer

#endif  // SMILEEXPLORER_VOLATILITY_SSVI_H_
