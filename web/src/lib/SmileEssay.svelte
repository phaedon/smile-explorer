<!--
  "The smile, calibrated": the capstone. From quoted implied vols, to a smooth
  arbitrage-free surface (SSVI), to Dupire local vol, to a tree that
  reprices the quotes.
-->
<script lang="ts">
  import {onDestroy} from 'svelte';

  import DraggableNumber from './DraggableNumber.svelte';
  import Heatmap from './Heatmap.svelte';
  import LineChart from './LineChart.svelte';
  import Surface3D from './Surface3D.svelte';
  import ToggleToken from './ToggleToken.svelte';
  import TreeView from './TreeView.svelte';
  import {engine, tryPrice} from './engine';
  import {fmt, level, pct} from './format';
  import type {SmileJob, SmileReply} from './smileCalibrationWorker';
  import {SMILE_EXPIRIES, SMILE_STRIKES, quotesFor, smileParams as p} from './smileParams.svelte';

  const volFmt = (v: number) => pct(v, 1);
  const bp = (v: number) => `${fmt(Math.abs(v) < 5e-6 ? 0 : v * 1e4, 1)}bp`;
  const expiryLabel = (t: number) => (t < 1 ? `${level(t * 12)}m` : `${level(t)}y`);

  // The grids for the surfaces, and the strikes for the densities.
  const GRID_STRIKES = Array.from({length: 41}, (_, k) => 60 + 2 * k);
  const GRID_TIMES = Array.from({length: 40}, (_, k) => Number((0.05 * (k + 1)).toFixed(10)));
  const DENSITY_STRIKES = Array.from({length: 161}, (_, k) => 40 + k);
  const CONVERGENCE_STEPS = [20, 40, 100, 200];

  const quotes = $derived(quotesFor(p));
  const sliceT = $derived(SMILE_EXPIRIES[p.slice]);

  const worker = new Worker(new URL('./smileCalibrationWorker.ts', import.meta.url), {type: 'module'});
  let jobId = 0;
  let reply = $state<SmileReply | null>(null);
  worker.onmessage = (e: MessageEvent<SmileReply>) => {
    if (e.data.id === jobId) reply = e.data;
  };
  worker.onerror = (e) => {
    reply = {id: jobId, error: `the calibration failed to load (${e.message || 'worker error'})`};
  };
  $effect(() => {
    const job: SmileJob = {
      id: ++jobId,
      quotes,
      gridStrikes: GRID_STRIKES,
      gridTimes: GRID_TIMES,
      densityStrikes: DENSITY_STRIKES,
      stepsPerYear: p.stepsPerYear,
      convergenceSteps: CONVERGENCE_STEPS,
    };
    worker.postMessage(job);
  });
  onDestroy(() => worker.terminate());

  type Ok = Extract<SmileReply, {error?: undefined}>;
  let lastOk: Ok | null = null;
  const result = $derived.by(() => {
    if (reply && reply.error === undefined) lastOk = reply as Ok;
    return lastOk;
  });
  const error = $derived(reply?.error ?? null);

  const n = SMILE_STRIKES.length;
  const quotePoints = $derived(
    SMILE_EXPIRIES.flatMap((t, i) => SMILE_STRIKES.map((k, j) => ({x: k, y: t, z: quotes.vols[i * n + j]}))),
  );
  const sliceSeries = $derived.by(() => {
    if (!result) return null;
    const i = p.slice;
    return {
      quotes: SMILE_STRIKES.map((k, j) => ({x: k, y: quotes.vols[i * n + j]})),
      fit: SMILE_STRIKES.map((k, j) => ({x: k, y: result.fit.fittedVols[i * n + j]})),
      residuals: SMILE_STRIKES.map((k, j) => ({x: k, y: (result.fit.fittedVols[i * n + j] - quotes.vols[i * n + j]) * 1e4})),
      tree: SMILE_STRIKES.map((k, j) => ({x: k, y: result.repriceResiduals[i * n + j] * 1e4})),
    };
  });

  // Implied and local vol across strikes at the selected expiry, from the
  // grids.
  const timeIndex = $derived(GRID_TIMES.findIndex((t) => Math.abs(t - sliceT) < 1e-9));
  const gridSlices = $derived.by(() => {
    if (!result || timeIndex < 0) return null;
    const row = timeIndex * GRID_STRIKES.length;
    const implied = GRID_STRIKES.map((k, j) => ({x: k, y: result.impliedGrid[row + j]}));
    const local = GRID_STRIKES.map((k, j) => ({x: k, y: result.localGrid[row + j]}));
    // Slopes either side of spot (100 is GRID_STRIKES[20]).
    const slope = (pts: {y: number}[]) => (pts[21].y - pts[19].y) / 4;
    return {implied, local, ratio: slope(local) / slope(implied)};
  });

  const density = $derived(
    result ? DENSITY_STRIKES.map((k, j) => ({x: k, y: result.densities[p.slice][j]})) : null,
  );

  // The calibrated tree, through priceVanilla.
  const treeOutcome = $derived.by(() => {
    if (!result) return null;
    return tryPrice({
      tree: {
        model: engine.TreeModel.LocalVolatility,
        spot: p.spot,
        vol: {
          model: engine.VolModel.SsviLocalVol,
          vol: 0.2,
          termMaturities: SMILE_EXPIRIES,
          termVols: result.fit.thetas.map((theta, i) => Math.sqrt(theta / SMILE_EXPIRIES[i])),
          smileRange: 0,
          smileStretch: 0,
          ssviRho: result.fit.rho,
          ssviEta: result.fit.eta,
          ssviGamma: result.fit.gamma,
        },
        rate: p.rate,
        drift: 0,
        dtYears: 1 / 20,
      },
      option: {strike: 90, expiryYears: 1, isCall: false, isAmerican: false},
    });
  });

  function steepen() {
    p.skew = 0.12;
    p.curvature = 0.06;
  }
  function reset() {
    p.atmVols = [0.22, 0.2, 0.19, 0.185];
    p.skew = 0.06;
    p.curvature = 0.02;
    p.noise = true;
  }
