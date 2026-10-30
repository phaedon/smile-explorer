// Copies the WebAssembly engine built by Bazel (//wasm:engine) into the app's
// source tree, where Vite can bundle it and TypeScript can see its types.
//
// Build the engine first with `npm run engine` (or `bazel build //wasm:engine`
// from the repo root).

import {chmodSync, copyFileSync, existsSync, mkdirSync, rmSync} from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const webDir = path.dirname(path.dirname(fileURLToPath(import.meta.url)));
const srcDir = path.resolve(webDir, '..', 'bazel-bin', 'wasm');
const destDir = path.join(webDir, 'src', 'lib', 'engine');
const files = ['engine_lib.js', 'engine_lib.wasm', 'engine_lib.d.ts'];

const missing = files.filter((f) => !existsSync(path.join(srcDir, f)));
if (missing.length > 0) {
  console.error(
      `Missing ${missing.join(', ')} in ${srcDir}.\n` +
      'Build the engine first: npm run engine');
  process.exit(1);
}

mkdirSync(destDir, {recursive: true});
for (const f of files) {
  // Bazel outputs are read-only, and copies keep that mode, so replace any
  // previous copy and make the new one writable.
  const dest = path.join(destDir, f);
  rmSync(dest, {force: true});
  copyFileSync(path.join(srcDir, f), dest);
  chmodSync(dest, 0o644);
}
console.log(`Copied the engine from ${srcDir}`);
