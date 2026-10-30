# Interactive essays

Explorable explanations of the pricing methods in smile-explorer, built with
[Svelte](https://svelte.dev) and [Vite](https://vite.dev). Every number is
computed live in the browser by the C++ engine, compiled to WebAssembly
(`//wasm:engine`).

```shell
cd web
npm install
npm run engine   # bazel build //wasm:engine, then copy it into src/lib/engine/
npm run dev      # http://localhost:5173
```

Other scripts:

- `npm run build`: a static site in `dist/` (relative URLs, so it can be
  served from any path, e.g. GitHub Pages).
- `npm run check`: type-check with `svelte-check`.

`npm run dev`, `build` and `check` copy the engine from `bazel-bin/` first, so
rebuild it with `npm run engine` after changing the C++ code.

## Layout

- `src/lib/engine.ts`: loads the engine; its types are generated from the C++
  structs in `api/pricing_api.h` via Embind.
- `src/lib/params.svelte.ts`: the shared, reactive parameters behind every
  figure and draggable number.
- `src/lib/BinomialEssay.svelte`: the binomial tree essay.
- `src/lib/*.svelte`: reusable pieces (draggable numbers, tree view, node
  formula, distribution and convergence charts).
