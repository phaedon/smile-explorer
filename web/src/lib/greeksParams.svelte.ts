// Reactive parameters for the Greeks and hedging essay.

import {engine, type VanillaPricingRequest} from './engine';

export const greeksParams = $state({
  spot: 100,
  strike: 100,
  vol: 0.2,
  rate: 0.03,
  expiryYears: 0.5,
  stepsPerYear: 50,
  isCall: true,
  isAmerican: false,

  // Hedging.
  realisedVol: 0.2,
  rebalances: 25,
});

export type GreeksParams = typeof greeksParams;

export function greeksRequestFor(p: GreeksParams): VanillaPricingRequest {
  return {
    tree: {
      model: engine.TreeModel.CoxRossRubinstein,
      spot: p.spot,
      vol: {model: engine.VolModel.Flat, vol: p.vol, termMaturities: [], termVols: [], smileRange: 0, smileStretch: 0},
      rate: p.rate,
      drift: 0,
      dtYears: 1 / p.stepsPerYear,
    },
    option: {strike: p.strike, expiryYears: p.expiryYears, isCall: p.isCall, isAmerican: p.isAmerican},
  };
}
