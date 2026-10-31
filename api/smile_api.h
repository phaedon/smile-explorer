#ifndef SMILEEXPLORER_API_SMILE_API_H_
#define SMILEEXPLORER_API_SMILE_API_H_

#include <vector>

#include "absl/status/statusor.h"
#include "calibration/smile_calibration.h"

// The runtime API for the volatility smile: fit an SSVI surface to quoted
// implied vols, evaluate it and the Dupire local vol it implies on a grid,
// and check the fit by building the local-vol tree and repricing the quotes.
//
// Rates are flat and continuously compounded, with no dividends.

namespace smileexplorer::api {

absl::StatusOr<SmileFit> calibrateSmile(const SmileQuotes& quotes);

// A fitted SSVI surface: its parameters and at-the-money total variance at
// each expiry (as in SmileFit).
struct SsviSpec {
  double rho = 0.0;
  double eta = 0.0;
  double gamma = 0.0;
  std::vector<double> expiries;
  std::vector<double> thetas;
};

struct SmileGridRequest {
  double spot = 100.0;
  double rate = 0.0;
  SsviSpec ssvi;
  // Strikes (or asset prices, for local vol) and times, both positive.
  std::vector<double> strikes;
  std::vector<double> times;
};

struct SmileSurfaces {
  // Row by row: the value for times[i] and strikes[j] is at i * strikes.size()
  // + j. Implied vol at strike K and expiry T; local vol at asset price S and
  // time t.
  std::vector<double> implied_vols;
  std::vector<double> local_vols;
  // The number of grid points where local vol was clamped (the formula broke
  // down or gave a vol outside DupireLocalVol's band).
  int clamped = 0;
};

absl::StatusOr<SmileSurfaces> smileSurfaces(const SmileGridRequest& request);

// Builds one local-vol tree to the last of `times` (which must fall on its
// steps), prices a European option at every strike and time from its
// Arrow-Debreu prices (out of the money: puts below the forward, calls above),
// and converts each price back to an implied vol.
struct RepriceRequest {
  SmileGridRequest grid;
  int steps_per_year = 100;
};

struct RepriceResult {
  // Row by row, as in SmileSurfaces. NaN where the price couldn't be inverted
  // (too far out of the money for the tree to resolve).
  std::vector<double> tree_vols;
  std::vector<double> surface_vols;
  // tree_vols - surface_vols, and the root mean square over finite entries.
  std::vector<double> residuals;
  double rms_error = 0.0;
};

absl::StatusOr<RepriceResult> repriceSmile(const RepriceRequest& request);

// The risk-neutral density of the asset price at `time`, at each strike.
// Negative values mean butterfly arbitrage.
absl::StatusOr<std::vector<double>> impliedDensity(
    const SmileGridRequest& request);

}  // namespace smileexplorer::api

#endif  // SMILEEXPLORER_API_SMILE_API_H_
