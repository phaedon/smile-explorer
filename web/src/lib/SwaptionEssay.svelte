<!--
  "Calibrating the rate tree": swaptions as options on coupon bonds, caps and
  the term structure of vol, and fitting Hull-White's two parameters to
  quoted swaption vols.
-->
<script lang="ts">
  import {onDestroy} from 'svelte';

  import DraggableNumber from './DraggableNumber.svelte';
  import LineChart from './LineChart.svelte';
  import StepControls from './StepControls.svelte';
  import ToggleToken from './ToggleToken.svelte';
  import TrinomialTreeView from './TrinomialTreeView.svelte';
  import {engine, type SwaptionResult} from './engine';
  import {fmt, level, pct} from './format';
  import type {CalibrationJob, CalibrationReply} from './calibrationWorker';
  import {
    COTERMINAL,
    DEFAULT_VOLS_BP,
    RATE_CURVE,
    capRequestFor,
    swaptionParams as p,
    swaptionRequestFor,
  } from './swaptionParams.svelte';

  function attempt<T>(fn: () => T): {value: T; error?: undefined} | {value?: undefined; error: string} {
    try {
      return {value: fn()};
    } catch (e) {
      return {error: e instanceof Error ? e.message : String(e)};
    }
  }

  // Basis points, without a "-0.0" for tiny negatives.
  const bp = (v: number) => `${fmt(Math.abs(v) < 5e-6 ? 0 : v * 1e4, 1)}bp`;
  const priceFmt = (v: number) => fmt(v, 4);
  const curve = {maturities: RATE_CURVE.maturities, rates: RATE_CURVE.rates};

  // ---- 1-2. The swaption on the tree ---------------------------------------------

  const outcome = $derived(attempt(() => engine.priceSwaption(swaptionRequestFor(p))));
  let lastGood: SwaptionResult | null = null;
  const swaption = $derived.by(() => {
    if (outcome.value) lastGood = outcome.value;
    return outcome.value ?? lastGood;
  });
  const tree = $derived(swaption?.option.tree ?? null);
  const T = $derived(swaption?.option.expiryIndex ?? 0);
  const dt = $derived(1 / p.stepsPerYear);
  const sliceOf = (n: number) => {
    if (!tree) return 0;
    let t = 0;
    while (tree.sliceOffsets[t + 1] <= n) ++t;
    return t;
  };
  const middleOf = (t: number) => (tree ? (tree.sliceOffsets[t] + tree.sliceOffsets[t + 1] - 1) / 2 : 0);

  let revealedFrom = $state(Number.POSITIVE_INFINITY);
  $effect(() => {
    if (revealedFrom > T) revealedFrom = T;
  });
  let hovered = $state<number | null>(null);
  const shownNode = $derived(hovered ?? middleOf(Math.min(revealedFrom, T)));
  const nodeStep = $derived.by(() => {
    if (!tree) return null;
    const n = shownNode;
    return {n, t: sliceOf(n), m: tree.successorMid[n]};
  });

  const STEP_COUNTS = [2, 3, 4, 6, 8, 12, 16, 24, 32];
  const convergence = $derived.by(() => {
    const points: {x: number; y: number}[] = [];
    let analytic = NaN;
    for (const steps of STEP_COUNTS) {
      const r = attempt(() => engine.priceSwaption(swaptionRequestFor(p, steps))).value;
      if (!r) continue;
      points.push({x: steps, y: r.option.price});
      analytic = r.option.analyticPrice;
    }
    return {points, analytic};
  });

  // ---- 3. Caps and the term structure of vol ---------------------------------------

  const CAPLET_EXPIRIES = Array.from({length: 39}, (_, k) => 0.25 * (k + 1));
  const capletQuotes = CAPLET_EXPIRIES.map((expiry) => ({
    type: engine.RateOptionType.Caplet,
    expiry,
    tenor: 0.25,
    normalVol: 0,
  }));
  function capletVols(meanReversion: number) {
    return attempt(() => {
      const vols = engine.modelNormalVols({
        curve,
        params: {meanReversion, sigma: p.sigma},
        quotes: capletQuotes,
        fixedPaymentsPerYear: 1,
      });
      return CAPLET_EXPIRIES.map((x, k) => ({x, y: vols[k]}));
    }).value;
  }
  const volCurves = $derived({current: capletVols(p.meanReversion), noReversion: capletVols(0.0005)});
  const cap = $derived(attempt(() => engine.priceCap(capRequestFor(p))));

  // ---- 4. Calibration ----------------------------------------------------------------

  const worker = new Worker(new URL('./calibrationWorker.ts', import.meta.url), {type: 'module'});
  let jobId = 0;
  let fit = $state<CalibrationReply | null>(null);
  worker.onmessage = (e: MessageEvent<CalibrationReply>) => {
    if (e.data.id === jobId) fit = e.data;
  };
  worker.onerror = (e) => {
    fit = {id: jobId, error: `calibration failed to load (${e.message || 'worker error'})`};
  };
  $effect(() => {
    const job: CalibrationJob = {
      id: ++jobId,
      curve,
      quotes: COTERMINAL.map((q, k) => ({...q, normalVol: p.quotesBp[k] / 1e4})),
    };
    worker.postMessage(job);
  });
  onDestroy(() => worker.terminate());

  type FitOk = Extract<CalibrationReply, {error?: undefined}>;
  const fitOk = $derived(fit && fit.error === undefined ? (fit as FitOk) : null);
  const fitError = $derived(fit?.error ?? null);

  // ---- 5. A Bermudan --------------------------------------------------------------

  const bermudanOutcome = $derived(attempt(() => engine.priceSwaption(swaptionRequestFor(p, p.stepsPerYear, true))));
  let lastBermudan: SwaptionResult | null = null;
  const bermudan = $derived.by(() => {
    if (bermudanOutcome.value) lastBermudan = bermudanOutcome.value;
    return bermudanOutcome.value ?? lastBermudan;
  });
  const bermudanTree = $derived(bermudan?.option.tree ?? null);
  let bermudanFrom = $state(Number.POSITIVE_INFINITY);
  $effect(() => {
    const last = bermudan?.option.expiryIndex ?? 0;
    if (bermudanFrom > last) bermudanFrom = last;
  });
  // Where exercising beats holding on (only on exercise dates, where an
  // exercise value exists).
  const exercisedNodes = $derived(
    bermudan
      ? Array.from(bermudan.option.exerciseValues, (exercise, n) => {
          const hold = bermudan!.option.continuationValues[n];
          return Number.isFinite(exercise) && exercise > 0 && (!Number.isFinite(hold) || exercise > hold + 1e-12);
        })
      : null,
  );
  const bestEuropean = $derived(bermudan ? Math.max(...bermudan.coterminalEuropeanPrices) : NaN);

  function useFit() {
    if (!fitOk) return;
    p.meanReversion = fitOk.meanReversion;
    p.sigma = fitOk.sigma;
  }
  function addHump() {
    p.quotesBp = p.quotesBp.map((v, k) => (k >= 2 && k <= 4 ? v + 8 : v));
  }
  function tiltDown() {
    p.quotesBp = COTERMINAL.map((_, k) => 98 - 2.5 * k);
  }
  function resetQuotes() {
    p.quotesBp = [...DEFAULT_VOLS_BP];
  }
