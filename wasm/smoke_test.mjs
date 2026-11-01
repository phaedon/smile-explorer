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
  ssviRho: 0,
  ssviEta: 0,
  ssviGamma: 0,
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

// Greeks off the tree agree with the closed forms.
const greeks = engine.computeGreeks(request({dtYears: 1 / 360.}), 0.01, 0.0001);
assertNear(greeks.tree.delta, greeks.closedForm.delta, 0.005, 'tree delta');
assertNear(greeks.tree.vega, greeks.closedForm.vega, 0.01 * greeks.closedForm.vega,
           'tree vega');
const gammas = engine.greekSurface({
  greek: engine.Greek.Gamma,
  strike: 100,
  vol: 0.2,
  rate: 0,
  isCall: true,
  spots: [90, 100, 110],
  expiries: [0.05, 1],
});
assert.equal(gammas.length, 6);
assert.ok(gammas[1] > gammas[4], 'ATM gamma grows near expiry');

// Delta hedging is reproducible for a given seed.
const hedge = (seed) => engine.simulateDeltaHedge({
  spot: 100, strike: 100, expiryYears: 0.25, isCall: true, impliedVol: 0.2,
  realisedVol: 0.2, rate: 0, rebalances: 50, paths: 500, seed,
});
assert.deepEqual([...hedge(3).pnl], [...hedge(3).pnl]);
assert.ok(Math.abs(hedge(3).mean) < 0.1 * hedge(3).premium, 'hedge mean');

// A 2y x 5y payer swaption, on the tree and by Jamshidian, and calibration
// recovering the parameters behind a set of quotes.
const rateCurve = {maturities: [0.5, 1, 2, 5, 10],
                   rates: [0.03, 0.032, 0.035, 0.04, 0.043]};
const swaption = engine.priceSwaption({
  tree: {meanReversion: 0.1, sigma: 0.01, dtYears: 1 / 24, horizonYears: 0,
         curve: rateCurve, bondFace: 0},
  swaption: {expiryYears: 2, tenorYears: 5, fixedPaymentsPerYear: 1,
             atTheMoney: true, fixedRate: 0, notional: 100, isPayer: true,
             isBermudan: false},
});
assertNear(swaption.option.price, swaption.option.analyticPrice,
           0.003 * swaption.option.analyticPrice, 'swaption tree vs closed form');
const bermudan = engine.priceSwaption({
  tree: {meanReversion: 0.1, sigma: 0.01, dtYears: 1 / 12, horizonYears: 0,
         curve: rateCurve, bondFace: 0},
  swaption: {expiryYears: 2, tenorYears: 5, fixedPaymentsPerYear: 1,
             atTheMoney: true, fixedRate: 0, notional: 100, isPayer: true,
             isBermudan: true},
});
assert.equal(bermudan.exerciseTimes.length, 5);
assert.ok(bermudan.option.price > Math.max(...bermudan.coterminalEuropeanPrices),
          'Bermudan exceeds its co-terminal Europeans');
const quotes = [1, 2, 3, 4].map((expiry) => ({
  type: engine.RateOptionType.Swaption, expiry, tenor: 5 - expiry, normalVol: 0,
}));
const modelVols = engine.modelNormalVols({
  curve: rateCurve, params: {meanReversion: 0.15, sigma: 0.011}, quotes,
  fixedPaymentsPerYear: 1,
});
const calibrated = engine.calibrateHullWhite({
  curve: rateCurve,
  quotes: quotes.map((q, i) => ({...q, normalVol: modelVols[i]})),
  fixedPaymentsPerYear: 1,
});
assertNear(calibrated.params.meanReversion, 0.15, 1e-4, 'calibrated a');
assertNear(calibrated.params.sigma, 0.011, 1e-6, 'calibrated sigma');

// The smile: quotes -> SSVI -> Dupire local vol -> tree -> implied vols.
const smileExpiries = [0.25, 0.5, 1, 2];
const smileStrikes = [70, 80, 90, 95, 100, 105, 110, 120, 130];
const smileVols = smileExpiries.flatMap((t, i) => smileStrikes.map((k) => {
  const m = Math.log(k / (100 * Math.exp(0.02 * t))) / Math.sqrt(t);
  return [0.22, 0.20, 0.19, 0.185][i] - 0.06 * m + 0.02 * m * m;
}));
const smileFit = engine.calibrateSmile({
  spot: 100, rate: 0.02, expiries: smileExpiries, strikes: smileStrikes,
  vols: smileVols,
});
assert.ok(smileFit.butterflyOk && smileFit.rmsError < 0.002, 'SSVI fit');
const smileGrid = {
  spot: 100, rate: 0.02,
  ssvi: {rho: smileFit.rho, eta: smileFit.eta, gamma: smileFit.gamma,
         expiries: smileExpiries, thetas: [...smileFit.thetas]},
  strikes: smileStrikes, times: smileExpiries,
};
const repriced = engine.repriceSmile({grid: smileGrid, stepsPerYear: 200});
assert.ok(repriced.rmsError < 0.001, `smile repriced on the tree (${repriced.rmsError})`);

