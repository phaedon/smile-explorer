#ifndef MARKETS_EXPLORER_PROPAGATOR_FACTORIES_
#define MARKETS_EXPLORER_PROPAGATOR_FACTORIES_

#include "explorer_params.h"
#include "markets/propagators.h"

namespace markets {

template <typename FwdPropT>
FwdPropT createDefaultPropagator(const ExplorerParams& params);

template <>
inline CRRPropagator createDefaultPropagator<CRRPropagator>(
    const ExplorerParams& params) {
  return CRRPropagator(params.spot_price);
}

template <>
inline JarrowRuddPropagator createDefaultPropagator<JarrowRuddPropagator>(
    const ExplorerParams& params) {
  return JarrowRuddPropagator(params.jarrowrudd_expected_drift,
                              params.spot_price);
}

template <>
inline LocalVolatilityPropagator
createDefaultPropagator<LocalVolatilityPropagator>(
    const ExplorerParams& params) {
  return LocalVolatilityPropagator(*params.curve, params.spot_price);
}

}  // namespace markets

#endif  // MARKETS_EXPLORER_PROPAGATOR_FACTORIES_