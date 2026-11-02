// Runs the convexity hedge simulations off the main thread: the requested
// hedge in full (with a few recorded paths), a sweep over realized vol, and
// every hedge style side by side.

import type {ConvexityHedgeSpec} from './engine';

// The engine is imported when the first message arrives, not at the top of
// the module: a static import would delay registering onmessage until the
// WebAssembly had loaded, and messages arriving before then would be lost.
const engineModule = import('./engine');

export type ConvexityJob = {
  id: number;
  spec: ConvexityHedgeSpec;
  paths: number;
  recordedPaths: number;
  // Realized vols to sweep, as multiples of the implied vol.
  sweep: number[];
  sweepPaths: number;
  stylePaths: number;
};

type Summary = {mean: number; sd: number; se: number};

export type ConvexityReply =
  | {
      id: number;
      error?: undefined;
      forwardRate: number;
      futuresRate: number;
      fixedRate: number;
      fraValue: number;
      adjustmentValue: number;
      pnls: Float64Array;
      pnl: Summary;
      // The value of the PAI on the FRA's margin, per path averaged.
      pai: Summary;
      times: Float64Array;
      recordedPaths: number;
      hedgeRatios: Float64Array;
      // Hedged position's cash (FRA with PAI, plus futures), undiscounted.
      hedgedCash: Float64Array;
      sweep: {multiple: number; mean: number; se: number}[];
      styles: {style: number; mean: number; sd: number}[];
    }
  | {id: number; error: string};

function summary(sum: number, sumSquares: number, n: number): Summary {
  const mean = sum / n;
  const variance = Math.max(0, sumSquares / n - mean * mean) * (n / Math.max(1, n - 1));
  const sd = Math.sqrt(variance);
  return {mean, sd, se: sd / Math.sqrt(n)};
}

self.onmessage = async (e: MessageEvent<ConvexityJob>) => {
  const job = e.data;
  try {
    const {engine} = await engineModule;
    const steps = Math.round(job.spec.endYears * job.spec.stepsPerYear);
    const run = (spec: ConvexityHedgeSpec, paths: number, recordedPaths = 0) =>
      engine.simulateConvexityHedge({
        spec,
        paths,
        seed: 1,
        batchIndex: 0,
        recordedPaths,
        recordEvery: Math.max(1, Math.round(steps / 250)),
      });
    const main = run(job.spec, job.paths, job.recordedPaths);
    const n = main.pnls.length;
    const hedgedCash = new Float64Array(main.fraCash.length);
    for (let k = 0; k < hedgedCash.length; ++k) hedgedCash[k] = main.fraCash[k] + main.futuresCash[k];
    const sweep = job.sweep.map((multiple) => {
      const r = run({...job.spec, realizedSigma: job.spec.impliedSigma * multiple, hedgeStyle: 0, fixedAtFuturesRate: false}, job.sweepPaths);
      const s = summary(r.pnlSum, r.pnlSumSquares, r.pnls.length);
      return {multiple, mean: s.mean, se: s.se};
    });
    const styles = [0, 1, 2, 3].map((style) => {
      const r = run({...job.spec, hedgeStyle: style}, job.stylePaths);
      const s = summary(r.pnlSum, r.pnlSumSquares, r.pnls.length);
      return {style, mean: s.mean, sd: s.sd};
    });
    const reply: ConvexityReply = {
      id: job.id,
      forwardRate: main.forwardRate,
      futuresRate: main.futuresRate,
      fixedRate: main.fixedRate,
      fraValue: main.fraValue,
      adjustmentValue: main.adjustmentValue,
      pnls: main.pnls,
      pnl: summary(main.pnlSum, main.pnlSumSquares, n),
      pai: summary(main.paiValueSum, main.paiValueSumSquares, n),
      times: main.times,
      recordedPaths: main.recordedPaths,
      hedgeRatios: main.hedgeRatios,
      hedgedCash,
      sweep,
      styles,
    };
    self.postMessage(reply);
  } catch (err) {
    self.postMessage({id: job.id, error: err instanceof Error ? err.message : String(err)} satisfies ConvexityReply);
  }
};