</script>

<article class="essay">
  <header>
    <p class="kicker">smile-explorer · interactive notes</p>
    <h1>The smile, calibrated</h1>
    <p class="lede">
      In <a href="#/volatility">an earlier essay</a>, a local volatility was
      chosen and the smile it produced was read off a tree. Practitioners
      need the opposite: the market quotes a smile, and the model has to
      reproduce it. This last essay builds that chain, from quoted implied
      vols to a smooth surface, to the local volatility it implies, to a tree
      that prices every quote back.
      <span class="hint">Drag any <span class="sample">underlined number</span>.</span>
    </p>
  </header>

  {#if error}
    <div class="error sticky" role="alert">
      <strong>The engine rejected these quotes:</strong>
      {error}. Showing the last valid fit.
    </div>
  {/if}

  <section>
    <h2>1. A market smile</h2>
    <p>
      Here are quotes for options on an index at {level(p.spot)}, at
      {SMILE_STRIKES.length} strikes from {SMILE_STRIKES[0]} to
      {SMILE_STRIKES[SMILE_STRIKES.length - 1]} and four expiries, with rates at
      {pct(p.rate, 0)}. They are <strong>illustrative, not market data</strong>:
      at-the-money vols of
      <DraggableNumber bind:value={p.atmVols[0]} min={0.08} max={0.5} step={0.005} label="3-month at-the-money vol" format={volFmt} />,
      <DraggableNumber bind:value={p.atmVols[1]} min={0.08} max={0.5} step={0.005} label="6-month at-the-money vol" format={volFmt} />,
      <DraggableNumber bind:value={p.atmVols[2]} min={0.08} max={0.5} step={0.005} label="1-year at-the-money vol" format={volFmt} />
      and
      <DraggableNumber bind:value={p.atmVols[3]} min={0.08} max={0.5} step={0.005} label="2-year at-the-money vol" format={volFmt} />,
      a skew of
      <DraggableNumber bind:value={p.skew} min={0} max={0.2} step={0.005} label="skew" format={(v) => fmt(v, 3)} />
      and a curvature of
      <DraggableNumber bind:value={p.curvature} min={0} max={0.1} step={0.002} label="curvature" format={(v) => fmt(v, 3)} />
      in standardised moneyness, with
      <ToggleToken bind:value={p.noise} on="a little noise" off="no noise" label="noise in the quotes" />.
      Like an equity index, low strikes trade at higher vols.
    </p>
    <p>
      No single σ prices all of these. Black–Scholes needs a different vol
      for every strike and expiry, a whole surface of them.
    </p>
    {#if result}
      <figure>
        <Surface3D
          xs={GRID_STRIKES}
          ys={GRID_TIMES}
          values={result.impliedGrid}
          points={quotePoints}
          xLabel="strike"
          yLabel="expiry"
          zLabel="implied vol"
          xFormat={(v) => fmt(v, 0)}
          yFormat={(v) => `${fmt(v, 1)}y`}
          zFormat={(v) => pct(v, 0)}
          ariaLabel="Implied volatility over strike and expiry: the fitted surface, with the quotes as points"
        />
        <figcaption>
          The quotes (orange) and the smooth surface fitted to them in the
          next section. Drag to turn it: the skew is steepest at short
          expiries and flattens with time.
        </figcaption>
      </figure>
    {/if}
  </section>

  <section>
    <h2>2. A smooth surface, without arbitrage</h2>
    <p>
      The quotes are points; the model needs a surface, smooth enough to
      differentiate and consistent enough that no combination of options is
      a free lunch. The <em>SSVI</em> surface of Gatheral and Jacquier writes the
      total implied variance <i>w</i> = σ²<i>T</i> at log-moneyness <i>k</i> as
    </p>
    <p class="display">
      <i>w</i> = θ/2 · (1 + ρφ<i>k</i> + √((φ<i>k</i> + ρ)² + 1 − ρ²)), &nbsp; φ = η / θ<sup>γ</sup>
    </p>
    <p>
      θ is the at-the-money total variance at each expiry, taken straight
      from the quotes. That leaves three numbers for the whole surface: ρ
      sets the skew, η the curvature, and γ how quickly the smile flattens
      with expiry. Fitting them to every quote at once, by least squares,
      while keeping the surface inside Gatheral and Jacquier's no-arbitrage
      conditions, gives:
    </p>
    {#if result}
      <p class="stat">
        ρ = <strong>{fmt(result.fit.rho, 3)}</strong>, η = <strong>{fmt(result.fit.eta, 3)}</strong>, γ =
        <strong>{fmt(result.fit.gamma, 3)}</strong>
        <span class="muted"
          >· RMS error {bp(result.fit.rmsError)} · {result.fit.butterflyOk ? 'no' : 'some'} butterfly arbitrage</span
        >
      </p>
      {#if sliceSeries}
        <p>
          The quotes and the fit at
          <DraggableNumber bind:value={p.slice} min={0} max={3} step={1} label="expiry shown" format={(i) => expiryLabel(SMILE_EXPIRIES[i])} />:
        </p>
        <figure>
          <LineChart
            ariaLabel="Quoted and fitted implied vol by strike at the selected expiry"
            series={[
              {name: 'SSVI fit', color: 'var(--series-1)', points: sliceSeries.fit},
              {name: 'quotes', color: 'var(--series-2)', points: sliceSeries.quotes, dotsOnly: true},
            ]}
            xFormat={(v) => fmt(v, 0)}
            yFormat={(v) => pct(v, 1)}
            xLabel="strike"
            markers={[{x: p.spot, label: 'spot'}]}
            height={200}
          />
        </figure>
      {/if}
      <p>
        "No arbitrage" has a concrete meaning. The second derivative of call
        prices in strike is the market's risk-neutral density of where the
        index will end up: it must never be negative, or a butterfly spread
        costs less than nothing for a payoff that is never negative.
      </p>
      {#if density}
        <figure>
          <LineChart
            ariaLabel="Risk-neutral density of the index at the selected expiry, implied by the fitted surface"
            series={[{name: 'risk-neutral density', color: 'var(--series-1)', points: density}]}
            xFormat={(v) => fmt(v, 0)}
            yFormat={(v) => fmt(v, 3)}
            xLabel="index level at expiry"
            markers={[{x: p.spot, label: 'spot'}]}
            height={180}
          />
          <figcaption>
            The density at {expiryLabel(sliceT)}: skewed, with a fatter left tail than a
            lognormal. Now <button class="inline" onclick={steepen}>steepen the quotes</button>:
            the fit is held to the no-arbitrage conditions, so its density stays
            positive, and where the quotes ask for more curvature than that allows, it
            gives up a little accuracy instead. <button class="inline" onclick={reset}>Reset</button>
          </figcaption>
        </figure>
      {/if}
    {/if}
  </section>

  <section>
    <h2>3. From implied vol to local vol</h2>
    <p>
      An implied vol is an average over every path from today to expiry. A
      tree needs the opposite: the vol at each price and time,
      σ(<i>S</i>, <i>t</i>). Dupire showed that a smooth, arbitrage-free surface of
      option prices determines it. With no rates or dividends, local
      variance at a strike and time is twice the rate at which the option's
      value grows with expiry, divided by <i>K</i>² times its curvature in
      strike; rates add one more term. Written in total implied variance,
      everything it needs comes straight from SSVI's derivatives.
    </p>
    {#if result}
      <figure>
        <Surface3D
          xs={GRID_STRIKES}
          ys={GRID_TIMES}
          values={result.localGrid}
          xLabel="index level"
          yLabel="time"
          zLabel="local vol"
          xFormat={(v) => fmt(v, 0)}
          yFormat={(v) => `${fmt(v, 1)}y`}
          zFormat={(v) => pct(v, 0)}
          ariaLabel="Local volatility over index level and time, implied by the fitted surface"
        />
        <figcaption>
          Local vol over index level and time. It is steeper than the implied
          surface, and sharper near today, when there is less time to
          average over.
          {#if result.clamped > 0}
            ({result.clamped} grid points hit the clamp that keeps local vol between 1% and 300%.)
          {/if}
        </figcaption>
      </figure>
      <figure>
        <Heatmap
          xs={GRID_STRIKES}
          ys={GRID_TIMES}
          values={result.localGrid}
          xLabel="index level"
          yLabel="time"
          xFormat={(v) => fmt(v, 0)}
          yFormat={(v) => `${fmt(v, 1)}y`}
          zFormat={(v) => `local vol ${pct(v, 1)}`}
          ariaLabel="Heatmap of local volatility over index level and time"
        />
        <figcaption>
          The same local vol from above: hover to read it. The steps in time
          fall at the quoted expiries: the at-the-money variance is linear
          between them, so the forward variance it implies jumps at each one.
        </figcaption>
      </figure>
      {#if gridSlices}
        <figure>
          <LineChart
            ariaLabel="Implied vol by strike and local vol by index level at the selected expiry"
            series={[
              {name: `implied vol at ${expiryLabel(sliceT)}`, color: 'var(--series-1)', points: gridSlices.implied},
              {name: `local vol at ${expiryLabel(sliceT)}`, color: 'var(--series-2)', points: gridSlices.local},
            ]}
            xFormat={(v) => fmt(v, 0)}
            yFormat={(v) => pct(v, 0)}
            xLabel="strike / index level"
            markers={[{x: p.spot, label: 'spot'}]}
            height={200}
          />
        </figure>
        <p>
          At the money, local vol here changes
          <strong>{fmt(gridSlices.ratio, 2)}×</strong> as fast as implied vol: Derman's
          rule of two again, from <a href="#/volatility">the volatility essay</a>,
          run in reverse. (It is exactly two only for a smile that doesn't change
          with expiry; this one flattens, so the ratio depends on when you look.)
        </p>
      {/if}
    {/if}
  </section>

  <section>
    <h2>4. The calibrated tree</h2>
    <p>
      The local-vol tree from the volatility essay, now driven by this
      σ(<i>S</i>, <i>t</i>), places each node so that every step has the right
      forward and the right local variance. Where a node would land on the
      wrong side of a forward, which steep skews can cause, it is moved as
      Derman and Kani prescribe, so every branching probability stays
      valid.
    </p>
    {#if treeOutcome?.result}
      {@const r = treeOutcome.result}
      <figure>
        <TreeView result={r} mode="asset" />
        <figcaption>
          A year of the calibrated tree, at 20 steps a year. Below spot, where
          local vol is higher, the nodes spread further apart than above it.
        </figcaption>
      </figure>
      <p class="stat">
        A 1-year 90 put on this tree <strong>{fmt(r.price, 3)}</strong>
        <span class="muted"
          >· at its quoted vol of {volFmt(r.bsmVol)}, Black–Scholes gives {fmt(r.bsmPrice, 3)}</span
        >
      </p>
    {:else if treeOutcome?.error}
      <div class="error" role="alert">{treeOutcome.error}</div>
    {/if}
  </section>

  <section>
    <h2>5. Closing the loop</h2>
    <p>
      The test of the whole chain: build one tree with
      <DraggableNumber bind:value={p.stepsPerYear} min={20} max={200} step={20} label="steps per year on the tree" />
      steps a year, price an option at every quoted strike and expiry from its
      Arrow–Debreu prices, and turn each price back into an implied vol.
    </p>
    {#if result && sliceSeries}
      <p class="stat">
        Tree against the fitted surface: RMS error <strong>{bp(result.repriceRms)}</strong>
        <span class="muted">· the fit itself missed the quotes by {bp(result.fit.rmsError)} RMS</span>
      </p>
      <figure>
        <LineChart
          ariaLabel="Errors at the selected expiry, by strike: the fit against the quotes, and the tree against the fit"
          series={[
            {name: 'tree − fit', color: 'var(--series-1)', points: sliceSeries.tree},
            {name: 'fit − quotes', color: 'var(--series-2)', points: sliceSeries.residuals, dotsOnly: true},
          ]}
          xFormat={(v) => fmt(v, 0)}
          yFormat={(v) => `${fmt(v, 0)}bp`}
          xLabel="strike"
          height={190}
        />
        <figcaption>
          Errors at {expiryLabel(sliceT)}, in basis points of vol. The tree's errors are
          largest at short expiries and far strikes, where it has the fewest
          steps and nodes to resolve the option.
        </figcaption>
      </figure>
      <figure>
        <LineChart
          ariaLabel="RMS repricing error against steps per year"
          series={[
            {name: 'tree − fit, RMS', color: 'var(--series-1)', points: result.convergence.map((c) => ({x: c.steps, y: c.rms * 1e4}))},
          ]}
          xFormat={(v) => fmt(v, 0)}
          yFormat={(v) => `${fmt(v, 0)}bp`}
          xLabel="steps per year"
          height={170}
        />
        <figcaption>More steps, smaller errors: the tree converges to the surface.</figcaption>
      </figure>
    {/if}
  </section>

  <section>
    <h2>6. What it's for, and where it stops</h2>
    <p>
      The calibrated tree prices every quoted option correctly, by
      construction, and anything else consistently with them: American
      options, barriers, anything whose value depends on where the index
      goes. That consistency is why local volatility is still a workhorse.
    </p>
    <p>
      It also has a known flaw. Local vol fits today's smile exactly, but
      the smiles it predicts for the future are flatter than markets
      actually produce. Products that depend on future smiles, such as
      forward-starting options or cliquets, can be mispriced, and the hedges
      it implies for vanilla options are known to be off. Stochastic
      volatility models address this, at the cost of trees that are no
      longer one-dimensional.
    </p>
    <p>
      That is a good place to stop. Starting from <a href="#/binomial">a single
      binomial step</a>, these essays have priced options on stocks, currencies
      and bonds, hedged them, and fitted trees to the market's curves and
      smiles, every number computed by the same small C++ library, running in
      your browser.
    </p>
  </section>

  <footer>
    Built with <a href="https://github.com/phaedon/smile-explorer">smile-explorer</a>,
    following Derman's <i>The Volatility Smile</i>, Gatheral's <i>The Volatility
    Surface</i>, and Gatheral &amp; Jacquier, <i>Arbitrage-free SVI volatility
    surfaces</i> (2014).
  </footer>
</article>
