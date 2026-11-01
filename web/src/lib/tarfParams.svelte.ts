// Reactive parameters for the TARF essay: EUR-USD on the currency essay's
// curves, with a customer who buys euros every month at a strike below the
// forward. Illustrative terms, not a quote.

import type {TarfSpec} from './engine';

// The currency essay's curves: zero rates at these maturities (in years).
const PILLARS = [0.25, 1, 2, 5];
const USD_RATES = [0.0425, 0.04, 0.0375, 0.0375];
const EUR_RATES = [0.02, 0.021, 0.0225, 0.025];

export const NOTIONAL = 1_000_000;
// Timesteps between fixings, for drawing; prices depend only on the fixings.
export const STEPS_PER_FIXING = 4;

export const tarfParams = $state({
  spot: 1.1,
  vol: 0.08,
  // Monthly fixings.
  fixings: 12,
  // Of gains, in dollars.
  target: 200_000,
  leverage: 2,
  strike: 1.064,
  // Whether the strike follows the zero-cost strike as the terms change.
  zeroCost: true,
});

export type TarfParams = typeof tarfParams;

export function tarfSpecFor(p: TarfParams, overrides: Partial<TarfSpec> = {}): TarfSpec {
  return {
    spot: p.spot,
    vol: p.vol,
    domestic: {maturities: PILLARS, rates: USD_RATES},
    foreign: {maturities: PILLARS, rates: EUR_RATES},
    notional: NOTIONAL,
    target: p.target,
    strike: p.strike,
    leverage: p.leverage,
    endYears: p.fixings / 12,
    fixingFrequency: 1 / 12,
    isLong: true,
    stepsPerFixing: STEPS_PER_FIXING,
    ...overrides,
  };
}
