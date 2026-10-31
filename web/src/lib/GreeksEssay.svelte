<!--
  "Greeks and hedging": how an option's price moves, read off the tree and
  from closed forms (including second-order effects), and what happens when
  you actually hedge it.
-->
<script lang="ts">
  import {onDestroy} from 'svelte';

  import DraggableNumber from './DraggableNumber.svelte';
  import Heatmap from './Heatmap.svelte';
  import Histogram from './Histogram.svelte';
  import LineChart from './LineChart.svelte';
  import Surface3D from './Surface3D.svelte';
  import ToggleToken from './ToggleToken.svelte';
  import {engine, nodeIndex, tryPrice, type GreeksResult} from './engine';
  import {fmt, level, pct} from './format';
  import {greeksParams as p, greeksRequestFor} from './greeksParams.svelte';
  import type {HedgeRequest, HedgeResponse} from './hedgeWorker';

  function attempt<T>(fn: () => T): {value: T; error?: undefined} | {value?: undefined; error: string} {
    try {
      return {value: fn()};
    } catch (e) {
      return {error: e instanceof Error ? e.message : String(e)};
    }
  }

  const volFmt = (v: number) => pct(v, 0);
  const yearsFmt = (v: number) => (v >= 1 / 12 - 1e-9 ? `${level(Math.round(v * 120) / 10)}m` : `${fmt(v * 52, 1)}w`);

  type GreekValue = Parameters<typeof engine.greekSurface>[0]['greek'];

  // A closed-form Greek at the given spots and expiries (row by row).
  function surface(greek: GreekValue, spots: number[], expiries: number[], vol = p.vol) {
    return engine.greekSurface({
      greek,
      strike: p.strike,
      vol,
      rate: p.rate,
      isCall: p.isCall,
      spots,
      expiries,
    });
  }
  const G = engine.Greek;

  // ---- 1. Greeks off the tree ---------------------------------------------------

  const priced = $derived(tryPrice(greeksRequestFor(p)));
  const greeks = $derived(attempt(() => engine.computeGreeks(greeksRequestFor(p), 0.01, 0.0001)));
  let lastGood: GreeksResult | null = null;
  const g = $derived.by(() => {
    if (greeks.value) lastGood = greeks.value;
    return greeks.value ?? lastGood;
  });
  const tree = $derived(priced.result ?? null);
  const S = (t: number, i: number) => tree?.assetValues[nodeIndex(t, i)] ?? NaN;
  const V = (t: number, i: number) => tree?.optionValues[nodeIndex(t, i)] ?? NaN;

  // Vega and rho are shown per point (0.01) of vol or rate.
  const rows = $derived(
    g
      ? [
          {name: 'delta Δ', tree: g.tree.delta, exact: g.closedForm.delta, digits: 4},
          {name: 'gamma Γ', tree: g.tree.gamma, exact: g.closedForm.gamma, digits: 4},
          {name: 'theta Θ (per year)', tree: g.tree.theta, exact: g.closedForm.theta, digits: 3},
          {name: 'vega (per vol point)', tree: g.tree.vega / 100, exact: g.closedForm.vega / 100, digits: 4},
          {name: 'rho (per rate point)', tree: g.tree.rho / 100, exact: g.closedForm.rho / 100, digits: 4},
        ]
      : [],
  );

  // ---- 2. Bump sizes ------------------------------------------------------------

  const BUMPS = [0.0001, 0.0003, 0.001, 0.003, 0.01, 0.03, 0.06, 0.1, 0.15];
  const bumpSeries = $derived.by(() => {
    if (p.isAmerican) return null;
    const points = BUMPS.filter((b) => b < p.vol).flatMap((b) => {
      const r = attempt(() => engine.computeGreeks(greeksRequestFor(p), b, 0.0001)).value;
      return r ? [{x: Math.log10(b * 100), y: r.tree.vega / 100, exact: r.closedForm.vega / 100}] : [];
    });
    return points.length ? points : null;
  });

  // ---- 3. Gamma near expiry --------------------------------------------------------

  const SPOTS = $derived(Array.from({length: 41}, (_, k) => p.strike * (0.7 + 0.015 * k)));
  // From a week to six months.
  const EXPIRIES = Array.from({length: 40}, (_, k) => 1 / 52 + (k * (0.5 - 1 / 52)) / 39);
  const gammaGrid = $derived(attempt(() => surface(G.Gamma, SPOTS, EXPIRIES)));
  const gammaSlices = $derived(
    attempt(() => {
      const [quarter, week] = [0.25, 1 / 52];
      const values = surface(G.Gamma, SPOTS, [quarter, week]);
      const n = SPOTS.length;
      return {
        quarter: SPOTS.map((s, j) => ({x: s, y: values[j]})),
        week: SPOTS.map((s, j) => ({x: s, y: values[n + j]})),
      };
    }),
  );
  const atm = $derived(
    attempt(() => {
      const [gQuarter, gWeek, gDay] = surface(G.Gamma, [p.strike], [0.25, 1 / 52, 1 / 365]);
      const [colourWeek] = surface(G.Colour, [p.strike], [1 / 52]);
      return {gQuarter, gWeek, gDay, colourWeek};
    }),
  );

  // ---- 4. Vanna --------------------------------------------------------------------

  const vannaSpots = $derived(Array.from({length: 61}, (_, k) => p.strike * (0.7 + 0.01 * k)));
  const deltaCurves = $derived(
    attempt(() => {
      const low = surface(G.Delta, vannaSpots, [p.expiryYears]);
      const high = surface(G.Delta, vannaSpots, [p.expiryYears], p.vol + 0.1);
      return {
        low: vannaSpots.map((s, k) => ({x: s, y: low[k]})),
        high: vannaSpots.map((s, k) => ({x: s, y: high[k]})),
      };
    }),
  );

  // ---- 5. Hedging ------------------------------------------------------------------

  const SWEEP = [4, 8, 16, 32, 64, 128, 256];
  const PATHS = 3000;
  const hedgeWorker = new Worker(new URL('./hedgeWorker.ts', import.meta.url), {type: 'module'});
  let hedgeId = 0;
  let hedge = $state<HedgeResponse | null>(null);
  hedgeWorker.onmessage = (e: MessageEvent<HedgeResponse>) => {
    if (e.data.id === hedgeId) hedge = e.data;
  };
  $effect(() => {
    const request: HedgeRequest = {
      id: ++hedgeId,
      spot: p.spot,
      strike: p.strike,
      expiryYears: p.expiryYears,
      isCall: p.isCall,
      impliedVol: p.vol,
      realisedVol: p.realisedVol,
      rate: p.rate,
      rebalances: p.rebalances,
      paths: PATHS,
      sweep: SWEEP,
    };
    hedgeWorker.postMessage(request);
  });
  onDestroy(() => hedgeWorker.terminate());

  type HedgeOk = Extract<HedgeResponse, {error?: undefined}>;
  const hedgeOk = $derived(hedge && hedge.error === undefined ? (hedge as HedgeOk) : null);
  const hedgeError = $derived(hedge?.error ?? null);

  // What the vol mismatch should be worth: the option at the implied vol,
  // less the option at the realised vol, grown to expiry.
  const expectedEdge = $derived(
    attempt(() => {
      const [atImplied] = surface(G.Price, [p.spot], [p.expiryYears]);
      const [atRealised] = surface(G.Price, [p.spot], [p.expiryYears], p.realisedVol);
      return Math.exp(p.rate * p.expiryYears) * (atImplied - atRealised);
    }),
  );

  const sweepSeries = $derived.by(() => {
    const h = hedgeOk;
    if (!h) return null;
    const actual = h.sweep.map((s) => ({x: Math.log2(s.rebalances), y: s.stddev}));
    // 1/sqrt(N), anchored at the first point.
    const anchor = h.sweep[0];
    const ideal = h.sweep.map((s) => ({
      x: Math.log2(s.rebalances),
      y: anchor.stddev * Math.sqrt(anchor.rebalances / s.rebalances),
    }));
    return {actual, ideal};
  });

  const pathSeries = $derived.by(() => {
    const h = hedgeOk;
    if (!h) return null;
    const xs = Array.from(h.times);
    return {
      spot: xs.map((t, k) => ({x: t, y: h.spots[k]})),
      theta: xs.map((t, k) => ({x: t, y: h.cumulativeTheta[k]})),
      gamma: xs.map((t, k) => ({x: t, y: h.cumulativeGamma[k]})),
      total: h.cumulativePnl[h.cumulativePnl.length - 1],
    };
  });
