#ifndef SMILEEXPLORER_CALIBRATION_SMILE_CALIBRATION_H_
#define SMILEEXPLORER_CALIBRATION_SMILE_CALIBRATION_H_

#include <vector>

#include "absl/status/statusor.h"
#include "volatility/ssvi.h"

// Fits an SSVI surface (volatility/ssvi.h) to a grid of quoted implied vols.

namespace smileexplorer {

struct SmileQuotes {
  double spot = 100.0;
  // Flat, continuously compounded.
  double rate = 0.0;
  double dividend_yield = 0.0;
  // Strictly increasing, positive.
  std::vector<double> expiries;
  std::vector<double> strikes;
  // Implied vols, row by row: the quote for expiries[i] and strikes[j] is at
  // index i * strikes.size() + j. NaN where there is no quote.
  std::vector<double> vols;
};

struct SmileFit {
  double rho = 0.0;
  double eta = 0.0;
  double gamma = 0.0;
  // At-the-money total variance at each expiry, from the quotes
  // (interpolated at the forward), made non-decreasing if need be.
  std::vector<double> thetas;
  bool thetas_adjusted = false;
  // Per quote, as in SmileQuotes::vols (NaN where there was no quote).
  std::vector<double> fitted_vols;
  std::vector<double> residuals;
  double rms_error = 0.0;
  bool butterfly_ok = false;
  bool calendar_ok = false;
  int iterations = 0;

  SsviSurface surface(const std::vector<double>& expiries) const {
    return SsviSurface(rho, eta, gamma, expiries, thetas);
  }
};

absl::StatusOr<SmileFit> calibrateSsvi(const SmileQuotes& quotes);

}  // namespace smileexplorer

#endif  // SMILEEXPLORER_CALIBRATION_SMILE_CALIBRATION_H_
