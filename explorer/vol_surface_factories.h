#ifndef SMILEEXPLORER_EXPLORER_VOL_SURFACE_FACTORIES_
#define SMILEEXPLORER_EXPLORER_VOL_SURFACE_FACTORIES_

#include "explorer_params.h"
#include "volatility/volatility.h"

namespace smileexplorer {

struct ConstantVolSurface {
  static constexpr VolSurfaceFnType type =
      VolSurfaceFnType::kBlackScholesMerton;
  ConstantVolSurface(const ExplorerParams& params) : params_(params) {}

  // We use variadic arguments here to allow the caller to pass any optional
  // args (such as time or price) -- but we ignore them in this functor since we
  // just return a flat vol.
  template <typename... Args>
  double operator()(Args... args) const {
    return params_.flat_vol;
  }

  const ExplorerParams params_;
};

struct TermStructureVolSurface {
  static constexpr VolSurfaceFnType type = VolSurfaceFnType::kTermStructure;
  TermStructureVolSurface(const ExplorerParams& params) : params_(params) {}

  double operator()(double t) const {
    if (t <= 1) return params_.flat_vol;
    // The parameters 1.2 and 1.1 are hard-coded to generate a simple tree which
    // first becomes more dense (because of rising forward vol in year 2) and
    // then less dense (falling forward vol in year 3). The discrete jumps serve
    // to make it more visually obvious.
    if (t <= 2)
      return forwardVol(0, 1, 2, params_.flat_vol, params_.flat_vol * 1.2);
    return forwardVol(0, 2, 3, params_.flat_vol * 1.2, params_.flat_vol * 1.1);
  }

  const ExplorerParams params_;
};

struct SigmoidSmile {
  static constexpr VolSurfaceFnType type =
      VolSurfaceFnType::kTimeInvariantSkewSmile;
  SigmoidSmile(const ExplorerParams& params) : params_(params) {}

  double operator()(double s) const {
    return params_.flat_vol + params_.sigmoid_vol_range /
                                  (1 + std::exp(params_.sigmoid_vol_stretch *
                                                (s - params_.spot_price)));
  }

 private:
  const ExplorerParams params_;
};

template <class VolSurfaceFnType>
Volatility<VolSurfaceFnType> createExampleVolSurface(
    const ExplorerParams& params);

template <>
inline Volatility<ConstantVolSurface>
createExampleVolSurface<ConstantVolSurface>(const ExplorerParams& params) {
  return Volatility(ConstantVolSurface(params));
}

template <>
inline Volatility<SigmoidSmile> createExampleVolSurface<SigmoidSmile>(
    const ExplorerParams& params) {
  return Volatility(SigmoidSmile(params));
}

}  // namespace smileexplorer

#endif  // SMILEEXPLORER_EXPLORER_VOL_SURFACE_FACTORIES_