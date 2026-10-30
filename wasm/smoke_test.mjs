// Smoke test for the WebAssembly build of the pricing engine.
//
// Usage (from the repo root):
//   bazel build //wasm:engine && node wasm/smoke_test.mjs [path/to/engine_lib.js]

import assert from 'node:assert/strict';
import path from 'node:path';
import {pathToFileURL} from 'node:url';

const enginePath =
    path.resolve(process.argv[2] ?? 'bazel-bin/wasm/engine_lib.js');
const {default: createEngine} = await import(pathToFileURL(enginePath).href);
const engine = await createEngine();

function assertNear(actual, expected, tolerance, label) {
  assert.ok(
      Math.abs(actual - expected) <= tolerance,
      `${label}: expected ${expected} ± ${tolerance}, got ${actual}`);
}

const nodeIndex = (t, i) => t * (t + 1) / 2 + i;

const flatVol = (vol) => ({
  model: engine.VolModel.Flat,
  vol,
  termMaturities: [],
  termVols: [],
  smileRange: 0,
  smileStretch: 0,
});

function request({
  model = engine.TreeModel.CoxRossRubinstein,
  dtYears = 1 / 360.,
  strike = 105,
  expiryYears = 1.0,
  isCall = true,
  isAmerican = false,
} = {}) {
  return {
    tree: {
      model,
      spot: 100,
      vol: flatVol(0.158745),
      rate: 0.12,
      drift: 0.1,
      dtYears,
    },
    option: {strike, expiryYears, isCall, isAmerican},
  };
}

// Mirrors DerivativeTest.TreePricingApproxEqualsBSM in
// derivatives/derivative_test.cpp: a 105-strike call on a daily CRR tree with
// vol 0.158745 and a flat 12% discount rate matches Black-Scholes to 0.005.
const euro = engine.priceVanilla(request());
assertNear(euro.price, euro.bsmPrice, 0.005, 'European call vs BSM');

const jr = engine.priceVanilla(
    request({model: engine.TreeModel.JarrowRudd}));
assertNear(jr.price, euro.price, 0.005, 'Jarrow-Rudd vs CRR');

// Per-node data are triangular Float64Arrays indexed by nodeIndex(t, i).
const coarse = engine.priceVanilla(request({dtYears: 1 / 12.}));
const T = coarse.expiryIndex;
assert.equal(T, 12);
assert.ok(coarse.assetValues instanceof Float64Array);
assert.equal(coarse.times.length, T + 1);
assert.equal(coarse.assetValues.length, nodeIndex(T + 1, 0));
assert.equal(coarse.assetValues[0], 100);
assert.equal(coarse.optionValues[0], coarse.price);

// Each node's value is its discounted expected value one step later.
const [t, i] = [3, 1];
const p = coarse.upProbs[nodeIndex(t, i)];
assertNear(
    coarse.optionValues[nodeIndex(t, i)],
    coarse.forwardDfs[t] *
        (p * coarse.optionValues[nodeIndex(t + 1, i + 1)] +
         (1 - p) * coarse.optionValues[nodeIndex(t + 1, i)]),
    1e-12, 'backward induction at (3, 1)');

// Early exercise is worth something for an ITM put with positive rates.
const put = (isAmerican) =>
    engine.priceVanilla(request({strike: 110, isCall: false, isAmerican}));
const euroPut = put(false), amerPut = put(true);
assert.ok(
    amerPut.price > euroPut.price,
    `American put ${amerPut.price} should exceed European ${euroPut.price}`);

// A local-vol tree with a skew produces a skew in implied vols.
const skew = {
  ...flatVol(0.15),
  model: engine.VolModel.SigmoidSmile,
  smileRange: 0.2,
  smileStretch: 0.05,
};
assertNear(
    engine.evaluateVol(skew, 100, [100])[0], 0.25, 1e-12, 'local vol at spot');
