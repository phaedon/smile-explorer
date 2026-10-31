#ifndef SMILEEXPLORER_VOLATILITY_H_
#define SMILEEXPLORER_VOLATILITY_H_

#include <Eigen/Dense>
#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include "time/timegrid.h"

namespace smileexplorer {

// Returns sig_t_T
inline double forwardVol(
    double t0, double t, double T, double sig_0_t, double sig_0_T) {
  return std::sqrt((T * std::pow(sig_0_T, 2) - t * std::pow(sig_0_t, 2)) /
                   (T - t));
}

enum class VolSurfaceFnType {
  kBlackScholesMerton,
  kTermStructure,
  kTimeInvariantSkewSmile,
  // Local vol as a function of the asset price and time, e.g. DupireLocalVol.
  kTimeVaryingSkewSmile
};

struct FlatVol {
  static constexpr VolSurfaceFnType type =
      VolSurfaceFnType::kBlackScholesMerton;
  FlatVol(double vol) : vol_(vol) {}

  // We use variadic arguments here to allow the caller to pass any optional
  // args (such as time or price) -- but we ignore them in this functor since we
  // just return a flat vol.
  template <typename... Args>
  double operator()(Args... args) const {
    return vol_;
  }

  double vol_;
};

// A term structure of volatility, specified by implied (spot) volatilities at
// increasing maturities, e.g. as quoted for at-the-money options.
//
// Evaluating it at time t returns the instantaneous (forward) volatility,
// which is constant between consecutive maturities, flat at the first implied
// vol before the first maturity, and flat at the last forward vol beyond the
// last maturity.
struct PiecewiseTermStructureVol {
  static constexpr VolSurfaceFnType type = VolSurfaceFnType::kTermStructure;

  // Expects matching, non-empty vectors with strictly increasing maturities
  // and non-decreasing total variance (maturity * vol^2), so that every
  // forward variance is non-negative.
  PiecewiseTermStructureVol(std::vector<double> maturities,
                            std::vector<double> implied_vols)
      : maturities_(std::move(maturities)),
        implied_vols_(std::move(implied_vols)) {}

  double operator()(double t) const {
    if (t <= maturities_[0]) return implied_vols_[0];
    for (size_t k = 1; k < maturities_.size(); ++k) {
      if (t <= maturities_[k]) return forwardVolBefore(k);
    }
    return maturities_.size() > 1 ? forwardVolBefore(maturities_.size() - 1)
                                  : implied_vols_[0];
  }

  // The implied (spot) volatility for an expiry at time t: the root mean
  // square of the forward volatility from 0 to t.
  double impliedVol(double t) const {
    if (t <= maturities_[0]) return implied_vols_[0];
    double total_variance = 0.0;
    double prev = 0.0;
    for (size_t k = 0; k < maturities_.size() && prev < t; ++k) {
      const double end = std::min(t, maturities_[k]);
      const double vol = (*this)(end);
      total_variance += vol * vol * (end - prev);
      prev = end;
    }
    if (t > prev) {
      const double vol = (*this)(t);
      total_variance += vol * vol * (t - prev);
    }
    return std::sqrt(total_variance / t);
  }

  const std::vector<double>& maturities() const { return maturities_; }
  const std::vector<double>& impliedVols() const { return implied_vols_; }

 private:
  // The forward vol between maturities k-1 and k.
  double forwardVolBefore(size_t k) const {
    return forwardVol(0,
                      maturities_[k - 1],
                      maturities_[k],
                      implied_vols_[k - 1],
                      implied_vols_[k]);
  }

  std::vector<double> maturities_;
  std::vector<double> implied_vols_;
};

// A time-invariant local volatility smile (in fact a skew), as a function of
// the asset price S:
//
//   sigma(S) = base + range / (1 + exp(stretch * (S - center)))
//
// Volatility rises towards base + range as S falls well below `center`, and
// falls towards `base` as S rises well above it; `stretch` sets how quickly.
struct SigmoidSmileVol {
  static constexpr VolSurfaceFnType type =
      VolSurfaceFnType::kTimeInvariantSkewSmile;

  double operator()(double s) const {
    return base + range / (1 + std::exp(stretch * (s - center)));
  }

  double base;
  double range;
  double stretch;
  double center;
};

template <typename VolSurfaceT>
class Volatility {
 public:
  Volatility(VolSurfaceT vol_surface) : vol_surface_(vol_surface) {}

  template <typename... Args>
  double get(Args... args) const {
    return vol_surface_(std::forward<Args>(args)...);
  }

  Timegrid generateTimegrid(double t_final, double initial_timestep) const;

  static constexpr VolSurfaceFnType type = VolSurfaceT::type;

 private:
  VolSurfaceT vol_surface_;
};

template <typename VolSurfaceT>
Timegrid Volatility<VolSurfaceT>::generateTimegrid(
    double t_final, double initial_timestep) const {
  if constexpr (VolSurfaceT::type == VolSurfaceFnType::kBlackScholesMerton ||
                VolSurfaceT::type ==
                    VolSurfaceFnType::kTimeInvariantSkewSmile ||
                VolSurfaceT::type == VolSurfaceFnType::kTimeVaryingSkewSmile) {
    const int timegrid_size = std::ceil(t_final / initial_timestep) + 1;
    Timegrid grid(timegrid_size);
    for (int i = 0; i < timegrid_size; ++i) {
      grid.set(i, i * initial_timestep);
    }
    return grid;
  } else if constexpr (VolSurfaceT::type == VolSurfaceFnType::kTermStructure) {
    Timegrid grid;

    double dt_curr = initial_timestep;
    double accumul_time = 0;

    // The loop condition allows for accrued floating-point error.
    while (accumul_time < t_final - grid.accruedErrorEstimate()) {
      double sig_curr = get(accumul_time);
      accumul_time += dt_curr;
      double sig_next = get(accumul_time);
      double dt_next = sig_curr * sig_curr * dt_curr / (sig_next * sig_next);
      grid.append(accumul_time);
      dt_curr = dt_next;
    }

    return grid;
  }
}

}  // namespace smileexplorer

#endif  // SMILEEXPLORER_VOLATILITY_H_