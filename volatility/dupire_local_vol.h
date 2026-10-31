#ifndef SMILEEXPLORER_VOLATILITY_DUPIRE_LOCAL_VOL_H_
#define SMILEEXPLORER_VOLATILITY_DUPIRE_LOCAL_VOL_H_

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include "volatility/ssvi.h"
#include "volatility/volatility.h"

namespace smileexplorer {

// The local volatility sigma(S, t) implied by a surface of implied vols, by
// Dupire's formula. In terms of the total implied variance w(k, T) at
// log-moneyness k = ln(K / F_T) (Gatheral, "The Volatility Surface", 1.10):
//
//   sigma^2 = (dw/dT) / (1 - (k / w) dw/dk
//                          + 1/4 (-1/4 - 1/w + k^2 / w^2) (dw/dk)^2
//                          + 1/2 d2w/dk2)
//
// evaluated at K = S and T = t. The forward is S0 exp((r - q) t).
struct DupireLocalVol {
  static constexpr VolSurfaceFnType type =
      VolSurfaceFnType::kTimeVaryingSkewSmile;

  // Local vol is kept in [kMinVol, kMaxVol]: outside it (or where the formula
  // breaks down, in the far wings of a poor fit), it is clamped.
  static constexpr double kMinVol = 0.01;
  static constexpr double kMaxVol = 3.0;
  // As t -> 0 the formula needs w -> 0, so it is evaluated no earlier than
  // this (about nine hours).
  static constexpr double kMinTime = 1e-3;

  DupireLocalVol(SsviSurface surface,
                 double spot,
                 double rate,
                 double dividend_yield = 0.0)
      : surface_(std::move(surface)),
        spot_(spot),
        rate_(rate),
        dividend_yield_(dividend_yield) {}

  // The local variance before clamping: NaN where the formula breaks down.
  double rawLocalVariance(double s, double t) const {
    t = std::max(t, kMinTime);
    const double forward = spot_ * std::exp((rate_ - dividend_yield_) * t);
    const double k = std::log(s / forward);
    const double w = surface_.totalVariance(k, t);
    const double w1 = surface_.dwdk(k, t);
    const double w2 = surface_.d2wdk2(k, t);
    const double denominator =
        1 - k / w * w1 + 0.25 * (-0.25 - 1 / w + k * k / (w * w)) * w1 * w1 +
        0.5 * w2;
    const double numerator = surface_.dwdt(k, t);
    if (!(denominator > 0) || !(numerator >= 0)) {
      return std::numeric_limits<double>::quiet_NaN();
    }
    return numerator / denominator;
  }

  bool isClamped(double s, double t) const {
    const double variance = rawLocalVariance(s, t);
    return !(variance >= kMinVol * kMinVol && variance <= kMaxVol * kMaxVol);
  }

  double operator()(double s, double t) const {
    const double variance = rawLocalVariance(s, t);
    if (std::isnan(variance)) return kMinVol;
    return std::clamp(std::sqrt(variance), kMinVol, kMaxVol);
  }

  const SsviSurface& surface() const { return surface_; }

 private:
  SsviSurface surface_;
  double spot_;
  double rate_;
  double dividend_yield_;
};

}  // namespace smileexplorer

#endif  // SMILEEXPLORER_VOLATILITY_DUPIRE_LOCAL_VOL_H_
