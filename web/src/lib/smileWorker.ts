// Computes implied-vol smiles off the main thread: each one prices a European
// call per strike on a fine local-vol tree, then inverts Black-Scholes.

// The engine is imported when the first message arrives, not at the top of
// the module: a static import would delay registering onmessage until the
// WebAssembly had loaded, and messages arriving before then would be lost.
const modules = Promise.all([import('./engine'), import('./volSpecs')]);

// Plain numbers only: Embind enums (like VolModel) don't survive
// postMessage's structured clone, so the worker builds the VolSpec itself.
export type SmileRequest = {
  id: number;
  smileBase: number;
  smileRange: number;
  smileStretch: number;
  spot: number;
  rate: number;
  expiryYears: number;
  dtYears: number;
  strikes: number[];
};

export type SmileResponse =
  | {id: number; points: {x: number; y: number}[]; error?: undefined}
  | {id: number; points?: undefined; error: string};

self.onmessage = async (e: MessageEvent<SmileRequest>) => {
  const {id, spot, rate, expiryYears, dtYears, strikes} = e.data;
  let response: SmileResponse;
  try {
    const [{engine}, {smileSpec}] = await modules;
    const vol = smileSpec(e.data);
    const points = strikes.map((strike) => {
      const r = engine.priceVanilla({
        tree: {
          model: engine.TreeModel.LocalVolatility,
          spot,
          vol,
          rate,
          drift: 0,
          dtYears,
        },
        option: {strike, expiryYears, isCall: true, isAmerican: false},
      });
      return {
        x: strike,
        y: engine.impliedVolatility({
          price: r.price,
          spot,
          strike,
          expiryYears,
          rate,
          isCall: true,
        }),
      };
    });
    response = {id, points};
  } catch (err) {
    response = {id, error: err instanceof Error ? err.message : String(err)};
  }
  self.postMessage(response);
};
