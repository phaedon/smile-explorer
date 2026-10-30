// Smoke test for the WebAssembly build of the pricing engine.
//
// Usage (from the repo root):
//   bazel build //wasm:engine && node wasm/smoke_test.mjs [path/to/engine_lib.js]

import assert from 'node:assert/strict';
import path from 'node:path';
import {pathToFileURL} from 'node:url';

const enginePath = path.resolve(
    process.argv[2] ?? 'bazel-bin/wasm/engine_lib.js');
const {default: createEngine} = await import(pathToFileURL(enginePath).href);
const engine = await createEngine();

function assertNear(actual, expected, tolerance, label) {
  assert.ok(
      Math.abs(actual - expected) <= tolerance,
      `${label}: expected ${expected} ± ${tolerance}, got ${actual}`);
}

// Mirrors DerivativeTest.TreePricingApproxEqualsBSM in
// derivatives/derivative_test.cpp: a 105-strike call on a daily CRR tree with
// vol 0.158745 and a flat 12% discount rate matches Black-Scholes to 0.005.
const spot = 100, vol = 0.158745, rate = 0.12;
const treeYears = 1.1, dt = 1 / 360., expiry = 1.0;

const euro = engine.priceVanillaCRR(
    spot, vol, rate, treeYears, dt, 105, expiry, /*is_call=*/ true,
    /*is_american=*/ false);
assert.equal(euro.error, undefined, euro.error);
assertNear(euro.price, euro.bsmPrice, 0.005, 'European call vs BSM');

// The tree is returned as one array of node values per timestep.
assert.equal(euro.times.length, euro.nodes.length);
assert.equal(euro.times[0], 0);
assert.deepEqual(euro.nodes[0], [spot]);
euro.nodes.forEach((states, t) => assert.equal(states.length, t + 1));

// Early exercise is worth something for an ITM put with positive rates.
const put = (american) => engine.priceVanillaCRR(
    spot, vol, rate, treeYears, dt, 110, expiry, false, american);
const euroPut = put(false), amerPut = put(true);
assert.ok(
    amerPut.price > euroPut.price,
    `American put ${amerPut.price} should exceed European ${euroPut.price}`);

// Invalid requests return an error instead of a price.
const bad = engine.priceVanillaCRR(
    spot, vol, rate, treeYears, dt, 105, /*expiry=*/ 2.0, true, false);
assert.match(bad.error, /expiry_years/);
assert.equal(bad.price, undefined);

console.log(
    `OK: European call ${euro.price.toFixed(4)} (BSM ${
        euro.bsmPrice.toFixed(4)}), ` +
    `put ${euroPut.price.toFixed(4)} European / ${
        amerPut.price.toFixed(4)} American`);
