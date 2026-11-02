<!--
  "Futures, forwards and convexity": why a SOFR future's rate sits above the
  forward rate of a collateralized FRA on the same quarter, what collateral
  (and the interest paid on it) has to do with it, and the hedge that earns
  the difference back.
-->
<script lang="ts">
  import {onDestroy} from 'svelte';

  import DraggableNumber from './DraggableNumber.svelte';
  import Histogram from './Histogram.svelte';
  import LineChart, {type Series} from './LineChart.svelte';
  import ToggleToken from './ToggleToken.svelte';
  import TrinomialTreeView from './TrinomialTreeView.svelte';
  import type {ConvexityJob, ConvexityReply} from './convexityWorker';
  import {
    HEDGE_STYLES,
    NOTIONAL,
    PERIOD,
    SOFR_CURVE,
    STEPS_PER_YEAR,
    convexityParams as p,
    hedgeSpecFor,
  } from './convexityParams.svelte';
  import {engine, type FuturesTreeResult} from './engine';
  import {fmt, level, pct} from './format';

  function attempt<T>(fn: () => T): {value: T; error?: undefined} | {value?: undefined; error: string} {
    try {
      return {value: fn()};
    } catch (e) {
      return {error: e instanceof Error ? e.message : String(e)};
    }
  }

  // Basis points, without a "-0.0" for tiny negatives.
  const bp = (v: number, digits = 2) => `${fmt(Math.abs(v) < 5e-9 ? 0 : v * 1e4, digits)}bp`;
  const rateFmt = (v: number) => pct(v, 3);
  const yearsFmt = (v: number) => `${level(v)}y`;
  const usd = (v: number, digits = 0) => {
    const s = Math.abs(v).toLocaleString('en-US', {minimumFractionDigits: digits, maximumFractionDigits: digits});
    return `${v < 0 && Number(s.replace(/,/g, '')) !== 0 ? '−' : ''}$${s}`;
  };
  const curve = {maturities: [...SOFR_CURVE.maturities], rates: [...SOFR_CURVE.rates]};

  // ---- 1. Today's rates for the chosen quarter -----------------------------------

  const today = $derived(
    attempt(() =>
      engine.futuresStrip({curve, meanReversion: p.meanReversion, sigma: p.sigma, starts: [p.start], periodYears: PERIOD}),
    ),
  );
  const forward = $derived(today.value?.forwardRates[0] ?? NaN);
  const futures = $derived(today.value?.futuresRates[0] ?? NaN);
  const adjustment = $derived(futures - forward);

  // ---- 2. One path, with and without PAI -----------------------------------------

  const steps = $derived(Math.round((p.start + PERIOD) * STEPS_PER_YEAR));
  const marginOutcome = $derived(
    attempt(() =>
      engine.simulateConvexityHedge({
        spec: hedgeSpecFor(p, {realizedSigma: p.sigma, hedgeStyle: 3, fixedAtFuturesRate: false}),
        paths: 1,
        seed: 1000 + p.pathNumber,
        batchIndex: 0,
        recordedPaths: 1,
        recordEvery: Math.max(1, Math.round(steps / 300)),
      }),
    ),
  );
  const marginPath = $derived.by(() => {
    const m = marginOutcome.value;
    if (!m) return null;
    const pts = (values: Float64Array) => Array.from(m.times, (t, k) => ({x: t, y: values[k]}));
    const last = m.times.length - 1;
    // Every few points, so the line underneath shows through.
    const sparse = (values: Float64Array) =>
      Array.from(m.times, (t, k) => ({x: t, y: values[k]})).filter((_, k) => k % 8 === 0 || k === last);
    const settlement = m.futuresRates[last];
    return {
      mark: pts(m.fraValues),
      withPai: sparse(m.fraCash),
      withoutPai: pts(m.noPaiCash),
      gapWith: Array.from(m.times, (t, k) => ({x: t, y: m.fraCash[k] - m.fraValues[k]})),
      gapWithout: Array.from(m.times, (t, k) => ({x: t, y: m.noPaiCash[k] - m.fraValues[k]})),
      rates: pts(m.shortRates),
      payoff: NOTIONAL * PERIOD * (m.fixedRate - settlement),
      cashWith: m.fraCash[last],
      cashWithout: m.noPaiCash[last],
      settlement,
      peak: Math.max(...Array.from(m.fraValues).map(Math.abs)),
    };
  });
  const marginSeries = $derived.by((): Series[] => {
    const m = marginPath;
    if (!m) return [];
    return p.pai
      ? [
          {name: "the FRA's value (its mark)", color: 'var(--series-1)', points: m.mark},
          {name: 'cash from its margin, with PAI', color: 'var(--series-2)', points: m.withPai, dotsOnly: true},
        ]
      : [
          {name: "the FRA's value (its mark)", color: 'var(--series-1)', points: m.mark},
          {name: 'cash from its margin, without PAI', color: 'var(--series-2)', points: m.withoutPai},
        ];
  });

  // ---- 3. The tree ------------------------------------------------------------------

  const treeOutcome = $derived(
    attempt(() =>
      engine.futuresOnTree({
        tree: {
          meanReversion: p.meanReversion,
          sigma: p.sigma,
          dtYears: 1 / p.treeStepsPerYear,
          horizonYears: 0,
          curve,
          bondFace: 100,
        },
        startYears: p.start,
        endYears: p.start + PERIOD,
      }),
    ),
  );
  let lastTree: FuturesTreeResult | null = null;
  const futuresTree = $derived.by(() => {
    if (treeOutcome.value) lastTree = treeOutcome.value;
    return treeOutcome.value ?? lastTree;
  });
  const nodeGaps = $derived.by(() => {
    const t = futuresTree;
    if (!t) return null;
    const out = new Float64Array(t.futuresRates.length);
    for (let n = 0; n < out.length; ++n) out[n] = t.futuresRates[n] - t.forwardRates[n];
    return out;
  });
  let treeHover = $state<number | null>(null);
  const treeNode = $derived(treeHover ?? 0);
  const treeSliceOf = (n: number) => {
    const t = futuresTree;
    if (!t) return 0;
    let s = 0;
    while (t.tree.sliceOffsets[s + 1] <= n) ++s;
    return s;
  };

  // ---- 4. The strip -------------------------------------------------------------------

  const STRIP_STARTS = Array.from({length: 40}, (_, k) => 0.25 * k);
  const strip = $derived(
    attempt(() => {
      const at = (a: number) =>
        engine.futuresStrip({curve, meanReversion: a, sigma: p.sigma, starts: STRIP_STARTS, periodYears: PERIOD});
      const base = at(p.meanReversion);
      const flat = at(0.001);
      const pts = (values: Float64Array) => STRIP_STARTS.map((s, k) => ({x: s, y: values[k] * 1e4}));
      return {
        sofr: pts(base.adjustments),
        term: pts(base.termAdjustments),
        noReversion: pts(flat.adjustments),
        at2: base.adjustments[8],
        at5: base.adjustments[20],
        at10: base.adjustments[39],
        flat10: flat.adjustments[39],
      };
    }),
  );
  // The rule of thumb: half sigma squared times the expiry times the end.
  const ruleOfThumb = $derived(0.5 * p.sigma * p.sigma * p.start * (p.start + PERIOD));

  // ---- 5. The hedge -------------------------------------------------------------------

  const SWEEP = [0.5, 0.75, 1, 1.25, 1.5];
  const worker = new Worker(new URL('./convexityWorker.ts', import.meta.url), {type: 'module'});
  let jobId = 0;
  let hedge = $state<ConvexityReply | null>(null);
  let running = $state(false);
  worker.onmessage = (e: MessageEvent<ConvexityReply>) => {
    if (e.data.id === jobId) {
      hedge = e.data;
      running = false;
    }
  };
  worker.onerror = (e) => {
    hedge = {id: jobId, error: `the simulation failed to load (${e.message || 'worker error'})`};
    running = false;
  };
  $effect(() => {
    const job: ConvexityJob = {
      id: ++jobId,
      spec: hedgeSpecFor(p),
      paths: 2000,
      recordedPaths: 12,
      sweep: SWEEP,
      sweepPaths: 300,
      stylePaths: 500,
    };
    running = true;
    worker.postMessage(job);
  });
  onDestroy(() => worker.terminate());

  type HedgeOk = Extract<ConvexityReply, {error?: undefined}>;
  const hedgeOk = $derived(hedge && hedge.error === undefined ? (hedge as HedgeOk) : null);
  const hedgeError = $derived(hedge?.error ?? null);

  const ratioSeries = $derived.by((): Series[] => {
    const h = hedgeOk;
    if (!h || h.recordedPaths === 0) return [];
    const row = h.times.length;
    const out: Series[] = [];
    for (let path = 0; path < h.recordedPaths; ++path) {
      out.push({
        name: `path ${path + 1}`,
        group: 'futures sold per FRA, on 12 paths',
        color: 'var(--series-1)',
        points: Array.from(h.times.subarray(0, row - 1), (t, k) => ({x: t, y: h.hedgeRatios[path * row + k]})),
      });
    }
    const end = p.start + PERIOD;
    const dfs = attempt(() => engine.evaluateCurve(curve, [...Array.from(h.times), end]));
    if (dfs.value) {
      const dfEnd = dfs.value[dfs.value.length - 1];
      out.push({
        name: "P(0, end) / P(0, t): today's forward discount factor",
        color: 'var(--series-2)',
        dashed: true,
        points: Array.from(h.times, (t, k) => ({x: t, y: dfEnd / dfs.value![k]})),
      });
    }
    return out;
  });

  const sweepSeries = $derived.by((): Series[] => {
    const h = hedgeOk;
    if (!h) return [];
    const toBp = (m: number) => p.sigma * m * 1e4;
    const fine = Array.from({length: 41}, (_, k) => 0.5 + k * 0.025);
    return [
      {
        name: 'average P&L over 300 paths',
        color: 'var(--series-1)',
        dotsOnly: true,
        points: h.sweep.map((s) => ({x: toBp(s.multiple), y: s.mean})),
      },
      {
        name: 'the adjustment’s value × (σ²realized / σ²implied − 1)',
        color: 'var(--series-2)',
        dashed: true,
        points: fine.map((m) => ({x: toBp(m), y: h.adjustmentValue * (m * m - 1)})),
      },
    ];
  });

  const histogramDomain = $derived.by((): [number, number] | null => {
    const h = hedgeOk;
    if (!h) return null;
    const lo = Math.min(...Array.from(h.pnls));
    const hi = Math.max(...Array.from(h.pnls));
    const pad = (hi - lo) * 0.05 || 1;
    return [lo - pad, hi + pad];
  });
