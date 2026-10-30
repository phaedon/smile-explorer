// Builders for the vol specs used by the volatility essay. Plain functions of
// plain numbers, so they work in the smile worker as well as the page.

import {engine, type VolSpec} from './engine';

export const TERM_MATURITIES = [1, 2, 3];

export function termSpec(p: {termVols: number[]}): VolSpec {
  return {
    model: engine.VolModel.TermStructure,
    vol: p.termVols[0],
    termMaturities: TERM_MATURITIES,
    termVols: [...p.termVols],
    smileRange: 0,
    smileStretch: 0,
  };
}

// Local vol: base + range / (1 + exp(stretch * (S - spot))).
export function smileSpec(p: {
  smileBase: number;
  smileRange: number;
  smileStretch: number;
}): VolSpec {
  return {
    model: engine.VolModel.SigmoidSmile,
    vol: p.smileBase,
    termMaturities: [],
    termVols: [],
    smileRange: p.smileRange,
    smileStretch: p.smileStretch,
  };
}
