# WebAssembly engine

The pricing core compiled to WebAssembly with [Emscripten](https://emscripten.org),
so it can run in a browser (or Node) with no native install.

```shell
# Build (downloads the Emscripten SDK on first use).
bazel build //wasm:engine
# -> bazel-bin/wasm/engine_lib.js and bazel-bin/wasm/engine_lib.wasm

# Smoke test in Node.
node wasm/smoke_test.mjs

# Browser demo: serve the repo root and open wasm/demo.html.
python3 -m http.server 8000
# -> http://localhost:8000/wasm/demo.html
```

## API

`engine_lib.js` is an ES module whose default export is an async factory.
TypeScript definitions are generated alongside it (`engine_lib.d.ts`).

```js
import createEngine from './engine_lib.js';
const engine = await createEngine();

const r = engine.priceVanilla({
  tree: {
    model: engine.TreeModel.CoxRossRubinstein,  // or JarrowRudd, LocalVolatility
    spot: 100,
    vol: {
      model: engine.VolModel.Flat,  // or TermStructure, SigmoidSmile
      vol: 0.16,                    // annualised, as a decimal
      termMaturities: [],           // TermStructure: implied vols by maturity
      termVols: [],
      smileRange: 0,                // SigmoidSmile: local vol skew in price
      smileStretch: 0,
    },
    rate: 0.04,       // flat, continuously compounded
    drift: 0.1,       // Jarrow-Rudd only
    dtYears: 1 / 52,  // timestep
  },
  option: {strike: 105, expiryYears: 1.0, isCall: true, isAmerican: false},
});
```

The result holds the price, the Black-Scholes European price for reference,
and every node of the backward induction (asset and option values, exercise
and continuation values, up-probabilities, Arrow-Debreu prices) as
`Float64Array`s. Node `(t, i)` is at index `t * (t + 1) / 2 + i`; see
`api/pricing_api.h` for details. Invalid requests throw an `Error`.

Also exported: `evaluateVol(volSpec, spot, xs)` samples a vol spec's forward
or local vol, and `impliedVolatility({price, spot, strike, expiryYears, rate,
isCall})` inverts Black-Scholes.

The bindings live in `engine_bindings.cpp`, on top of the C++ API in `//api`.
