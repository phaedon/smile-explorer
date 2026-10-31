#include "api/short_rate_api.h"

#include <utility>

#include "api/hull_white_tree.h"
#include "derivatives/hull_white_bond_option.h"

namespace smileexplorer::api {

absl::StatusOr<HullWhiteTreeResult> buildHullWhiteTree(
    const HullWhiteTreeSpec& spec) {
  absl::StatusOr<FittedTree> fitted = fitTree(spec);
  if (!fitted.ok()) {
    return fitted.status();
  }
  return std::move(fitted->result);
}

absl::StatusOr<BondOptionResult> priceBondOption(
    const BondOptionRequest& request) {
  absl::StatusOr<FittedTree> fitted = fitTree(request.tree);
  if (!fitted.ok()) {
    return fitted.status();
  }
  absl::StatusOr<BondOptionResult> result =
      optionOnBond(*fitted, request.option, request.tree.dt_years);
  if (!result.ok()) {
    return result.status();
  }
  const HullWhiteTreeSpec& spec = request.tree;
  const auto& times = fitted->result.times;
  result->analytic_price = hullWhiteZeroBondOption(*fitted->market_curve,
                                                   spec.mean_reversion,
                                                   spec.sigma,
                                                   times[result->expiry_index],
                                                   times.back(),
                                                   spec.bond_face,
                                                   request.option.strike,
                                                   request.option.is_call);
  result->tree = std::move(fitted->result);
  return result;
}

}  // namespace smileexplorer::api
