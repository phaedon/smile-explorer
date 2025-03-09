#ifndef MARKETS_VOLATILITY_H_
#define MARKETS_VOLATILITY_H_

#include <Eigen/Dense>
#include <utility>

#include "time/timegrid.h"

namespace markets {

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

  // Currently unimplemented.
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

template <typename VolSurfaceT>
class Volatility {
 public:
  Volatility(VolSurfaceT vol_surface) : vol_surface_(vol_surface) {}

  template <typename... Args>
  double get(Args... args) const {
    return vol_surface_(std::forward<Args>(args)...);
  }

  Timegrid generateTimegrid(double t_final, double initial_timestep) const;

 private:
  VolSurfaceT vol_surface_;
};

template <typename VolSurfaceT>
Timegrid Volatility<VolSurfaceT>::generateTimegrid(
    double t_final, double initial_timestep) const {
  if constexpr (VolSurfaceT::type == VolSurfaceFnType::kBlackScholesMerton ||
                VolSurfaceT::type ==
                    VolSurfaceFnType::kTimeInvariantSkewSmile) {
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

}  // namespace markets

#endif  // MARKETS_VOLATILITY_H_