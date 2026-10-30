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
- `src/lib/params.svelte.ts`: the shared, reactive parameters behind every
  figure and draggable number.
- `src/App.svelte`: navigation between essays (hash routes, e.g.
  `#/volatility`).
- `src/lib/BinomialEssay.svelte`, `src/lib/VolatilityEssay.svelte`: the
  essays.
- `src/lib/smileWorker.ts`: computes implied-vol smiles in a Web Worker.
- `src/lib/*.svelte`: reusable pieces (draggable numbers, tree view, node
  formula, line, distribution and convergence charts).
