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

`engine_lib.js` is an ES module whose default export is an async factory:

```js
import createEngine from './engine_lib.js';
const engine = await createEngine();

const r = engine.priceVanillaCRR(
    100,     // spot
    0.16,    // vol (decimal, annualised)
    0.04,    // flat continuously-compounded discount rate
    1.1,     // tree duration in years (must exceed the expiry)
    1 / 52,  // timestep in years
    105,     // strike
    1.0,     // expiry in years
    true,    // call (false = put)
    false);  // American (false = European)

// r = {price, bsmPrice, times: [t0, t1, ...], nodes: [[S00], [S10, S11], ...]}
// or {error: "..."} for invalid inputs.
```

The bindings live in `engine_bindings.cpp`.
