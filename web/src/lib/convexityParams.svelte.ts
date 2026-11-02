// Reactive parameters for the convexity essay: SOFR futures against a
// collateralized FRA on the same quarter.

import type {ConvexityHedgeSpec} from './engine/engine_lib.js';
import {RATE_CURVE} from './swaptionParams.svelte';

// The overnight (SOFR) curve: the swaptions essay's illustrative curve, so
// that its calibrated a and sigma carry over. Not market data.
export const SOFR_CURVE = {maturities: RATE_CURVE.maturities, rates: RATE_CURVE.rates};

// One CME 3-month SOFR future: $2,500 per index point, so $25 per basis point
// on a $1mm quarter.
export const NOTIONAL = 1e6;
export const PERIOD = 0.25;
export const STEPS_PER_YEAR = 252;

export const HEDGE_STYLES = [
  {id: 0, name: 'tailed, daily', short: 'tailed'},
  {id: 1, name: 'untailed, daily', short: 'untailed'},
  {id: 2, name: 'tailed once, never rebalanced', short: 'static'},
  {id: 3, name: 'no hedge', short: 'none'},
];

export const convexityParams = $state({
  // The swaptions essay's fit.
  meanReversion: 0.05,
  sigma: 0.0105,
  realizedSigma: 0.0105,
  // The start of the contract's quarter, in years.
  start: 2,
  // Section 2: which simulated path, and whether its margin carries PAI.
  pathNumber: 1,
  pai: true,
  // Section 3.
  treeStepsPerYear: 4,
  // Section 5.
  hedgeStyle: 0,
  fixedAtFuturesRate: false,
});

export type ConvexityParams = typeof convexityParams;

export function hedgeSpecFor(p: ConvexityParams, overrides: Partial<ConvexityHedgeSpec> = {}): ConvexityHedgeSpec {
  return {
    curve: {maturities: [...SOFR_CURVE.maturities], rates: [...SOFR_CURVE.rates]},
    meanReversion: p.meanReversion,
    impliedSigma: p.sigma,
    realizedSigma: p.realizedSigma,
    startYears: p.start,
    endYears: p.start + PERIOD,
    notional: NOTIONAL,
    fixedAtFuturesRate: p.fixedAtFuturesRate,
    hedgeStyle: p.hedgeStyle,
    stepsPerYear: STEPS_PER_YEAR,
    ...overrides,
  };
}