const impliedAt = (strike) => {
  const r = engine.priceVanilla({
    tree: {
      model: engine.TreeModel.LocalVolatility,
      spot: 100,
      vol: skew,
      rate: 0.05,
      drift: 0,
      dtYears: 1 / 100,
    },
    option: {strike, expiryYears: 1, isCall: true, isAmerican: false},
  });
  return engine.impliedVolatility({
    price: r.price,
    spot: 100,
    strike,
    expiryYears: 1,
    rate: 0.05,
    isCall: true,
  });
};
assert.ok(impliedAt(80) > impliedAt(120), 'implied vols should skew');

// A currency option, priced off a domestic and a foreign zero curve: Hull,
// "Options, Futures...", Figure 13.12.
const fxRequest = (isAmerican) => ({
  tree: {
    spot: 0.61,
    vol: 0.12,
    domestic: {maturities: [1], rates: [0.05]},
    foreign: {maturities: [1], rates: [0.07]},
    dtYears: 1 / 12,
  },
  option: {strike: 0.6, expiryYears: 0.25, isCall: true, isAmerican},
});
const fxEuro = engine.priceFxVanilla(fxRequest(false));
const fxAmer = engine.priceFxVanilla(fxRequest(true));
assertNear(fxEuro.price, 0.01860, 1e-5, 'European FX call (Hull 13.12)');
assertNear(fxAmer.price, 0.01888, 1e-5, 'American FX call (Hull 13.12)');
assertNear(
    fxEuro.forwardGrowth[0], Math.exp(-0.02 / 12), 1e-12,
    'FX forward growth');

const dfs = engine.evaluateCurve({maturities: [1, 2], rates: [0.02, 0.03]},
                                 [0, 1, 2]);
assertNear(dfs[2], Math.exp(-0.06), 1e-12, 'curve discount factor');
assert.throws(
    () => engine.evaluateCurve({maturities: [2, 1], rates: [0, 0]}, [1]),
    /strictly increasing/);

// A Hull-White trinomial tree fitted to a zero curve: Hull, "Options,
// Futures...", section 32.6.
const hw = engine.buildHullWhiteTree({
  meanReversion: 0.1,
  sigma: 0.01,
  dtYears: 1,
  horizonYears: 3,
  curve: {
    maturities: [0.5, 1, 1.5, 2, 2.5, 3],
    rates: [.0343, .03824, .04183, .04512, .04812, .05086],
  },
  bondFace: 100,
});
assert.ok(hw.sliceOffsets instanceof Int32Array);
assert.deepEqual([...hw.sliceOffsets], [0, 1, 4, 9, 14]);
[0.03824, 0.05205, 0.06252].forEach(
    (alpha, t) => assertNear(hw.alphas[t], alpha, 1e-5, `alpha ${t}`));
assertNear(hw.bondPrice, hw.marketBondPrice, 1e-9, 'tree reprices the bond');

// An American put on a 9-year zero-coupon bond, on a quarterly tree.
const bondOption = (isAmerican) => engine.priceBondOption({
  tree: {
    meanReversion: 0.1,
    sigma: 0.01,
    dtYears: 0.25,
    horizonYears: 9,
    curve: {maturities: [1, 5, 10], rates: [0.05, 0.06, 0.065]},
    bondFace: 100,
  },
  option: {strike: 63, expiryYears: 3, isCall: false, isAmerican},
});
const bondEuro = bondOption(false);
assertNear(bondEuro.price, bondEuro.analyticPrice, 0.05,
           'bond option tree vs closed form');
assert.ok(bondOption(true).price > bondEuro.price,
          'American bond put should exceed European');
assert.equal(bondEuro.expiryIndex, 12);

// Invalid requests throw a JS Error.
assert.throws(
    () => engine.priceVanilla(request({expiryYears: -1})), /expiry_years/);

console.log(
    `OK: European call ${euro.price.toFixed(4)} (BSM ${
        euro.bsmPrice.toFixed(4)}), FX call ${fxAmer.price.toFixed(5)}, ` +
    `put ${euroPut.price.toFixed(4)} European / ${
        amerPut.price.toFixed(4)} American`);