</script>

<article class="essay">
  <header>
    <p class="kicker">smile-explorer · interactive notes</p>
    <h1>Greeks and hedging</h1>
    <p class="lede">
      The other essays price an option. Whoever sells it then has a different
      question: how will its value move, and what should they hold against
      it? The answers are the <em>Greeks</em>, the option's sensitivities to
      spot, time, volatility and rates, and the trades they imply.
      <span class="hint">Drag any <span class="sample">underlined number</span>.</span>
    </p>
  </header>

  {#if greeks.error}
    <div class="error sticky" role="alert">
      <strong>The engine rejected these parameters:</strong>
      {greeks.error}. Showing the last valid values.
    </div>
  {/if}

  <section>
    <h2>1. Delta and gamma, read off the tree</h2>
    <p>
      Take a {p.isAmerican ? 'American' : 'European'}
      <ToggleToken bind:value={p.isCall} on="call" off="put" label="option type" />
      struck at
      <DraggableNumber bind:value={p.strike} min={60} max={140} step={1} label="strike" />
      on a stock at
      <DraggableNumber bind:value={p.spot} min={60} max={140} step={1} label="spot" />, expiring in
      <DraggableNumber bind:value={p.expiryYears} min={0.1} max={2} step={0.05} label="expiry in years" format={(v) => `${fmt(v, 2)}y`} />,
      with vol
      <DraggableNumber bind:value={p.vol} min={0.05} max={0.6} step={0.01} label="volatility" format={volFmt} />
      and rates at
      <DraggableNumber bind:value={p.rate} min={0} max={0.1} step={0.005} label="rate" format={(v) => pct(v, 1)} />,
      priced on a tree with
      <DraggableNumber bind:value={p.stepsPerYear} min={4} max={200} step={2} label="steps per year" />
      steps a year.
    </p>
    <p>
      The tree already holds the Greeks. One step in, the option is worth
      V<sub>up</sub> or V<sub>down</sub>, so the shares that match its move,
      the <em>delta</em>, are their difference over the stock's. Two steps in,
      the same at the upper and lower pairs of nodes gives two deltas, and
      their difference is <em>gamma</em>: how fast delta itself changes.
      Theta compares the middle node two steps in, where the stock is back at
      spot, with today.
    </p>
    {#if tree && g}
      <p class="display">
        Δ = (V<sub>up</sub> − V<sub>down</sub>) / (S<sub>up</sub> − S<sub>down</sub>) =
        ({fmt(V(1, 1), 3)} − {fmt(V(1, 0), 3)}) / ({fmt(S(1, 1), 2)} − {fmt(S(1, 0), 2)}) =
        <strong>{fmt(g.tree.delta, 4)}</strong>
      </p>
      <table class="greeks">
        <thead>
          <tr><th scope="col"></th><th scope="col">tree</th><th scope="col">Black–Scholes</th></tr>
        </thead>
        <tbody>
          {#each rows as row (row.name)}
            <tr>
              <th scope="row">{row.name}</th>
              <td>{fmt(row.tree, row.digits)}</td>
              <td>{Number.isFinite(row.exact) ? fmt(row.exact, row.digits) : '–'}</td>
            </tr>
          {/each}
        </tbody>
      </table>
      <p>
        Switch the option to
        <ToggleToken bind:value={p.isAmerican} on="American" off="European" label="exercise style" />.
        Black–Scholes has no formula for an American option, so its column
        empties, but the tree's Greeks work exactly as before. That is one of
        the main reasons trees are still used.
      </p>
    {/if}
  </section>

  <section>
    <h2>2. Vega and rho: bump and reprice</h2>
    <p>
      Volatility and rates aren't on the tree's axes, so their Greeks come
      from repricing: build the tree again with vol a little higher and a
      little lower, and divide the change in price by the change in vol. Both
      trees keep the same steps, so their prices differ only through the vol.
    </p>
    {#if bumpSeries}
      <figure>
        <LineChart
          ariaLabel="Tree vega against the size of the vol bump, with the Black-Scholes vega for reference"
          series={[
            {name: 'tree vega', color: 'var(--series-1)', points: bumpSeries.map(({x, y}) => ({x, y}))},
            {name: 'Black–Scholes vega', color: 'var(--series-2)', points: bumpSeries.map(({x, exact}) => ({x, y: exact}))},
          ]}
          xFormat={(v) => `${level(Number((10 ** v).toPrecision(2)))}`}
          yFormat={(v) => fmt(v, 4)}
          xLabel="bump (vol points, log scale)"
          height={200}
        />
        <figcaption>
          Large bumps measure an average slope over a wide range of vols, not
          the slope here. As the bump shrinks the estimate settles on the
          tree's own vega; what gap remains to the formula is the tree's
          discretisation error, which more steps reduce.
        </figcaption>
      </figure>
    {/if}
  </section>

  <section>
    <h2>3. Gamma near expiry</h2>
    <p>
      Gamma is what makes hedging hard: it is how much the hedge must change
      when the stock moves. For a long-dated option it is a gentle hump around
      the strike. As expiry approaches, the hump narrows and grows. At the
      money it rises like 1/√τ, while away from the strike it falls to zero,
      because the option is then almost surely worth nothing, or almost
      surely the stock less the strike.
    </p>
    {#if gammaGrid.value}
      <figure>
        <Surface3D
          xs={SPOTS}
          ys={EXPIRIES}
          values={gammaGrid.value}
          xLabel="spot"
          yLabel="time to expiry"
          zLabel="gamma"
          xFormat={(v) => fmt(v, 0)}
          yFormat={yearsFmt}
          zFormat={(v) => fmt(v, 2)}
          ariaLabel="Gamma as a 3D surface over spot and time to expiry: a ridge along the strike that rises into a spike near expiry"
        />
        <figcaption>
          Gamma over spot and time to expiry. Drag to turn it: the ridge along
          the strike turns into a spike in the last weeks.
        </figcaption>
      </figure>
      <figure>
        <Heatmap
          xs={SPOTS}
          ys={EXPIRIES}
          values={gammaGrid.value}
          xLabel="spot"
          yLabel="time to expiry"
          xFormat={(v) => fmt(v, 0)}
          yFormat={yearsFmt}
          zFormat={(v) => `gamma ${fmt(v, 4)}`}
          ariaLabel="Heatmap of gamma over spot and time to expiry"
        />
        <figcaption>The same surface from above: hover to read values.</figcaption>
      </figure>
    {/if}
    {#if gammaSlices.value}
      <figure>
        <LineChart
          ariaLabel="Gamma against spot, three months and one week before expiry"
          series={[
            {name: '3 months to expiry', color: 'var(--series-1)', points: gammaSlices.value.quarter},
            {name: '1 week to expiry', color: 'var(--series-2)', points: gammaSlices.value.week},
          ]}
          xFormat={(v) => fmt(v, 0)}
          yFormat={(v) => fmt(v, 3)}
          xLabel="spot"
          markers={[{x: p.strike, label: 'strike'}]}
          height={200}
        />
      </figure>
    {/if}
    {#if atm.value}
      <p>
        At the money, gamma is {fmt(atm.value.gQuarter, 4)} three months out
        and {fmt(atm.value.gWeek, 4)} a week out:
        <strong>{fmt(atm.value.gWeek / atm.value.gQuarter, 1)}×</strong> larger, close to
        √13 ≈ 3.6. Its rate of growth is a second-order Greek of its own,
        <em>colour</em> (∂Γ/∂t), which a week out is {fmt(atm.value.colourWeek, 2)}
        per year. With a day to go, a 1% move in the stock changes delta by
        Γ × S × 1% = <strong>{fmt(atm.value.gDay * p.strike * 0.01, 2)}</strong>:
        the hedge can swing from almost nothing to almost a whole share. That
        is <em>pin risk</em>, and why traders dread short-dated options that sit
        on the strike.
      </p>
    {/if}
  </section>

  <section>
    <h2>4. When vol moves: vanna</h2>
    <p>
      A delta hedge is computed at today's vol. If vol changes, so does delta,
      even if the stock doesn't move. This sensitivity, ∂Δ/∂σ, is
      <em>vanna</em>. Higher vol spreads the option's possible outcomes, so
      the delta curve flattens: out-of-the-money options gain delta and
      in-the-money ones lose it.
    </p>
    {#if deltaCurves.value}
      <figure>
        <LineChart
          ariaLabel="Delta against spot at the current vol and at 10 points higher"
          series={[
            {name: `vol ${volFmt(p.vol)}`, color: 'var(--series-1)', points: deltaCurves.value.low},
            {name: `vol ${volFmt(p.vol + 0.1)}`, color: 'var(--series-2)', points: deltaCurves.value.high},
          ]}
          xFormat={(v) => fmt(v, 0)}
          yFormat={(v) => fmt(v, 2)}
          xLabel="spot"
          markers={[{x: p.strike, label: 'strike'}]}
          height={200}
        />
      </figure>
    {/if}
    {#if g && Number.isFinite(g.vanna)}
      <p>
        Here vanna is {fmt(g.vanna, 3)}: a 5-point rise in vol changes the
        delta by about {fmt(g.vanna * 0.05, 3)}. That matters most with a
        volatility smile, as in <a href="#/volatility">the volatility essay</a>:
        when stocks fall, implied vol tends to rise, so spot and vol move
        together, and a hedge that ignores vanna is consistently wrong in the
        same direction. Its cousin <em>volga</em> (∂vega/∂σ, here
        {fmt(g.volga / 100, 3)} per vol point) is part of why the wings of a
        smile are priced higher: far-from-the-money options gain vega as vol
        rises.
      </p>
    {/if}
  </section>

  <section>
    <h2>5. Hedging in practice</h2>
    <p>
      Now sell the option and delta-hedge it: hold Δ shares, rebalanced
      <DraggableNumber bind:value={p.rebalances} min={2} max={250} step={1} label="number of rebalances" />
      times over its life, along {PATHS.toLocaleString()} simulated paths of
      the stock. If the stock's realised vol is the
      <DraggableNumber bind:value={p.realisedVol} min={0.05} max={0.6} step={0.01} label="realised volatility" format={volFmt} />
      against the {volFmt(p.vol)} the option was priced at, here is the
      seller's final profit or loss, per option:
    </p>
    {#if hedgeError}
      <div class="error" role="alert">{hedgeError}</div>
    {:else if hedgeOk}
      {@const hedge = hedgeOk}
      <figure>
        <Histogram
          samples={hedge.pnl}
          xLabel="P&L at expiry"
          xFormat={(v) => fmt(v, 1)}
          markers={[
            {x: 0, label: 'break even'},
            ...(Math.abs(hedge.mean) > hedge.stddev / 4 ? [{x: hedge.mean, label: 'mean'}] : []),
          ]}
          ariaLabel="Histogram of the seller's hedged P&L across simulated paths"
        />
      </figure>
      <p class="stat">
        Mean <strong>{fmt(hedge.mean, 3)}</strong>
        <span class="muted"
          >· standard deviation {fmt(hedge.stddev, 3)} · premium received {fmt(hedge.premium, 3)}</span
        >
      </p>
      <p>
        When realised vol matches implied, the hedge breaks even on average,
        but not on every path: rebalancing only {p.rebalances} times leaves
        gaps. Hedging more often shrinks the spread like 1/√N, so each halving
        of the error costs four times as many trades (and their costs).
      </p>
      {#if sweepSeries}
        <figure>
          <LineChart
            ariaLabel="Standard deviation of hedged P&L against the number of rebalances, with a 1 over square root of N reference"
            series={[
              {name: 'simulated', color: 'var(--series-1)', points: sweepSeries.actual, dotsOnly: true},
              {name: '1/√N', color: 'var(--series-2)', points: sweepSeries.ideal},
            ]}
            xFormat={(v) => level(2 ** v)}
            yFormat={(v) => fmt(v, 2)}
            xLabel="rebalances (log scale)"
            height={180}
          />
        </figure>
      {/if}
      {#if expectedEdge.value !== undefined}
        <p>
          Now make realised vol differ from implied. The mean moves to about
          the difference between the option's value at the two vols,
          <strong>{fmt(expectedEdge.value, 3)}</strong>, and the simulation's
          mean is {fmt(hedge.mean, 3)}. A hedged short option is a bet on
          volatility: it profits when the stock moves less than the price
          assumed and loses when it moves more.
        </p>
      {/if}
      {#if pathSeries}
        <p>
          One path shows why. Each step, the short option <em>earns</em> its
          time decay, −Θ Δt, and <em>pays</em> for gamma,
          ½ Γ (ΔS)²: a short option's hedge must buy after the stock rises and
          sell after it falls, always a little too late. Theta is set by implied vol; the gamma bill by
          the moves that actually happen.
        </p>
        <figure>
          <LineChart
            ariaLabel="One simulated stock path"
            series={[{name: 'stock', color: 'var(--series-1)', points: pathSeries.spot}]}
            xFormat={(v) => `${fmt(v, 2)}y`}
            yFormat={(v) => fmt(v, 0)}
            xLabel="time"
            height={160}
          />
        </figure>
        <figure>
          <LineChart
            ariaLabel="Cumulative theta earned and gamma paid along that path"
            series={[
              {name: 'theta earned', color: 'var(--series-1)', points: pathSeries.theta},
              {name: 'gamma paid', color: 'var(--series-2)', points: pathSeries.gamma},
            ]}
            xFormat={(v) => `${fmt(v, 2)}y`}
            yFormat={(v) => fmt(v, 1)}
            xLabel="time"
            height={180}
          />
          <figcaption>
            On this path, theta earned {fmt(pathSeries.theta[pathSeries.theta.length - 1].y, 2)}
            and gamma cost {fmt(-pathSeries.gamma[pathSeries.gamma.length - 1].y, 2)}; with interest and
            the gaps between rebalances, the path ended at {fmt(pathSeries.total, 2)}.
          </figcaption>
        </figure>
      {/if}
    {:else}
      <p class="muted">Simulating…</p>
    {/if}
  </section>

  <footer>
    Built with <a href="https://github.com/phaedon/smile-explorer">smile-explorer</a>,
    following Hull's <i>Options, Futures, and Other Derivatives</i> and
    Taleb's <i>Dynamic Hedging</i>.
  </footer>
</article>

<style>
  .greeks {
    border-collapse: collapse;
    font: 14px var(--font-ui);
    font-variant-numeric: tabular-nums;
    margin: 8px 0 16px;
  }
  .greeks th,
  .greeks td {
    padding: 4px 14px;
    text-align: right;
  }
  .greeks thead th {
    color: var(--text-secondary);
    font-weight: 500;
    border-bottom: 1px solid var(--border);
  }
  .greeks tbody th {
    text-align: left;
    font-weight: 500;
  }
</style>
