// The essay's shared, reactive model parameters. Every figure and code snippet
// reads from (and the draggable numbers write to) this one object.

import {engine, flatVol, type VanillaPricingRequest} from './engine';

export const params = $state({
  spot: 100,
  vol: 0.2,
  rate: 0.05,
  stepsPerYear: 8,
  expiryYears: 1,
  strike: 100,
  isCall: true,
  isAmerican: false,
});

export type Params = typeof params;

export function requestFor(
  p: Params,
  stepsPerYear = p.stepsPerYear,
): VanillaPricingRequest {
  return {
    tree: {
      model: engine.TreeModel.CoxRossRubinstein,
      spot: p.spot,
      vol: flatVol(p.vol),
      rate: p.rate,
      drift: 0,
      dtYears: 1 / stepsPerYear,
    },
    option: {
      strike: p.strike,
      expiryYears: p.expiryYears,
      isCall: p.isCall,
      isAmerican: p.isAmerican,
    },
  };
}
