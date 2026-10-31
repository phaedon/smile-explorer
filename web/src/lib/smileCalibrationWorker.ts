// Fits the SSVI surface to the quotes, evaluates it and its local vol on a
// grid, and reprices the quotes on the local-vol tree: off the main thread,
// since together these take tens of milliseconds.

// The engine is imported when the first message arrives, not at the top of
// the module: a static import would delay registering onmessage until the
// WebAssembly had loaded, and messages arriving before then would be lost.
const engineModule = import('./engine');

export type SmileJob = {
  id: number;
  quotes: {spot: number; rate: number; expiries: number[]; strikes: number[]; vols: number[]};
  gridStrikes: number[];
  gridTimes: number[];
  densityStrikes: number[];
  stepsPerYear: number;
  convergenceSteps: number[];
};

export type SmileReply =
  | {
      id: number;
      error?: undefined;
      fit: {
        rho: number;
        eta: number;
        gamma: number;
        thetas: number[];
        thetasAdjusted: boolean;
        fittedVols: number[];
        rmsError: number;
        butterflyOk: boolean;
      };
      impliedGrid: number[];
      localGrid: number[];
      clamped: number;
      // Per expiry, at densityStrikes.
      densities: number[][];
      treeVols: number[];
      repriceResiduals: number[];
      repriceRms: number;
      convergence: {steps: number; rms: number}[];
    }
  | {id: number; error: string};

self.onmessage = async (e: MessageEvent<SmileJob>) => {
  const job = e.data;
  try {
    const {engine} = await engineModule;
    const q = job.quotes;
    const fit = engine.calibrateSmile(q);
    const ssvi = {rho: fit.rho, eta: fit.eta, gamma: fit.gamma, expiries: q.expiries, thetas: [...fit.thetas]};
    const base = {spot: q.spot, rate: q.rate, ssvi};
    const surfaces = engine.smileSurfaces({...base, strikes: job.gridStrikes, times: job.gridTimes});
    const densities = q.expiries.map((t) => [
      ...engine.impliedDensity({...base, strikes: job.densityStrikes, times: [t]}),
    ]);
    const atQuotes = {...base, strikes: q.strikes, times: q.expiries};
    const repriced = engine.repriceSmile({grid: atQuotes, stepsPerYear: job.stepsPerYear});
    const convergence = job.convergenceSteps.map((steps) => ({
      steps,
      rms: engine.repriceSmile({grid: atQuotes, stepsPerYear: steps}).rmsError,
    }));
    self.postMessage({
      id: job.id,
      fit: {
        rho: fit.rho,
        eta: fit.eta,
        gamma: fit.gamma,
        thetas: [...fit.thetas],
        thetasAdjusted: fit.thetasAdjusted,
        fittedVols: [...fit.fittedVols],
        rmsError: fit.rmsError,
        butterflyOk: fit.butterflyOk,
      },
      impliedGrid: [...surfaces.impliedVols],
      localGrid: [...surfaces.localVols],
      clamped: surfaces.clamped,
      densities,
      treeVols: [...repriced.treeVols],
      repriceResiduals: [...repriced.residuals],
      repriceRms: repriced.rmsError,
      convergence,
    } satisfies SmileReply);
  } catch (err) {
    self.postMessage({id: job.id, error: err instanceof Error ? err.message : String(err)} satisfies SmileReply);
  }
};
