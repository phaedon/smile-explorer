// Values the TARF for the essay's analysis: the zero-cost strike first (the
// live run waits on it), then sweeps across vol, target and leverage. Every
// valuation uses the same seed, so the sweeps differ only through the terms,
// not through Monte Carlo noise.

import type {TarfSpec, TarfValuation} from './engine';

// See hedgeWorker.ts for why the engine is imported here and not statically.
const engineModule = import('./engine');

export type AnalysisJob = {
  id: number;
  tarf: TarfSpec;
  valuationPaths: number;
  sweepPaths: number;
  vols: number[];
  targets: number[];
  leverages: number[];
};

type Sweep = {npvs: number[]; strikes: number[]; forward: number};

export type AnalysisReply =
  | {id: number; kind: 'valuation'; valuation: TarfValuation; error?: undefined}
  | {id: number; kind: 'sweeps'; vol: Sweep; target: Sweep; leverage: Sweep; error?: undefined}
  | {id: number; error: string};

self.onmessage = async (e: MessageEvent<AnalysisJob>) => {
  const job = e.data;
  try {
    const {engine} = await engineModule;
    const valuation = engine.valueTarf({tarf: job.tarf, paths: job.valuationPaths, seed: 2});
    self.postMessage({id: job.id, kind: 'valuation', valuation} satisfies AnalysisReply);

    const sweep = (field: 'Vol' | 'Target' | 'Leverage', values: number[]): Sweep => {
      const s = engine.sweepTarf({
        valuation: {tarf: job.tarf, paths: job.sweepPaths, seed: 2},
        field: engine.TarfSweepField[field],
        values,
      });
      return {npvs: [...s.npvs], strikes: [...s.zeroCostStrikes], forward: s.weightedForwards[0]};
    };
    self.postMessage({
      id: job.id,
      kind: 'sweeps',
      vol: sweep('Vol', job.vols),
      target: sweep('Target', job.targets),
      leverage: sweep('Leverage', job.leverages),
    } satisfies AnalysisReply);
  } catch (err) {
    self.postMessage({id: job.id, error: err instanceof Error ? err.message : String(err)} satisfies AnalysisReply);
  }
};