</script>

{#snippet modelControls()}
  <i>a</i> = <DraggableNumber bind:value={p.meanReversion} min={0.005} max={0.5} step={0.005} label="mean reversion speed a" format={(v) => fmt(v, 3)} />
  and σ = <DraggableNumber bind:value={p.sigma} min={0.002} max={0.03} step={0.0005} label="short-rate volatility sigma" format={(v) => bp(v, 0)} />
{/snippet}

{#snippet quarterControl()}
  <DraggableNumber bind:value={p.start} min={0.25} max={9.75} step={0.25} label="start of the contract's quarter, in years" format={yearsFmt} />
{/snippet}

<article class="essay">
  <header>
    <p class="kicker">smile-explorer · interactive notes</p>
    <h1>Futures, forwards and convexity</h1>
    <p class="lede">
      A SOFR future and a SOFR FRA on the same quarter settle on the same
      number. Both are cleared, and both are margined every day. Yet the
      futures rate is higher than the FRA's. This essay explains why
      (collateral is part of the answer, but not the part people usually
      expect), how big the gap is, and how a desk that trades one against the
      other earns it back.
      <span class="hint">Drag any <span class="sample">underlined number</span>.</span>
    </p>
  </header>

  {#if today.error || treeOutcome.error || strip.error || marginOutcome.error}
    <div class="error sticky" role="alert">
      <strong>The engine rejected these parameters:</strong>
      {today.error ?? treeOutcome.error ?? strip.error ?? marginOutcome.error}.
    </div>
  {/if}

  <section>
    <h2>1. Two contracts on one rate</h2>
    <p>
      Since LIBOR ended, the US dollar short end has been built on SOFR, the
      overnight rate on Treasury repo. CME's three-month SOFR future settles
      on SOFR compounded daily over an "IMM quarter", between the third
      Wednesdays of consecutive quarter months. Its price is 100 minus that
      rate, and on its $1mm notional it pays $25 per basis point. Its
      over-the-counter twin is a SOFR FRA (in practice, a one-period OIS swap
      on the same IMM dates). It pays notional × ¼ × (K − R) at the end of
      the quarter, where R is the same compounded rate and K is its fixed rate.
    </p>
    <div class="specs" role="group" aria-label="The two contracts compared">
      <table>
        <thead>
          <tr><th scope="col"></th><th scope="col">3-month SOFR future</th><th scope="col">SOFR FRA (cleared)</th></tr>
        </thead>
        <tbody>
          <tr><th scope="row">settles on</th><td colspan="2" class="span">SOFR compounded over the IMM quarter</td></tr>
          <tr><th scope="row">pays</th><td>daily: $25 × the change in price, in bp</td><td>at quarter end: notional × ¼ × (K − R)</td></tr>
          <tr><th scope="row">daily margin</th><td>variation margin, <em>settled</em>: it's yours, and no interest is paid on it</td><td>variation margin, <em>collateral</em>: whoever holds it pays interest on it at SOFR (PAI)</td></tr>
          <tr><th scope="row">counterparty</th><td>the clearing house (CME)</td><td>the clearing house (CME or LCH), or a CSA</td></tr>
          <tr><th scope="row">fair rate today</th><td>E<sup>Q</sup>[R]: the risk-neutral average</td><td>E<sup><i>T</i></sup>[R]: the forward rate</td></tr>
        </tbody>
      </table>
    </div>
    <p>
      Both counterparties are clearing houses, so credit is not what
      separates them. What does is the last row. Q is the risk-neutral measure
      whose numeraire is a bank account accruing at SOFR, and <i>T</i> is the
      forward measure for the quarter's end. Take the quarter starting in
      {@render quarterControl()}, in a Hull–White model of SOFR with
      {@render modelControls()} (the parameters the
      <a href="#/swaptions">calibration essay</a> fitted to swaptions, on the
      same curve):
    </p>
    {#if today.value}
      <p class="stat">
        Forward rate <strong>{rateFmt(forward)}</strong> · futures rate
        <strong>{rateFmt(futures)}</strong> (price {fmt(100 - futures * 100, 4)}) ·
        convexity adjustment <strong>{bp(adjustment)}</strong>
      </p>
    {/if}
    <p>
      The adjustment is the futures rate less the forward rate. It's always
      positive, and it grows quickly with the time to expiry. The rest of this
      essay is about where it comes from.
    </p>
  </section>

  <section>
    <h2>2. Does collateral make them the same?</h2>
    <p>
      The textbook story says a future is margined daily while a forward
      settles once, at the end. A cleared FRA is margined daily too. Every
      evening the clearing house moves variation margin equal to the change
      in its value. So is "settles at the end" a fiction?
    </p>
    <p>
      It isn't, because of the interest on that margin. Whoever holds the
      margin pays the overnight rate on it to whoever posted it. Clearing
      houses call this price alignment interest (PAI), charged at SOFR in
      dollars since October 2020 and at €STR in euros. Under a bilateral CSA,
      the same role is played by the interest on cash collateral. With
      interest charged, margin is a loan, repaid when the trade ends.
      Margining moves cash early, but it doesn't change what the FRA is worth.
    </p>
    <p>
      Follow one simulated path (number
      <DraggableNumber bind:value={p.pathNumber} min={1} max={500} step={1} label="path number" format={(v) => fmt(v, 0)} />)
      of a $1mm FRA, receiving fixed at the forward rate, with its margin
      <ToggleToken bind:value={p.pai} on="carrying PAI" off="carrying no PAI" label="interest on the FRA's margin" />.
      The margin, with interest, sits in a cash account that also earns SOFR:
    </p>
    {#if marginPath}
      <figure>
        <LineChart
          ariaLabel="The FRA's mark-to-market value along one path, and the cash its margin has delivered"
          series={marginSeries}
          xFormat={yearsFmt}
          yFormat={(v) => usd(v)}
          xLabel="years"
          markers={[{x: p.start, label: 'quarter starts'}]}
          height={240}
        />
        <figcaption>
          {#if p.pai}
            With PAI, the cash account is the FRA's value, every day. At the
            end it is {usd(marginPath.cashWith)}: exactly the payoff,
            $1mm × ¼ × ({pct(forward, 3)} − {pct(marginPath.settlement, 3)}) =
            {usd(marginPath.payoff)}.
          {:else}
            Without PAI, the cash drifts away from the value. At the end it
            is {usd(marginPath.cashWithout)} against a payoff of
            {usd(marginPath.payoff)}, a difference of
            {usd(marginPath.cashWithout - marginPath.payoff)} on this path.
          {/if}
          Try other paths, and switch the PAI.
        </figcaption>
      </figure>
      <figure>
        <LineChart
          ariaLabel="Cash from the margin less the FRA's value, with and without PAI"
          series={[
            {name: 'with PAI', color: 'var(--series-1)', points: marginPath.gapWith},
            {name: 'without PAI', color: 'var(--series-2)', points: marginPath.gapWithout},
          ]}
          xFormat={yearsFmt}
          yFormat={(v) => usd(v)}
          xLabel="years"
          markers={[{x: p.start, label: 'quarter starts'}]}
          height={180}
        />
        <figcaption>
          The same path, zoomed in: cash less the FRA's value. With PAI the
          gap is zero throughout; without it, it wanders, and on average it
          ends below zero.
        </figcaption>
      </figure>
    {/if}
    <p>
      Without PAI, the cash account no longer tracks the trade. The gap is
      systematic, and its sign follows from who holds the margin. The
      receiver of fixed is owed margin when rates have fallen. It then holds
      collateral and pays PAI at a low rate. When rates have risen it has
      posted collateral and receives PAI at a high rate. So PAI is worth
      something to the receiver, and removing it costs the receiver that
      value.
    </p>
    {#if hedgeOk}
      <p class="stat">
        Value of the PAI to the FRA's receiver, averaged over 2,000 paths:
        <strong>{usd(-hedgeOk.pai.mean)}</strong>
        <span class="muted">± {usd(hedgeOk.pai.se)}</span> · the convexity
        adjustment's value, $1mm × ¼ × {bp(hedgeOk.futuresRate - hedgeOk.forwardRate)} × P(0, end):
        <strong>{usd(hedgeOk.adjustmentValue)}</strong>
      </p>
    {:else if running}
      <p class="stat muted">Simulating…</p>
    {/if}
    <p>
      The two agree to within the simulation's noise. (They need not agree
      exactly: a future's margin follows the undiscounted rate, and the FRA's
      follows its present value. But the difference is small.) In one sentence: <strong>a future is a collateralized forward
      with the interest on its margin switched off</strong>. Its variation
      margin is settled, not lent, and a long future receives it just when
      rates are low, so it reinvests at low rates, and pays it just when they
      are high. The futures price is lower to compensate, which makes the
      futures rate higher.
    </p>
    <p class="aside">
      Some clearing houses have moved swaps to "settled-to-market", where
      variation margin is legally a settlement payment. They kept an
      equivalent charge, the price alignment amount, so the economics are
      unchanged. Bilateral CSAs can break this: collateral that earns less
      than the overnight rate, thresholds, or a choice of collateral currency
      each leave a trade only partly collateralized. Initial margin is posted
      on both contracts. Its funding cost (MVA) is similar for the two and is
      ignored here.
    </p>
  </section>

  <section>
    <h2>3. The same tree, one difference</h2>
    <p>
      The <a href="#/hull-white">Hull–White tree</a> makes this concrete. Its
      branch probabilities are risk-neutral, with the SOFR bank account as
      numeraire, so both contracts can be priced on it by backward induction.
      The only difference is the rule applied at each step.
    </p>
    <ul>
      <li>
        <strong>The FRA</strong> (collateralized at SOFR) is worth the
        discounted expectation of its payoff, like any collateralized
        trade. Its fair rate at a node is a ratio of two bond prices rolled
        back on the tree, (P(<i>t</i>, start) / P(<i>t</i>, end) − 1) / ¼,
        each discounted at every step.
      </li>
      <li>
        <strong>The future</strong> rolls back with no discounting at all,
        because its margin carries no interest. Its rate is the expected
        compounding: (E[e<sup>∫r</sup>] − 1) / ¼, with each step's expectation
        multiplied by e<sup>+<i>r</i>Δt</sup> inside the quarter and by
        nothing before it.
      </li>
    </ul>
    <p>
      Inside the quarter, the FRA's compounding and discounting cancel. The
      future's compounding isn't offset by anything, and the expectation of a
      convex function of the rate exceeds the function of its expectation.
      The tree below has steps of 1/<DraggableNumber bind:value={p.treeStepsPerYear} min={4} max={24} step={4} label="tree steps per year" /> of a year, and each node is
      shaded by the gap between the two rates there:
    </p>
    {#if futuresTree && nodeGaps}
      <figure>
        <TrinomialTreeView tree={futuresTree.tree} yMode="rate" values={nodeGaps} lastSlice={futuresTree.endIndex} bind:hovered={treeHover} />
        <div class="panel" aria-live="polite">
          <div class="head">
            Step {treeSliceOf(treeNode)} · t = {level(futuresTree.tree.times[treeSliceOf(treeNode)])}y · short rate
            {rateFmt(futuresTree.tree.shortRates[treeNode])}
          </div>
          {#if Number.isFinite(futuresTree.futuresRates[treeNode])}
            <div>
              futures {rateFmt(futuresTree.futuresRates[treeNode])} − forward {rateFmt(futuresTree.forwardRates[treeNode])} =
              <strong>{bp(nodeGaps[treeNode])}</strong>
            </div>
          {:else}
            <div class="muted">Inside the quarter: the rate is partly fixed, and the gap depends on the path so far.</div>
          {/if}
        </div>
        <figcaption>
          Nodes up to the start of the quarter, shaded by futures rate minus
          forward rate (darker is larger); nodes inside the quarter are
          faded. Hover a node. The gap shrinks towards the start, where only
          the quarter's own variance is left (a tiny amount, and none on a
          tree whose steps are a quarter long), and it barely depends on the
          level of rates.
        </figcaption>
      </figure>
      <p class="stat">
        On the tree: forward {rateFmt(futuresTree.forwardRate)}, futures {rateFmt(futuresTree.futuresRate)}, adjustment
        <strong>{bp(futuresTree.futuresRate - futuresTree.forwardRate)}</strong>
        <span class="muted">· closed form: {bp(futuresTree.analyticFuturesRate - futuresTree.analyticForwardRate)}</span>
      </p>
      <p>
        The tree's forward rate is the curve's, whatever <i>a</i> and σ are,
        because the tree was fitted to reprice bonds. Its futures rate
        depends on both, and as the steps shrink it converges to the closed
        form. In the Hull–White model the integral of <i>r</i> over any
        interval is Gaussian, so E[e<sup>∫r</sup>] is an exponential of its
        mean and variance.
      </p>
    {/if}
  </section>

  <section>
    <h2>4. How big?</h2>
    <p>
      Across the strip of quarterly contracts out to ten years, with
      {@render modelControls()}:
    </p>
    {#if strip.value}
      <figure>
        <LineChart
          ariaLabel="Convexity adjustment by contract start, in basis points"
          series={[
            {name: 'SOFR future (compounded in arrears)', color: 'var(--series-1)', points: strip.value.sofr},
            {name: 'a term-rate future (fixed at the start, like Eurodollars)', color: 'var(--series-1)', dashed: true, points: strip.value.term},
            {name: 'SOFR future, with no mean reversion', color: 'var(--series-2)', dashed: true, points: strip.value.noReversion},
          ]}
          xFormat={yearsFmt}
          yFormat={(v) => `${fmt(v, 1)}bp`}
          xLabel="start of the contract's quarter"
          markers={[{x: p.start, label: 'chosen'}]}
          height={260}
        />
        <figcaption>
          Futures rate less forward rate. Two years out it's
          {bp(strip.value.at2)}, five years out {bp(strip.value.at5, 1)}, and
          ten years out {bp(strip.value.at10, 1)}
          ({bp(strip.value.flat10, 1)} with no mean reversion).
        </figcaption>
      </figure>
    {/if}
    <p>
      Three things set its size:
    </p>
    <ul>
      <li>
        <strong>Volatility, squared.</strong> Double σ and the adjustment
        quadruples. Without mean reversion it is about
        ½σ²<i>T</i><sub>start</sub><i>T</i><sub>end</sub> (here
        {bp(ruleOfThumb)}), the familiar rule of thumb.
      </li>
      <li>
        <strong>Time, squared.</strong> Both the variance of the rate and
        the time over which margin flows are reinvested grow with expiry.
        Small at the front, the adjustment is tens of basis points at the
        back of the strip, which is why it matters most when long-dated
        futures are used to build a curve.
      </li>
      <li>
        <strong>Mean reversion</strong> damps long-dated rate variance, and
        with it the back of the strip. That makes <i>a</i>, which caps and
        swaptions pin down only loosely, the parameter desks argue about.
      </li>
    </ul>
    <p>
      A SOFR future's rate is compounded <em>in arrears</em>, through the
      quarter, so the quarter's own variance adds to the adjustment over a
      term rate fixed at its start (the old Eurodollar design, and Euribor's
      today). The gap is small, about σ²τ²/3 for a quarter τ. Once the quarter
      has started, the part already fixed has no variance left, and the
      front contract's adjustment runs off to zero by its last day.
    </p>
  </section>

  <section>
    <h2>5. Capturing it: the hedge</h2>
    <p>
      The adjustment isn't free money. It is the price of a position.
      Receive fixed on the FRA at the forward rate, and sell futures against
      it at the futures rate. Since the futures rate is higher, the futures
      are sold "cheap", and the position starts out paying away the
      adjustment. It earns that back through convexity, provided it is
      hedged correctly:
    </p>
    <ul>
      <li>
        <strong>Tailing.</strong> A future pays its margin today, and the FRA
        pays at the quarter's end. A basis point on the future is therefore
        worth more than a basis point on the FRA, so the hedge is fewer
        futures than FRAs: about P(<i>t</i>, end) per FRA, rising to one at
        the end.
      </li>
      <li>
        <strong>Rebalancing.</strong> The tail moves with rates, so the
        hedge has to move too. Each day, after rates rise the short futures
        have gained margin, which is reinvested at the higher rate. After
        rates fall, the losses are funded at the lower rate. The position is
        long convexity, and those small gains are what pay for the
        adjustment.
      </li>
    </ul>
    <p>
      The simulation runs 2,000 daily paths with realized volatility
      σ<sub>realized</sub> =
      <DraggableNumber bind:value={p.realizedSigma} min={0.002} max={0.03} step={0.0005} label="realized volatility" format={(v) => bp(v, 0)} />
      (the futures are priced with σ<sub>implied</sub> = {bp(p.sigma, 0)}).
      The FRA is struck at
      <ToggleToken bind:value={p.fixedAtFuturesRate} on="the futures rate" off="the forward rate" label="the FRA's fixed rate" />,
      and the hedge is:
    </p>
    <div class="styles" role="radiogroup" aria-label="Hedge style">
      {#each HEDGE_STYLES as style (style.id)}
        <button
          type="button"
          role="radio"
          aria-checked={p.hedgeStyle === style.id}
          class:active={p.hedgeStyle === style.id}
          onclick={() => (p.hedgeStyle = style.id)}>{style.name}</button
        >
      {/each}
    </div>

    {#if hedgeError}
      <div class="error" role="alert"><strong>The simulation failed:</strong> {hedgeError}.</div>
    {/if}
    {#if hedgeOk && histogramDomain}
      <figure class:stale={running}>
        <Histogram
          samples={hedgeOk.pnls}
          domain={histogramDomain}
          xLabel="P&L per $1mm FRA, valued today"
          xFormat={(v) => usd(v)}
          markers={[{x: hedgeOk.pnl.mean, label: `mean ${usd(hedgeOk.pnl.mean)}`}]}
          ariaLabel="Distribution of the hedged position's P&L over 2,000 paths"
        />
        <figcaption>
          Each path's P&L (the FRA's payoff and the futures' margin, both with
          interest at SOFR), discounted to today along the path. Mean
          {usd(hedgeOk.pnl.mean, 2)} ± {usd(hedgeOk.pnl.se, 2)}, standard deviation
          {usd(hedgeOk.pnl.sd)}.
          {#if p.fixedAtFuturesRate}
            Struck at the futures rate, the FRA starts out worth
            {usd(hedgeOk.fraValue)} to its receiver: the adjustment's value,
            given away by whoever paid fixed at a rate read straight off the
            futures.
          {/if}
        </figcaption>
      </figure>

      <div class="styletable" role="group" aria-label="Every hedge style compared">
        <table>
          <thead><tr><th scope="col">hedge</th><th scope="col">mean P&L</th><th scope="col">standard deviation</th></tr></thead>
          <tbody>
            {#each hedgeOk.styles as s (s.style)}
              <tr class:current={s.style === p.hedgeStyle}>
                <th scope="row">{HEDGE_STYLES[s.style].name}</th>
                <td>{usd(s.mean)}</td>
                <td>{usd(s.sd)}</td>
              </tr>
            {/each}
          </tbody>
        </table>
      </div>
      <p>
        With realized vol equal to implied, the tailed, daily hedge breaks
        even, with a spread of a few dollars from rebalancing only once a
        day. The other hedges average about zero too, since the futures are
        fairly priced, but they leave far more rate risk. An untailed or
        never-rebalanced hedge leaves hundreds of dollars, and no hedge
        leaves thousands. The tail is what turns the position into a pure
        convexity trade. Its hedge ratio along a dozen paths:
      </p>
      <figure>
        <LineChart
          ariaLabel="Futures sold per FRA along recorded paths, against the forward discount factor"
          series={ratioSeries}
          xFormat={yearsFmt}
          yFormat={(v) => fmt(v, 3)}
          xLabel="years"
          height={220}
        />
        <figcaption>
          Futures sold per FRA. It starts at the forward discount factor
          and ends at one. In between it wanders with the FRA's value: once
          the FRA is in or out of the money, that value is itself a payment
          due at the quarter's end, with rate risk of its own to hedge. The
          daily rebalancing follows these moves.
        </figcaption>
      </figure>

      <h3>Convexity is a volatility position</h3>
      <p>
        The futures were priced with σ<sub>implied</sub>. If rates then move
        more than that, the rebalancing gains are larger than the adjustment
        paid; if less, smaller. Sweeping the realized vol:
      </p>
      <figure>
        <LineChart
          ariaLabel="Average hedged P&L against realized volatility"
          series={sweepSeries}
          xFormat={(v) => `${fmt(v, 0)}bp`}
          yFormat={(v) => usd(v)}
          xLabel="realized volatility σ (implied: {bp(p.sigma, 0)})"
          markers={[{x: p.sigma * 1e4, label: 'implied'}]}
          height={220}
        />
        <figcaption>
          The adjustment is proportional to σ², so the average P&L is its
          value scaled by σ²<sub>realized</sub>/σ²<sub>implied</sub> − 1. In
          this one-factor Gaussian model the position's convexity doesn't
          depend on where rates go, so the P&L hardly varies from path to
          path. In markets, realized vol is itself uncertain, and this is the
          risk the trade carries.
        </figcaption>
      </figure>
    {:else if running}
      <p class="muted">Simulating…</p>
    {/if}
  </section>

  <section>
    <h2>6. On the desk</h2>
    <ul>
      <li>
        <strong>Curves.</strong> SOFR futures are the most liquid
        instruments at the front of the curve, and curve builders use them
        alongside OIS swaps. A futures rate can't be used as a forward rate
        without first subtracting the adjustment. A curve built from
        unadjusted futures has forwards that are too high, by more the
        further out it goes.
      </li>
      <li>
        <strong>Calibration.</strong> The adjustment comes from a model,
        usually Hull–White with σ calibrated to caps or swaptions and <i>a</i>
        set or fitted, as in the <a href="#/swaptions">calibration essay</a>.
        Desks also read the market's own adjustment from the spread between
        futures and IMM-dated OIS swaps. That spread also carries supply and
        demand, balance-sheet costs and margin differences, so it is not pure
        convexity.
      </li>
      <li>
        <strong>Model risk.</strong> One factor means every rate moves
        together. A Gaussian model has no smile. And the long end hinges on
        mean reversion. Desks run the trade as a long-dated vol position, and
        hedge it with options when the mispricing is large.
      </li>
      <li>
        <strong>Euro FRAs.</strong> Euribor FRAs still trade, and they settle
        at the <em>start</em> of the period, discounted at the fixing. That
        timing mismatch is a convexity adjustment of its own, much smaller.
        Euribor is a term rate with a basis to €STR discounting, so a second
        curve is needed as well.
      </li>
    </ul>
    <p>
      The same logic applies wherever a rate is paid at an unnatural time
      or in an unnatural currency. The largest example is a constant-maturity
      swap rate paid each quarter, whose adjustment depends on the whole
      swaption smile. That calls for a model with a smile, and is a subject
      for a later essay.
    </p>
  </section>

  <footer>
    Built with <a href="https://github.com/phaedon/smile-explorer">smile-explorer</a>, on the Hull–White model of the
    <a href="#/hull-white">interest rate essay</a>. See V. Piterbarg, "Funding beyond discounting: collateral
    agreements and derivatives pricing", <i>Risk</i> (2010), for futures and collateralized prices; F. Mercurio, "A
    simple multi-curve model for pricing SOFR futures and other derivatives" (2018), for SOFR futures in Hull–White; and
    Hull's <i>Options, Futures, and Other Derivatives</i> for the classic futures–forward adjustment.
  </footer>
</article>

<style>
  .specs,
  .styletable {
    overflow-x: auto;
    margin: 12px 0 16px;
  }
  table {
    border-collapse: collapse;
    font: 14px/1.45 var(--font-ui);
    font-variant-numeric: tabular-nums;
  }
  .specs table {
    min-width: 560px;
  }
  th,
  td {
    padding: 6px 10px;
    vertical-align: top;
    text-align: left;
    border-bottom: 1px solid var(--border);
  }
  thead th {
    color: var(--text-secondary);
    font-weight: 500;
  }
  tbody th {
    font-weight: 500;
    white-space: nowrap;
  }
  td.span {
    text-align: center;
  }
  .styletable td {
    text-align: right;
  }
  .styletable tr.current th,
  .styletable tr.current td {
    color: var(--accent);
    font-weight: 600;
  }
  .styles {
    display: flex;
    flex-wrap: wrap;
    gap: 8px;
    margin: 4px 0 8px;
  }
  .styles button {
    font: 14px var(--font-ui);
    padding: 6px 12px;
    border-radius: 999px;
    border: 1px solid var(--border);
    background: var(--surface-raised);
    color: var(--text-primary);
    cursor: pointer;
  }
  .styles button.active {
    border-color: var(--accent);
    color: var(--accent);
    font-weight: 600;
  }
  .styles button:focus-visible {
    outline: 2px solid var(--accent);
    outline-offset: 2px;
  }
  .aside {
    font-size: 16px;
    color: var(--text-secondary);
    border-left: 3px solid var(--border);
    padding-left: 14px;
  }
  .muted {
    color: var(--text-secondary);
  }
  .stale {
    opacity: 0.6;
    transition: opacity 0.2s;
  }
  h3 {
    font-size: 19px;
    margin: 36px 0 8px;
  }
  .panel {
    font-family: var(--font-code);
    font-size: 13.5px;
    line-height: 1.6;
    background: var(--surface-raised);
    border: 1px solid var(--border);
    border-radius: 8px;
    padding: 10px 14px;
    font-variant-numeric: tabular-nums;
    overflow-x: auto;
    margin-top: 6px;
  }
  .panel .head {
    font-family: var(--font-ui);
    color: var(--text-secondary);
    font-size: 13px;
    margin-bottom: 4px;
  }
  .panel .muted {
    color: var(--text-secondary);
  }
</style>
