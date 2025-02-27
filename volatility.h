#ifndef MARKETS_VOLATILITY_H_
#define MARKETS_VOLATILITY_H_

#include <Eigen/Dense>
#include <utility>

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
  double operator()() const { return vol_; }
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

  Eigen::VectorXd getTimegrid(double t_final, double initial_timestep) const;

 private:
  VolSurfaceT vol_surface_;
};

template <typename VolSurfaceT>
Eigen::VectorXd Volatility<VolSurfaceT>::getTimegrid(
    double t_final, double initial_timestep) const {
  if constexpr (VolSurfaceT::type == VolSurfaceFnType::kBlackScholesMerton ||
                VolSurfaceT::type ==
                    VolSurfaceFnType::kTimeInvariantSkewSmile) {
    const int timegrid_size = std::ceil(t_final / initial_timestep) + 1;
    Eigen::VectorXd timegrid(timegrid_size);
    for (int i = 0; i < timegrid_size; ++i) {
      timegrid[i] = i * initial_timestep;
    }
    return timegrid;
  } else if constexpr (VolSurfaceT::type == VolSurfaceFnType::kTermStructure) {
    Eigen::VectorXd timegrid(1);
    timegrid.setZero();

    double dt_curr = initial_timestep;
    double accumul_time = 0;

    // The loop condition allows for accrued floating-point error.
    while (accumul_time <
           t_final - std::numeric_limits<double>::epsilon() * timegrid.size()) {
      double sig_curr = get(accumul_time);
      accumul_time += dt_curr;
      double sig_next = get(accumul_time);
      double dt_next = sig_curr * sig_curr * dt_curr / (sig_next * sig_next);
      timegrid.conservativeResize(timegrid.size() + 1);
      timegrid[timegrid.size() - 1] = accumul_time;
      dt_curr = dt_next;
    }

    return timegrid;
  } else {
    static_assert(false, "Unsupported VolSurfaceFnType");
  }
}

}  // namespace markets

#endif  // MARKETS_VOLATILITY_H_