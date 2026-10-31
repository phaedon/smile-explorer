#ifndef SMILEEXPLORER_API_GREEKS_API_H_
#define SMILEEXPLORER_API_GREEKS_API_H_

#include <cstdint>
#include <vector>

#include "absl/status/statusor.h"
#include "api/pricing_api.h"

// The runtime API for Greeks and hedging: sensitivities read off a binomial
// tree, closed-form Black-Scholes Greeks (including second-order ones) over a
// grid, and a simulation of discrete delta hedging.
//
// Conventions: theta, charm and colour are per year of calendar time (as the
// option ages); vega and volga are per unit of vol (so a 1-point move in vol,
// 0.01, changes the price by vega / 100); rho is per unit of rate.

namespace smileexplorer::api {

struct Greeks {
  double delta = 0.0;
  double gamma = 0.0;
  double theta = 0.0;
  double vega = 0.0;
  double rho = 0.0;
};

struct GreeksResult {
  double price = 0.0;
  // From the tree: delta and gamma from the nodes at the first two steps,
  // theta from the middle node two steps in, and vega and rho by repricing
  // with the vol and rate bumped up and down (on the same timegrid).
  Greeks tree;
  // Closed form (Black-Scholes). NaN for American options.
  Greeks closed_form;
  double vanna = 0.0;
  double volga = 0.0;
  double charm = 0.0;
  double colour = 0.0;
  // The bump sizes used for the tree's vega and rho.
  double vol_bump = 0.0;
  double rate_bump = 0.0;
};

// Requires a Cox-Ross-Rubinstein or Jarrow-Rudd tree with flat vol and at
// least two steps.
absl::StatusOr<GreeksResult> computeGreeks(const VanillaPricingRequest& request,
                                           double vol_bump = 0.01,
                                           double rate_bump = 0.0001);

enum class Greek {
  kPrice,
  kDelta,
  kGamma,
  kTheta,
  kVega,
  kVanna,
  kVolga,
  kCharm,
  kColour,
};

struct GreekSurfaceRequest {
  Greek greek = Greek::kGamma;
  double strike = 100.0;
  double vol = 0.2;
  double rate = 0.0;
  bool is_call = true;
  std::vector<double> spots;
  // Times to expiry, in years (positive).
  std::vector<double> expiries;
};

// The closed-form Greek at every (expiry, spot) pair, row by row: the value
// for expiries[i] and spots[j] is at index i * spots.size() + j.
absl::StatusOr<std::vector<double>> greekSurface(
    const GreekSurfaceRequest& request);

struct DeltaHedgeRequest {
  double spot = 100.0;
  double strike = 100.0;
  double expiry_years = 0.25;
  bool is_call = true;
  // The vol the option is sold at, and hedged with.
  double implied_vol = 0.2;
  // The vol the spot actually realises.
  double realised_vol = 0.2;
  // Risk-free rate; also the spot's drift.
  double rate = 0.0;
  // Number of hedge rebalances over the option's life.
  int rebalances = 50;
  int paths = 2000;
  uint64_t seed = 1;
};

struct DeltaHedgeResult {
  // Final P&L of each path, at expiry, for the seller of one option who
  // delta-hedges it.
  std::vector<double> pnl;
  double mean = 0.0;
  double stddev = 0.0;
  // The option's price at the implied vol.
  double premium = 0.0;

  // The first path in detail, per rebalance time (rebalances + 1 of them):
  std::vector<double> times;
  std::vector<double> spots;
  // Shares held after rebalancing (NaN at expiry).
  std::vector<double> deltas;
  // Cumulative P&L, marking the option at its model value.
  std::vector<double> cumulative_pnl;
  // Cumulative theta earned (-theta * dt) and gamma paid (-gamma * dS^2 / 2)
  // by the short option: together they account for most of the P&L.
  std::vector<double> cumulative_theta;
  std::vector<double> cumulative_gamma;
};

absl::StatusOr<DeltaHedgeResult> simulateDeltaHedge(
    const DeltaHedgeRequest& request);

}  // namespace smileexplorer::api

#endif  // SMILEEXPLORER_API_GREEKS_API_H_
