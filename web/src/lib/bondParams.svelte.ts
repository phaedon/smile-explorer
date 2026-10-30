// Reactive parameters for the bond options essay. The defaults are Hull,
// "Options, Futures, and Other Derivatives", example 32.1: a 3-year put,
// struck at 63, on a 9-year zero-coupon bond with face value 100.

import type {BondOptionRequest} from './engine';

// Hull's zero curve (continuously compounded), at maturities given in days.
export const HULL_CURVE = {
  maturities: [3, 31, 62, 94, 185, 367, 731, 1096, 1461, 1826, 2194, 2558, 2922, 3287, 3653].map(
    (days) => days / 365,
  ),
  rates: [
    0.0501772, 0.0498284, 0.0497234, 0.0496157, 0.0499058, 0.0509389, 0.0579733, 0.0630595,
    0.0673464, 0.0694816, 0.0708807, 0.0727527, 0.0730852, 0.073979, 0.0749015,
  ],
};

export const BOND_MATURITY = 9;

export const bondParams = $state({
  meanReversion: 0.1,
  sigma: 0.01,
  // The tree's timestep is expiryYears / stepsToExpiry.
  stepsToExpiry: 3,
  expiryYears: 3,
  strike: 63,
  isCall: false,
  isAmerican: false,
});

export type BondParams = typeof bondParams;

export function bondRequestFor(
  p: BondParams,
  overrides: {stepsToExpiry?: number; meanReversion?: number; sigma?: number; isAmerican?: boolean} = {},
): BondOptionRequest {
  return {
    tree: {
      meanReversion: overrides.meanReversion ?? p.meanReversion,
      sigma: overrides.sigma ?? p.sigma,
      dtYears: p.expiryYears / (overrides.stepsToExpiry ?? p.stepsToExpiry),
      horizonYears: BOND_MATURITY,
      curve: {maturities: HULL_CURVE.maturities, rates: HULL_CURVE.rates},
      bondFace: 100,
    },
    option: {
      strike: p.strike,
      expiryYears: p.expiryYears,
      isCall: p.isCall,
      isAmerican: overrides.isAmerican ?? p.isAmerican,
    },
  };
}
