# Interactive essays

Explorable explanations of the pricing methods in smile-explorer, built with
[Svelte](https://svelte.dev) and [Vite](https://vite.dev). Every number is
computed live in the browser by the C++ engine, compiled to WebAssembly
(`//wasm:engine`).

```shell
cd web
npm install
npm run engine   # bazel build -c opt //wasm:engine, then copy it into src/lib/engine/
npm run dev      # http://localhost:5173
```

Other scripts:

- `npm run build`: a static site in `dist/` (relative URLs, so it can be
  served from any path, e.g. GitHub Pages).
- `npm run check`: type-check with `svelte-check`.

`npm run dev`, `build` and `check` copy the engine from `bazel-bin/` first, so
rebuild it with `npm run engine` after changing the C++ code.

## Deployment

`.github/workflows/pages.yml` builds the optimised engine and the site, and
deploys it to GitHub Pages on every push to `main` (or manually, from the
Actions tab). It needs Pages enabled once in the repository settings, with
**Source: GitHub Actions**.

## Layout

- `src/lib/engine.ts`: loads the engine; its types are generated from the C++
  structs in `api/pricing_api.h` via Embind.
- `src/lib/params.svelte.ts`, `volParams.svelte.ts`, `fxParams.svelte.ts`,
  `hwParams.svelte.ts`, `bondParams.svelte.ts`, `greeksParams.svelte.ts`,
  `swaptionParams.svelte.ts`, `smileParams.svelte.ts`, `tarfParams.svelte.ts`:
  each essay's shared,
  reactive parameters behind every figure and draggable number.
- `src/App.svelte`: the essays' order and grouping, the contents menu, and
  previous/next links (hash routes, e.g. `#/volatility`).
- `src/lib/BinomialEssay.svelte`, `VolatilityEssay.svelte`, `FxEssay.svelte`,
  `HullWhiteEssay.svelte`, `BondOptionEssay.svelte`, `GreeksEssay.svelte`,
  `SwaptionEssay.svelte`, `SmileEssay.svelte`, `TarfEssay.svelte`: the essays
  (`#/binomial`, `#/volatility`, `#/fx`, `#/tarf`, `#/hull-white`,
  `#/bond-options`, `#/greeks`, `#/swaptions`, `#/smile`).
- `src/lib/smileWorker.ts`, `hedgeWorker.ts`, `calibrationWorker.ts`,
  `smileCalibrationWorker.ts`: implied-vol smiles, delta-hedging
  simulations, Hull-White calibration and smile calibration, computed in
  Web Workers.
- `src/lib/tarfRun.svelte.ts`, `tarfPathWorker.ts`, `tarfAnalysisWorker.ts`:
  the TARF essay's live Monte Carlo run. The page pulls paths from a worker
  a few seeded blocks at a time, so it can pause, step and resume without
  changing the result, and the zero-cost strike and sweeps run in a second
  worker. `PathFan.svelte` draws the paths on a canvas as they arrive, and
  `SimControls.svelte` drives the run.
- `src/lib/Surface3D.svelte`: a 3D surface (three.js, loaded only when the
  figure scrolls into view), always paired with a 2D view such as
  `Heatmap.svelte` for reading values.
- `src/lib/*.svelte`: reusable pieces (draggable numbers, binomial and
  trinomial tree views, step controls, node formula, line, distribution and
  convergence charts).
