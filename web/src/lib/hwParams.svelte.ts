// Reactive parameters for the Hull-White essay. The defaults are the example
// in Hull, "Options, Futures, and Other Derivatives", section 32.6.

import {engine, type HullWhiteTreeResult, type HullWhiteTreeSpec} from './engine';

// Maturities (in years) of the zero rates that define the market curve.
export const HW_PILLARS = [0.5, 1, 1.5, 2, 2.5, 3];

export const hwParams = $state({
  meanReversion: 0.1,
  sigma: 0.01,
  stepsPerYear: 1,
  horizonYears: 3,
  // Continuously-compounded zero rates at each pillar.
  rates: [0.0343, 0.03824, 0.04183, 0.04512, 0.04812, 0.05086],
  bondFace: 100,
});

export type HwParams = typeof hwParams;

export function hwSpecFor(p: HwParams): HullWhiteTreeSpec {
  return {
    meanReversion: p.meanReversion,
    sigma: p.sigma,
    dtYears: 1 / p.stepsPerYear,
    horizonYears: p.horizonYears,
    curve: {maturities: HW_PILLARS, rates: [...p.rates]},
    bondFace: p.bondFace,
  };
}

export type TreeOutcome =
  | {tree: HullWhiteTreeResult; error?: undefined}
  | {tree?: undefined; error: string};

export function tryBuildTree(spec: HullWhiteTreeSpec): TreeOutcome {
  try {
    return {tree: engine.buildHullWhiteTree(spec)};
  } catch (e) {
    return {error: e instanceof Error ? e.message : String(e)};
  }
}
