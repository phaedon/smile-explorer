// Reactive parameters for the currency options essay: EUR-USD, quoted as the
// price of one euro in dollars, so USD is the domestic currency and EUR the
// foreign one.

import type {FxVanillaPricingRequest} from './engine';

// Maturities (in years) of the zero rates that define each curve.
export const PILLARS = [0.25, 1, 2, 5];

export const fxParams = $state({
  spot: 1.1,
  vol: 0.08,
  strike: 1.1,
  expiryYears: 1,
  stepsPerYear: 12,
  isCall: true,
  isAmerican: false,
  // Continuously-compounded zero rates at each pillar: roughly an inverted
  // dollar curve and an upward-sloping euro curve.
  domesticRates: [0.0425, 0.04, 0.0375, 0.0375],
  foreignRates: [0.02, 0.021, 0.0225, 0.025],
});

export type FxParams = typeof fxParams;

export function fxRequestFor(
  p: FxParams,
  overrides: {stepsPerYear?: number; isCall?: boolean; isAmerican?: boolean} = {},
): FxVanillaPricingRequest {
  return {
    tree: {
      spot: p.spot,
      vol: p.vol,
      domestic: {maturities: PILLARS, rates: [...p.domesticRates]},
      foreign: {maturities: PILLARS, rates: [...p.foreignRates]},
      dtYears: 1 / (overrides.stepsPerYear ?? p.stepsPerYear),
    },
    option: {
      strike: p.strike,
      expiryYears: p.expiryYears,
      isCall: overrides.isCall ?? p.isCall,
      isAmerican: overrides.isAmerican ?? p.isAmerican,
    },
  };
}
