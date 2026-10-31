// Reactive parameters for the swaptions and calibration essay.

import type {CapRequest, SwaptionRequest} from './engine/engine_lib.js';

// An illustrative zero curve (continuously compounded), slightly inverted at
// the front. Not market data.
export const RATE_CURVE = {
  maturities: [1, 2, 3, 5, 7, 10],
  rates: [0.036, 0.035, 0.0355, 0.037, 0.0385, 0.04],
};

// Co-terminal swaptions: each expires in `expiry` years into a swap ending in
// 10 years.
export const COTERMINAL = [1, 2, 3, 4, 5, 6, 7, 8, 9].map((expiry) => ({expiry, tenor: 10 - expiry}));

// Illustrative at-the-money normal vols for those swaptions, in basis points:
// made up, with a realistic shape (Hull-White with a = 0.05 and sigma = 1.05%,
// plus a little noise). Not market data.
export const DEFAULT_VOLS_BP = [86.3, 86.3, 86.3, 86.2, 86.3, 86.3, 86.5, 86.6, 86.8];

export const swaptionParams = $state({
  meanReversion: 0.05,
  sigma: 0.0105,
  expiryYears: 2,
  tenorYears: 5,
  stepsPerYear: 4,
  isPayer: true,
  quotesBp: [...DEFAULT_VOLS_BP],
});

export type SwaptionParams = typeof swaptionParams;

function treeSpec(p: SwaptionParams, stepsPerYear: number) {
  return {
    meanReversion: p.meanReversion,
    sigma: p.sigma,
    dtYears: 1 / stepsPerYear,
    horizonYears: 0,
    curve: {maturities: RATE_CURVE.maturities, rates: RATE_CURVE.rates},
    bondFace: 100,
  };
}

export function swaptionRequestFor(p: SwaptionParams, stepsPerYear = p.stepsPerYear): SwaptionRequest {
  return {
    tree: treeSpec(p, stepsPerYear),
    swaption: {
      expiryYears: p.expiryYears,
      tenorYears: p.tenorYears,
      fixedPaymentsPerYear: 1,
      atTheMoney: true,
      fixedRate: 0,
      notional: 100,
      isPayer: p.isPayer,
    },
  };
}

export function capRequestFor(p: SwaptionParams): CapRequest {
  return {
    tree: treeSpec(p, 12),
    cap: {maturityYears: 5, paymentsPerYear: 4, atTheMoney: true, strike: 0, notional: 100, isCap: true},
  };
}
