// Calibrates Hull-White to swaption quotes off the main thread (each fit takes
// tens of milliseconds, too slow to repeat on every drag in the page).

// The engine is imported when the first message arrives, not at the top of
// the module: a static import would delay registering onmessage until the
// WebAssembly had loaded, and messages arriving before then would be lost.
const engineModule = import('./engine');

// Plain numbers only: Embind enums don't survive structured cloning.
export type CalibrationJob = {
  id: number;
  curve: {maturities: number[]; rates: number[]};
  quotes: {expiry: number; tenor: number; normalVol: number}[];
};

export type CalibrationReply =
  | {
      id: number;
      error?: undefined;
      meanReversion: number;
      sigma: number;
      modelVols: number[];
      rmsError: number;
      converged: boolean;
    }
  | {id: number; error: string};

self.onmessage = async (e: MessageEvent<CalibrationJob>) => {
  const {id, curve, quotes} = e.data;
  try {
    const {engine} = await engineModule;
    const result = engine.calibrateHullWhite({
      curve,
      quotes: quotes.map((q) => ({...q, type: engine.RateOptionType.Swaption})),
      fixedPaymentsPerYear: 1,
    });
    self.postMessage({
      id,
      meanReversion: result.params.meanReversion,
      sigma: result.params.sigma,
      modelVols: Array.from(result.modelVols),
      rmsError: result.rmsError,
      converged: result.converged,
    } satisfies CalibrationReply);
  } catch (err) {
    self.postMessage({id, error: err instanceof Error ? err.message : String(err)} satisfies CalibrationReply);
  }
};
