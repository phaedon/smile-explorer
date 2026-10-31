// Runs delta-hedging simulations off the main thread: the requested one, in
// full, and a sweep over rebalancing frequencies for the error-vs-frequency
// chart.

import {engine} from './engine';

// Plain numbers only, so that the messages survive structured cloning.
export type HedgeRequest = {
  id: number;
  spot: number;
  strike: number;
  expiryYears: number;
  isCall: boolean;
  impliedVol: number;
  realisedVol: number;
  rate: number;
  rebalances: number;
  paths: number;
  sweep: number[];
};

export type HedgeResponse =
  | {
      id: number;
      error?: undefined;
      pnl: Float64Array;
      mean: number;
      stddev: number;
      premium: number;
      times: Float64Array;
      spots: Float64Array;
      cumulativePnl: Float64Array;
      cumulativeTheta: Float64Array;
      cumulativeGamma: Float64Array;
      sweep: {rebalances: number; stddev: number}[];
    }
  | {id: number; error: string};

self.onmessage = (e: MessageEvent<HedgeRequest>) => {
  const {id, sweep, ...spec} = e.data;
  try {
    const run = (rebalances: number, paths: number) =>
      engine.simulateDeltaHedge({...spec, rebalances, paths, seed: 1});
    const main = run(spec.rebalances, spec.paths);
    const response: HedgeResponse = {
      id,
      pnl: main.pnl,
      mean: main.mean,
      stddev: main.stddev,
      premium: main.premium,
      times: main.times,
      spots: main.spots,
      cumulativePnl: main.cumulativePnl,
      cumulativeTheta: main.cumulativeTheta,
      cumulativeGamma: main.cumulativeGamma,
      sweep: sweep.map((n) => ({rebalances: n, stddev: run(n, 1000).stddev})),
    };
    self.postMessage(response);
  } catch (err) {
    self.postMessage({id, error: err instanceof Error ? err.message : String(err)} satisfies HedgeResponse);
  }
};