// A TARF, a batch at a time: EUR-USD, buying EUR monthly for a year.
const tarf = {
  spot: 1.1, vol: 0.08,
  domestic: {maturities: [0.25, 1, 2, 5], rates: [0.0425, 0.04, 0.0375, 0.0375]},
  foreign: {maturities: [0.25, 1, 2, 5], rates: [0.02, 0.021, 0.0225, 0.025]},
  notional: 1e6, target: 2e5, strike: 1.1, leverage: 2, endYears: 1,
  fixingFrequency: 1 / 12, isLong: true, stepsPerFixing: 4,
};
const tarfValuation = engine.valueTarf({tarf, paths: 4000, seed: 1});
assert.ok(tarfValuation.zeroCostStrike < tarfValuation.weightedForward,
          'TARF zero-cost strike below the forward');
const atZeroCost = {...tarf, strike: tarfValuation.zeroCostStrike};
const batch = (batchIndex) => engine.simulateTarfBatch({
  tarf: atZeroCost, paths: 250, seed: 1, batchIndex, recordedPaths: 50});
let tarfStart = performance.now();
const batches = [0, 1, 2, 3].map(batch);
const tarfBatchMs = (performance.now() - tarfStart) / 4;
assert.equal(batches[0].npvs.length, 250);
assert.equal(batches[0].spots.length, 50 * 49);
// Batches are reproducible in any order, so pausing a run can't change it.
assert.deepEqual([...batch(2).npvs], [...batches[2].npvs]);
assert.notDeepEqual([...batches[1].npvs], [...batches[0].npvs]);
const tarfMean = batches.reduce((s, b) => s + b.npvSum, 0) / 1000;
const tarfSweep = engine.sweepTarf({
  valuation: {tarf: atZeroCost, paths: 2000, seed: 1},
  field: engine.TarfSweepField.Vol, values: [0.06, 0.08, 0.1]});
assert.ok(tarfSweep.npvs[0] > tarfSweep.npvs[2], 'TARF vega is negative');
tarfStart = performance.now();
engine.valueTarf({tarf, paths: 4000, seed: 1});
const tarfValuationMs = performance.now() - tarfStart;
assert.throws(() => engine.simulateTarfBatch({
  tarf, paths: 0, seed: 1, batchIndex: 0, recordedPaths: 0}), /paths/);
console.log(
    `TARF: zero-cost strike ${tarfValuation.zeroCostStrike.toFixed(4)} ` +
    `(forward ${tarfValuation.weightedForward.toFixed(4)}), mean of 1000 ` +
    `paths ${tarfMean.toFixed(0)}, ${tarfBatchMs.toFixed(2)} ms a batch, ` +
    `valuation ${tarfValuationMs.toFixed(0)} ms, knocked out ${
        [...batches[0].knockoutFixings].filter((k) => k >= 0).length}/250`);

// Natural gas: a mean-reverting tree fitted to a seasonal futures curve.
const gasTimes = Array.from({length: 24}, (_, m) => (m + 1) / 12);
const gas = {
  spot: 3.0,
  futuresTimes: gasTimes,
  futuresPrices: gasTimes.map(
      (t) => 3.2 * (1 + 0.25 * Math.cos(2 * Math.PI * (t - 2 / 12)))),
  rate: 0.04, meanReversion: 1.5, sigma: 0.6, stepsPerMonth: 4,
  horizonYears: 2,
};
let gasStart = performance.now();
const gasTree = engine.buildCommodityTree(gas);
const gasTreeMs = performance.now() - gasStart;
assert.equal(gasTree.times.length, 97);
for (let t = 0; t < gasTree.times.length; ++t) {
  assertNear(gasTree.expectedSpots[t] / gasTree.curvePrices[t], 1, 1e-12,
             `gas tree fitted at slice ${t}`);
}
assert.ok(gasTree.futuresVols[0] > gasTree.futuresVols[23],
          'near futures are more volatile');
gasStart = performance.now();
// At four steps a month the tree is a percent or two off; at sixteen, much
// closer.
const gasOption = engine.priceCommodityOption({
  commodity: {...gas, stepsPerMonth: 16},
  option: {deliveryYears: 3 / 12, expiryYears: 3 / 12, strike: 3.9,
           isCall: true, isAmerican: true}});
const gasOptionMs = performance.now() - gasStart;
assertNear(gasOption.europeanPrice / gasOption.blackPrice, 1, 0.01,
           'gas option on the tree vs Black-76');
assert.ok(gasOption.price >= gasOption.europeanPrice);
assert.throws(() => engine.buildCommodityTree({...gas, sigma: 0}), /sigma/);
console.log(
    `Gas: Feb call ${gasOption.europeanPrice.toFixed(4)} on the tree ` +
    `(Black-76 ${gasOption.blackPrice.toFixed(4)}, vol ${
        (gasOption.impliedVol * 100).toFixed(1)}%), American ${
        gasOption.price.toFixed(4)}; tree ${gasTreeMs.toFixed(1)} ms, ` +
    `option ${gasOptionMs.toFixed(1)} ms`);

// Invalid requests throw a JS Error.
assert.throws(
    () => engine.priceVanilla(request({expiryYears: -1})), /expiry_years/);

console.log(
    `OK: European call ${euro.price.toFixed(4)} (BSM ${
        euro.bsmPrice.toFixed(4)}), FX call ${fxAmer.price.toFixed(5)}, ` +
    `put ${euroPut.price.toFixed(4)} European / ${
        amerPut.price.toFixed(4)} American`);