</script>

<article class="essay">
  <header>
    <p class="kicker">smile-explorer · interactive notes</p>
    <h1>Calibrating the rate tree</h1>
    <p class="lede">
      The <a href="#/hull-white">Hull–White tree</a> has two parameters
      beyond the curve it is fitted to: mean reversion <i>a</i> and volatility
      σ. <a href="#/bond-options">Options on bonds</a> depend on both, but
      nobody quotes those. What the market does quote are swaptions and caps.
      This essay prices them on the tree, then turns the problem around and
      chooses <i>a</i> and σ to match their quotes.
      <span class="hint">Drag any <span class="sample">underlined number</span>.</span>
    </p>
  </header>

  {#if outcome.error}
    <div class="error sticky" role="alert">
      <strong>The engine rejected these parameters:</strong>
      {outcome.error}. Showing the last valid tree.
    </div>
  {/if}

  <section>
    <h2>1. Swaps, and the par rate</h2>
    <p>
      A swap exchanges a fixed rate for a floating one on some notional,
      here 100. Take one that starts in
      <DraggableNumber bind:value={p.expiryYears} min={1} max={5} step={1} label="swap start in years" format={(v) => `${v}y`} />
      and runs for
      <DraggableNumber bind:value={p.tenorYears} min={1} max={8} step={1} label="swap length in years" format={(v) => `${v}y`} />,
      paying fixed once a year. On a reset date the floating leg is always
      worth par, since it pays whatever rate the market sets for the next
      period. So the swap is worth par less a bond paying the fixed coupons
      and the notional at the end, and the <em>par rate</em> is the coupon
      that makes that bond worth par.
    </p>
    {#if swaption}
      <p class="display">
        par rate = (P(0, {level(p.expiryYears)}) − P(0, {level(p.expiryYears + p.tenorYears)})) / annuity =
        <strong>{pct(swaption.parRate, 3)}</strong>
      </p>
      <p>
        The annuity, Σ τ<sub><i>i</i></sub> P(0, <i>t</i><sub><i>i</i></sub>) =
        {fmt(swaption.annuity, 4)}, is the value today of 1 a year paid on the
        swap's dates. These numbers come straight from today's curve
        (illustrative, with zero rates from {pct(RATE_CURVE.rates[1], 1)} to
        {pct(RATE_CURVE.rates[RATE_CURVE.rates.length - 1], 1)}); no model is
        needed yet.
      </p>
    {/if}
  </section>

  <section>
    <h2>2. A swaption is a bond option</h2>
    <p>
      A <ToggleToken bind:value={p.isPayer} on="payer" off="receiver" label="swaption type" />
      swaption is the right, at the swap's start, to enter it
      {p.isPayer ? 'paying' : 'receiving'} the fixed rate, here struck at the par
      rate. Since the swap is worth par less the coupon bond, that right is a
      <strong>{p.isPayer ? 'put' : 'call'}</strong> on the bond, struck at par. The
      library already prices options on bonds on the tree, so a swaption is
      just a coupon bond and a strike:
    </p>
    <pre><code>FixedCashflowInstrument bond(&amp;tree);
// Coupons at the par rate each year, and the notional at the end.
bond.setCashflows({'{'}{swaption ? [...swaption.paymentTimes].map((t, k, all) => `{${level(t)}, ${fmt(100 * swaption.fixedRate + (k === all.length - 1 ? 100 : 0), 3)}}`).join(', ') : ''}{'}'});
auto underlying = InterestRateSwap::createBond(bond);
InterestRateDerivative swaption(&amp;tree, &amp;underlying);
swaption.price(VanillaOption(100, OptionPayoff::{p.isPayer ? 'Put' : 'Call'}), {level(p.expiryYears)});  // = {swaption ? priceFmt(swaption.option.price) : '–'}</code></pre>

    {#if swaption && tree && nodeStep}
      <p>
        The tree here has
        <DraggableNumber bind:value={p.stepsPerYear} min={1} max={12} step={1} label="steps per year" />
        steps a year, with <i>a</i> =
        <DraggableNumber bind:value={p.meanReversion} min={0.01} max={0.5} step={0.01} label="mean reversion a" format={(v) => fmt(v, 3)} />
        and σ =
        <DraggableNumber bind:value={p.sigma} min={0.002} max={0.03} step={0.0005} label="short-rate volatility sigma" format={(v) => pct(v, 2)} />.
      </p>
      <figure>
        <StepControls bind:revealedFrom expiryIndex={T} />
        <TrinomialTreeView
          {tree}
          yMode="rate"
          values={swaption.option.optionValues}
          shownFrom={Math.min(revealedFrom, T)}
          lastSlice={T}
          bind:hovered
        />
        <div class="panel" aria-live="polite">
          {#if nodeStep.t > T}
            <div class="head">
              Step {nodeStep.t} · after expiry · bond worth {fmt(tree.bondValues[nodeStep.n], 3)}
            </div>
            <div class="muted">The option has expired; the tree carries on to price the bond.</div>
          {:else}
            {@const n = nodeStep.n}
            <div class="head">
              Step {nodeStep.t} · <i>r</i> = {pct(tree.shortRates[n], 3)} · bond B = {fmt(tree.bondValues[n], 3)}
            </div>
            {#if nodeStep.t < Math.min(revealedFrom, T)}
              <div class="muted">Not computed yet: step the induction back to here.</div>
            {:else if nodeStep.t === T}
              <div>
                V = {p.isPayer ? `max(100 − ${fmt(tree.bondValues[n], 3)}, 0)` : `max(${fmt(tree.bondValues[n], 3)} − 100, 0)`}
                = <strong>{priceFmt(swaption.option.optionValues[n])}</strong>
              </div>
            {:else}
              {@const m = nodeStep.m}
              <div>
                V = e<sup>−{fmt(tree.shortRates[n], 4)}×{fmt(dt, 3)}</sup> × ({fmt(tree.pu[n], 3)} × {priceFmt(swaption.option.optionValues[m + 1])}
                + {fmt(tree.pm[n], 3)} × {priceFmt(swaption.option.optionValues[m])} + {fmt(tree.pd[n], 3)} × {priceFmt(swaption.option.optionValues[m - 1])})
                = <strong>{priceFmt(swaption.option.optionValues[n])}</strong>
              </div>
            {/if}
          {/if}
        </div>
        <figcaption>
          The {p.isPayer ? 'put' : 'call'} on the coupon bond, shaded by value, up to
          the swaption's expiry (the tree itself runs on to the end of the swap,
          to value the bond). A payer swaption pays off where rates are high and
          the bond is cheap.
        </figcaption>
      </figure>

      <p>
        There is a closed form too. Every bond price on the tree falls as the
        short rate rises, so the option is exercised exactly when the rate at
        expiry is on one side of some <i>r</i>*. Jamshidian noticed that this
        splits the option on the coupon bond into a portfolio of options on
        each of its zero-coupon bonds, which the bond-option formula prices.
      </p>
      <p class="stat">
        Tree <strong>{priceFmt(swaption.option.price)}</strong>
        <span class="muted"
          >· Jamshidian {priceFmt(swaption.option.analyticPrice)} · as a normal vol
          {bp(swaption.normalVol)} (closed form {bp(swaption.analyticNormalVol)})</span
        >
      </p>
      {#if convergence.points.length > 1}
        <figure>
          <LineChart
            ariaLabel="Swaption price on the tree against steps per year, converging to Jamshidian's closed form"
            series={[
              {name: 'tree', color: 'var(--series-1)', points: convergence.points},
              {
                name: 'Jamshidian',
                color: 'var(--series-2)',
                points: convergence.points.map((pt) => ({x: pt.x, y: convergence.analytic})),
              },
            ]}
            xFormat={(v) => fmt(v, 0)}
            yFormat={(v) => fmt(v, 3)}
            xLabel="steps per year"
            height={190}
          />
          <figcaption>
            The tree's price converges quickly at first, then settles within a
            small fraction of a percent of the formula.
          </figcaption>
        </figure>
      {/if}
      <p>
        The last number is how swaptions are quoted: not as a price but as a
        <em>normal vol</em>, the volatility of the swap rate, in basis points a
        year, that the Bachelier formula needs to reproduce the price. It
        makes options on different swaps comparable.
      </p>
    {/if}
  </section>

  <section>
    <h2>3. Caps, and how vol depends on expiry</h2>
    <p>
      A cap pays the floating rate above a strike, period by period. Each
      piece, a <em>caplet</em>, is a put on the zero-coupon bond covering its
      period, so the bond-option formula prices it directly.
      {#if cap.value}
        A five-year at-the-money quarterly cap on 100, struck at
        {pct(cap.value.strike, 3)}, is worth <strong>{priceFmt(cap.value.price)}</strong>
        on the tree and {priceFmt(cap.value.analyticPrice)} in closed form.
      {/if}
    </p>
    <p>
      The caplets' normal vols show what the two parameters do. With no
      mean reversion, the short rate is a random walk and every caplet has
      about the same normal vol, roughly σ. Mean reversion pulls rates back, so distant
      rates move less than near ones: the further out the caplet, the lower
      its vol. <i>a</i> sets how fast vol decays with expiry, and σ sets its
      level.
    </p>
    {#if volCurves.current && volCurves.noReversion}
      <figure>
        <LineChart
          ariaLabel="Caplet normal vol against expiry, with and without mean reversion"
          series={[
            {name: `a = ${fmt(p.meanReversion, 3)}`, color: 'var(--series-1)', points: volCurves.current},
            {name: 'no mean reversion', color: 'var(--series-2)', points: volCurves.noReversion},
          ]}
          xFormat={(v) => `${fmt(v, 0)}y`}
          yFormat={(v) => `${fmt(v * 1e4, 0)}bp`}
          xLabel="caplet expiry"
          height={200}
        />
        <figcaption>
          Two parameters give one decaying shape. Real vol curves are often
          humped, rising for the first year or two and then falling, which
          Hull–White can't reproduce.
        </figcaption>
      </figure>
    {/if}
  </section>

  <section>
    <h2>4. Calibration</h2>
    <p>
      Now turn it around. Here are at-the-money normal vols for nine
      <em>co-terminal</em> swaptions, all into swaps that end ten years from
      now. They are <strong>illustrative, not market data</strong>: made up, but
      realistically shaped. Drag any of them, and the calibration finds the
      <i>a</i> and σ that match them best, by least squares on the vols.
    </p>
    {#if fitError}
      <div class="error" role="alert">{fitError}</div>
    {/if}
    <div class="quotes" role="group" aria-label="Swaption quotes">
      <table>
        <thead>
          <tr>
            <th scope="col">swaption</th>
            <th scope="col">quote</th>
            <th scope="col">model</th>
            <th scope="col">error</th>
          </tr>
        </thead>
        <tbody>
          {#each COTERMINAL as q, k (q.expiry)}
            <tr>
              <th scope="row">{q.expiry}y × {q.tenor}y</th>
              <td
                ><DraggableNumber
                  bind:value={p.quotesBp[k]}
                  min={30}
                  max={200}
                  step={0.5}
                  label="{q.expiry} year into {q.tenor} year normal vol in basis points"
                  format={(v) => `${fmt(v, 1)}bp`}
                /></td
              >
              <td>{fitOk ? bp(fitOk.modelVols[k]) : '…'}</td>
              <td>{fitOk ? bp(fitOk.modelVols[k] - p.quotesBp[k] / 1e4) : ''}</td>
            </tr>
          {/each}
        </tbody>
      </table>
    </div>
    <p class="buttons">
      <button class="inline" onclick={addHump}>Add a hump</button>
      <button class="inline" onclick={tiltDown}>Tilt them down</button>
      <button class="inline" onclick={resetQuotes}>Reset</button>
    </p>
    {#if fitOk}
      <p class="stat">
        Fitted <i>a</i> = <strong>{fmt(fitOk.meanReversion, 4)}</strong>, σ =
        <strong>{pct(fitOk.sigma, 3)}</strong>
        <span class="muted">· RMS error {bp(fitOk.rmsError)}</span>
        <button class="inline" onclick={useFit}>Use these in the tree above</button>
      </p>
      {#if !fitOk.converged}
        <p class="note">
          The fit has hit a bound: these quotes want vols that <em>rise</em>
          with expiry, which would need negative mean reversion. The tree here
          requires <i>a</i> &gt; 0, so it settles for the closest it can get.
        </p>
      {/if}
      <figure>
        <LineChart
          ariaLabel="Quoted and fitted normal vols for each co-terminal swaption"
          series={[
            {name: 'Hull–White fit', color: 'var(--series-1)', points: COTERMINAL.map((q, k) => ({x: q.expiry, y: fitOk!.modelVols[k] * 1e4}))},
            {name: 'quotes', color: 'var(--series-2)', points: COTERMINAL.map((q, k) => ({x: q.expiry, y: p.quotesBp[k]})), dotsOnly: true},
          ]}
          xFormat={(v) => `${fmt(v, 0)}y`}
          yFormat={(v) => `${fmt(v, 1)}bp`}
          xLabel="swaption expiry"
          height={200}
        />
        <figcaption>
          Add a hump and the fit splits the difference: two parameters can't
          follow it, so some swaptions come out too cheap and others too dear.
        </figcaption>
      </figure>
    {/if}
  </section>

  <section>
    <h2>5. What calibration is for: a Bermudan</h2>
    <p>
      None of the quoted swaptions needed a model: their prices were given.
      The point of fitting <i>a</i> and σ is what comes next. A tree that
      reprices the curve, and these swaptions as well as two parameters
      allow, can price what isn't quoted. The classic example is a
      <em>Bermudan</em> swaption: the right to enter the swap on any of
      several dates, each time for the rest of its life, at the same fixed
      rate.
    </p>
    {#if bermudan && bermudanTree && swaption}
      <p>
        Take the {p.isPayer ? 'payer' : 'receiver'} swaption from section 2 and
        make it exercisable at {level(p.expiryYears)} years or on any later
        payment date but the last:
        {[...bermudan.exerciseTimes].map((t) => `${level(t)}y`).join(', ')}. Exercising
        on a payment date enters the swap after that date's coupon, so on
        the tree the exercise value is par less the <em>ex-coupon</em> bond.
        Each date on its own would be a European swaption on the rest of the
        swap. These are the co-terminal swaptions, which is why section 4
        calibrates to them.
      </p>
      <pre><code>// Exercise at {level(p.expiryYears)}y, then on each coupon date, excluding that coupon.
std::vector&lt;ExerciseDate&gt; dates = {'{'}{[...bermudan.exerciseTimes].map((t, k) => `{${level(t)}, ${k === 0 ? '0' : fmt(100 * bermudan.fixedRate, 3)}}`).join(', ')}{'}'};
double price = swaption.priceBermudan(VanillaOption(100, OptionPayoff::{p.isPayer ? 'Put' : 'Call'}), dates);  // = {priceFmt(bermudan.option.price)}</code></pre>
      <figure>
        <StepControls bind:revealedFrom={bermudanFrom} expiryIndex={bermudan.option.expiryIndex} />
        <TrinomialTreeView
          tree={bermudanTree}
          yMode="rate"
          values={bermudan.option.optionValues}
          shownFrom={Math.min(bermudanFrom, bermudan.option.expiryIndex)}
          lastSlice={bermudan.option.expiryIndex}
          marked={exercisedNodes}
        />
        <figcaption>
          The Bermudan's value, rolled back from its last exercise date.
          Ringed nodes are where exercising beats waiting: when rates are high
          enough that the swap left is worth more now than the chance of a
          better date later.
        </figcaption>
      </figure>
      <p class="stat">
        Bermudan <strong>{priceFmt(bermudan.option.price)}</strong>
        <span class="muted"
          >· best single date {priceFmt(bestEuropean)} · the right to choose is worth
          {priceFmt(bermudan.option.price - bestEuropean)}</span
        >
      </p>
      <figure>
        <LineChart
          ariaLabel="Each co-terminal European swaption's price, against the Bermudan's"
          series={[
            {
              name: 'Bermudan',
              color: 'var(--series-1)',
              points: [...bermudan.exerciseTimes].map((x) => ({x, y: bermudan!.option.price})),
            },
            {
              name: 'exercisable on that date only',
              color: 'var(--series-2)',
              points: [...bermudan.exerciseTimes].map((x, k) => ({x, y: bermudan!.coterminalEuropeanPrices[k]})),
              dotsOnly: true,
            },
          ]}
          xFormat={(v) => `${fmt(v, 0)}y`}
          yFormat={(v) => fmt(v, 2)}
          xLabel="exercise date"
          height={190}
        />
        <figcaption>
          The Bermudan is worth more than any one of its dates, but less than
          all of them together: it can only be exercised once.
        </figcaption>
      </figure>
      <p>
        Unlike the European, a Bermudan has no closed form, and its price
        depends on how the model moves rates <em>between</em> the exercise
        dates, not just on each date's vol. That is why it matters which
        quotes the model was calibrated to, and why a two-parameter fit that
        misses some of them leaves real uncertainty in the price.
      </p>
    {/if}
  </section>

  <footer>
    Built with <a href="https://github.com/phaedon/smile-explorer">smile-explorer</a>,
    following Hull's <i>Options, Futures, and Other Derivatives</i>.
  </footer>
</article>

<style>
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
    min-height: 5em;
  }
  .panel .head {
    font-family: var(--font-ui);
    color: var(--text-secondary);
    font-size: 13px;
    margin-bottom: 4px;
  }
  .panel .muted {
    color: var(--text-secondary);
    font-family: var(--font-ui);
  }
  .quotes {
    overflow-x: auto;
    margin: 12px 0;
  }
  table {
    border-collapse: collapse;
    font: 14px var(--font-ui);
    font-variant-numeric: tabular-nums;
  }
  th,
  td {
    padding: 3px 12px;
    text-align: right;
    white-space: nowrap;
  }
  thead th {
    color: var(--text-secondary);
    font-weight: 500;
    border-bottom: 1px solid var(--border);
  }
  tbody th {
    text-align: left;
    font-weight: 500;
  }
  .buttons {
    display: flex;
    flex-wrap: wrap;
    gap: 8px;
  }
  .note {
    color: var(--text-secondary);
  }
</style>
