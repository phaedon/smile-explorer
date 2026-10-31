#ifndef SMILEEXPLORER_API_HULL_WHITE_TREE_H_
#define SMILEEXPLORER_API_HULL_WHITE_TREE_H_

// Internal to //api: building a Hull-White tree for the API's requests, and
// pricing options on a bond held on it. Shared by short_rate_api.cpp and
// rates_options_api.cpp.

#include <memory>
#include <optional>
#include <vector>

#include "absl/status/statusor.h"
#include "api/short_rate_api.h"
#include "derivatives/interest_rate_derivative.h"
#include "instruments/swaps/fixed_cashflow_instrument.h"
#include "rates/short_rate_tree_curve.h"
#include "rates/zero_curve.h"

namespace smileexplorer::api {

// A Hull-White tree fitted to the market curve, with a bond priced on it.
// Held by pointer: the bond refers to the tree, and the tree to the curve.
struct FittedTree {
  std::unique_ptr<ZeroSpotCurve> market_curve;
  std::unique_ptr<ShortRateTreeCurve> tree_curve;
  std::unique_ptr<FixedCashflowInstrument> bond;
  // The tree's nodes, with the bond's value at each.
  HullWhiteTreeResult result;
};

// Validates the spec and fits the tree. The bond pays `cashflows`, which must
// fall on the tree's steps, or by default spec.bond_face at the end of the
// tree.
absl::StatusOr<FittedTree> fitTree(
    const HullWhiteTreeSpec& spec,
    const std::optional<std::vector<Cashflow>>& cashflows = std::nullopt);

// Prices an option on the fitted bond, filling everything in BondOptionResult
// except `tree` (left empty, for the caller to move the nodes into) and
// `analytic_price` (NaN).
//
// With `bermudan_dates`, the option can be exercised only on those dates (the
// first being option.expiry_years), its exercise values are set only there,
// and european_price is the option exercisable on the first date alone.
absl::StatusOr<BondOptionResult> optionOnBond(
    const FittedTree& fitted,
    const BondOptionSpec& option,
    double dt_years,
    const std::vector<ExerciseDate>* bermudan_dates = nullptr);

}  // namespace smileexplorer::api

#endif  // SMILEEXPLORER_API_HULL_WHITE_TREE_H_
