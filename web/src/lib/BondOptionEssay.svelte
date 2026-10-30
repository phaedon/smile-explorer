<!--
  "Options on bonds": pricing options on a zero-coupon bond on a Hull-White
  tree, checking them against the closed form, and seeing what mean
  reversion and volatility do.
-->
<script lang="ts">
  import DraggableNumber from './DraggableNumber.svelte';
  import LineChart from './LineChart.svelte';
  import StepControls from './StepControls.svelte';
  import ToggleToken from './ToggleToken.svelte';
  import TrinomialTreeView from './TrinomialTreeView.svelte';
  import {engine, type BondOptionResult} from './engine';
  import {fmt, level, pct} from './format';
  import {BOND_MATURITY, bondParams as p, bondRequestFor} from './bondParams.svelte';

  const priceFmt = (v: number) => fmt(v, 4);

  function attempt<T>(fn: () => T): {value: T; error?: undefined} | {value?: undefined; error: string} {
    try {
      return {value: fn()};
    } catch (e) {
      return {error: e instanceof Error ? e.message : String(e)};
    }
  }

  const outcome = $derived(attempt(() => engine.priceBondOption(bondRequestFor(p))));

  // Keep showing the last good result while the parameters are invalid.
  let lastGood: BondOptionResult | null = null;
  const result = $derived.by(() => {
    if (outcome.value) lastGood = outcome.value;
    return outcome.value ?? lastGood;
  });

  const tree = $derived(result?.tree ?? null);
  const T = $derived(result?.expiryIndex ?? 0);
  const dt = $derived(p.expiryYears / p.stepsToExpiry);
  const sliceOf = (n: number) => {
    if (!tree) return 0;
    let t = 0;
    while (tree.sliceOffsets[t + 1] <= n) ++t;
    return t;
  };
  const middleOf = (t: number) =>
    tree ? (tree.sliceOffsets[t] + tree.sliceOffsets[t + 1] - 1) / 2 : 0;

  // ---- 1. Pricing on the tree -------------------------------------------------

  let revealedFrom = $state(Number.POSITIVE_INFINITY);
  $effect(() => {
    if (revealedFrom > T) revealedFrom = T;
  });
  let hovered = $state<number | null>(null);
  const shownNode = $derived(hovered ?? middleOf(Math.min(revealedFrom, T)));

  const exercised = $derived.by(() => {
    if (!result || !p.isAmerican) return null;
    return Array.from(result.optionValues, (v, n) => {
      const hold = result.continuationValues[n];
      const exercise = result.exerciseValues[n];
      return Number.isFinite(hold) && exercise > 0 && exercise > hold + 1e-12;
    });
  });

  const nodeStep = $derived.by(() => {
    if (!result || !tree) return null;
    const n = shownNode;
    const t = sliceOf(n);
    return {n, t, m: tree.successorMid[n]};
  });

  function tryLowStrike() {
    p.isCall = false;
    p.isAmerican = true;
    p.strike = 50;
    p.stepsToExpiry = 6;
    revealedFrom = 0;
  }

  // ---- 2. The closed form -------------------------------------------------------

  const sigmaP = $derived.by(() => {
    const a = p.meanReversion;
    return (
      (p.sigma / a) *
      (1 - Math.exp(-a * (BOND_MATURITY - p.expiryYears))) *
      Math.sqrt((1 - Math.exp(-2 * a * p.expiryYears)) / (2 * a))
    );
  });

  const STEPS = [3, 5, 8, 10, 15, 20, 30, 40, 50, 60, 80, 100];
  const convergence = $derived.by(() => {
    const points: {x: number; y: number}[] = [];
    let analytic = NaN;
    for (const n of STEPS) {
      const r = attempt(() =>
        engine.priceBondOption(bondRequestFor(p, {stepsToExpiry: n, isAmerican: false})),
      ).value;
      if (!r) continue;
      points.push({x: n, y: r.price});
      analytic = r.analyticPrice;
    }
    return {points, analytic};
  });

  // ---- 4. Mean reversion and volatility ----------------------------------------

  // The closed form is enough here, so use a coarse tree for each point.
  function sweep(key: 'meanReversion' | 'sigma', xs: number[]) {
    return xs.flatMap((v) => {
      const r = attempt(() =>
        engine.priceBondOption(
          bondRequestFor(p, {
            ...(key === 'sigma' ? {sigma: v} : {meanReversion: v}),
            stepsToExpiry: 3,
            isAmerican: false,
          }),
        ),
      ).value;
      return r ? [{x: v, y: r.analyticPrice}] : [];
    });
  }
  const bySigma = $derived(sweep('sigma', Array.from({length: 40}, (_, k) => 0.001 * (k + 1))));
  const byMeanReversion = $derived(
    sweep('meanReversion', Array.from({length: 40}, (_, k) => 0.02 + 0.02 * k)),
  );
</script>

<article class="essay">
  <header>
    <p class="kicker">smile-explorer · interactive notes</p>
    <h1>Options on bonds</h1>
    <p class="lede">
      In <a href="#/hull-white">the previous essay</a>, a Hull–White tree was
      fitted so that every zero-coupon bond came out at its market price,
      whatever the model's parameters. Options on those bonds are a different
      matter: their value depends on how far rates can move before they
      expire, and that is exactly what the tree's shape describes.
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
    <h2>1. An option on a bond</h2>
    <p>
      Take Hull's example: a zero-coupon bond paying 100 in {BOND_MATURITY}
      years, and a
      <ToggleToken bind:value={p.isCall} on="call" off="put" label="option type" />
      on it expiring in {p.expiryYears} years, struck at
      <DraggableNumber bind:value={p.strike} min={40} max={90} step={0.5} label="strike" format={(v) => fmt(v, 1)} />.
      Today's curve has zero rates rising from 5% to 7.5% over ten years, and
      the model has mean reversion <i>a</i> =
      <DraggableNumber bind:value={p.meanReversion} min={0.01} max={0.8} step={0.01} label="mean reversion speed a" format={(v) => fmt(v, 2)} />
      and volatility σ =
      <DraggableNumber bind:value={p.sigma} min={0.001} max={0.04} step={0.0005} label="short-rate volatility sigma" format={(v) => pct(v, 2)} />.
    </p>
    <p>
      The tree runs to the bond's maturity, with
      <DraggableNumber bind:value={p.stepsToExpiry} min={1} max={12} step={1} label="steps to expiry" />
      steps to expiry (so Δt = {fmt(dt, 3)}y). First the bond is priced
      backwards from its face value, as before. At expiry, each node then
      knows the bond's price there, and the option's payoff is
      {p.isCall ? 'max(B − K, 0)' : 'max(K − B, 0)'}: a put pays off where
      rates are high and the bond is cheap. From there, backward induction
      works as for any option, discounting at each node's own short rate.
    </p>

    <pre><code>InterestRateDerivative bond_option(&amp;tree, &amp;bond);
double price = bond_option.price(
    VanillaOption(<DraggableNumber bind:value={p.strike} min={40} max={90} step={0.5} label="strike" format={(v) => fmt(v, 1)} />, OptionPayoff::<ToggleToken
          bind:value={p.isCall}
          on="Call"
          off="Put"
          label="option type"
        />, ExerciseStyle::<ToggleToken
          bind:value={p.isAmerican}
          on="American"
          off="European"
          label="exercise style"
        />), {level(p.expiryYears)});  // = {result ? priceFmt(result.price) : '–'}</code></pre>

    {#if result && tree && nodeStep}
      <figure>
        <StepControls bind:revealedFrom expiryIndex={T} />
        <TrinomialTreeView
          {tree}
          yMode="rate"
          values={result.optionValues}
          shownFrom={Math.min(revealedFrom, T)}
          marked={exercised}
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
                V = payoff = {p.isCall ? `max(${fmt(tree.bondValues[n], 3)} − ${level(p.strike)}, 0)` : `max(${level(p.strike)} − ${fmt(tree.bondValues[n], 3)}, 0)`}
                = <strong>{priceFmt(result.optionValues[n])}</strong>
              </div>
            {:else}
              {@const m = nodeStep.m}
              <div>
                hold = e<sup>−{fmt(tree.shortRates[n], 4)}×{fmt(dt, 3)}</sup> × ({fmt(tree.pu[n], 3)} × {priceFmt(result.optionValues[m + 1])}
                + {fmt(tree.pm[n], 3)} × {priceFmt(result.optionValues[m])} + {fmt(tree.pd[n], 3)} × {priceFmt(result.optionValues[m - 1])})
                = {priceFmt(result.continuationValues[n])}
              </div>
              {#if p.isAmerican}
                <div>exercise = {priceFmt(result.exerciseValues[n])}</div>
                <div>
                  V = max(exercise, hold) = <strong>{priceFmt(result.optionValues[n])}</strong>
                  {#if exercised?.[n]}<em class="flag">exercise early</em>{/if}
                </div>
              {:else}
                <div>V = hold = <strong>{priceFmt(result.optionValues[n])}</strong></div>
              {/if}
            {/if}
          {/if}
        </div>
        <figcaption>
          Nodes shaded by the option's value (light to dark); faded nodes are
          after expiry, where only the bond is priced. Press play to roll the
          option back from expiry, and hover a node for its arithmetic.
        </figcaption>
      </figure>

      <p class="stat">
        Tree price <strong>{priceFmt(result.price)}</strong>
        <span class="muted">· closed form (European) {priceFmt(result.analyticPrice)}</span>
      </p>
    {/if}
  </section>

  <section>
    <h2>2. Checking against the closed form</h2>
    <p>
      For a European option on a zero-coupon bond, Hull–White has a closed
      form. The bond's forward price at expiry is lognormal, so the option is
      priced by a Black-style formula with today's bond prices
      P(0, {level(p.expiryYears)}) and P(0, {BOND_MATURITY}) and a bond price volatility
    </p>
    <p class="display">
      σ<sub>P</sub> = (σ/<i>a</i>)(1 − e<sup>−<i>a</i>(<i>s</i>−<i>T</i>)</sup>)
      √((1 − e<sup>−2<i>aT</i></sup>) / 2<i>a</i>) = <strong>{pct(sigmaP, 2)}</strong>
    </p>
    <p>
      where <i>T</i> = {level(p.expiryYears)} is the option's expiry and <i>s</i> =
      {BOND_MATURITY} the bond's maturity. As the tree's steps shrink, its
      European price converges to the formula, just as a binomial tree
      converges to Black–Scholes. Hull reports 1.8468 with 10 steps and
      1.8090 with 200, against a closed form of 1.8093; small differences here
      come from how the zero curve is interpolated.
    </p>
    {#if convergence.points.length > 0}
      <figure>
        <LineChart
          ariaLabel="European bond option price on the tree against the number of steps to expiry, converging to the closed form"
          series={[
            {name: 'tree (European)', color: 'var(--series-1)', points: convergence.points},
            {
              name: 'closed form',
              color: 'var(--series-2)',
              points: convergence.points.map((pt) => ({x: pt.x, y: convergence.analytic})),
            },
          ]}
          xFormat={(v) => fmt(v, 0)}
          yFormat={(v) => fmt(v, 3)}
          xLabel="steps to expiry"
          height={200}
        />
      </figure>
    {/if}
  </section>

  <section>
    <h2>3. Exercising early</h2>
    <p>
      An American option on the bond can be exercised at any node before
      expiry. Switch this one to
      <ToggleToken bind:value={p.isAmerican} on="American" off="European" label="exercise style" />
      and the nodes where exercising beats holding are ringed in the tree
      above.
    </p>
    <p>
      With Hull's strike of 63, the American put is exercised at once. A
      zero-coupon bond's price is pulled towards its face value as it nears
      maturity: this one is worth {tree ? fmt(tree.bondPrice, 2) : '–'} today, and
      its forward price for year 3 is {tree && result ? fmt(tree.bondPrice / tree.marketDfs[result.expiryIndex], 2) : '–'}. So the right to
      sell it for 63 <em>today</em> is worth {tree ? fmt(63 - tree.bondPrice, 2) : '–'}
      straight away, far more than waiting. A strike that suits a European
      option is deep in the money for an American one.
    </p>
    <p>
      Early exercise only becomes a real decision with a strike below today's
      bond price. <button class="inline" onclick={tryLowStrike}>Try a strike of 50</button>
      on a finer tree. The European put is now almost worthless, because by
      expiry the bond will have drifted well above 50. But the American put
      can be exercised early at the nodes where rates have jumped and pushed
      the bond below 50 before it has had time to recover.
    </p>
    {#if result}
      <p class="stat">
        {p.isAmerican ? 'American' : 'European'} {p.isCall ? 'call' : 'put'}
        <strong>{priceFmt(result.price)}</strong>
        <span class="muted"
          >· European {priceFmt(result.europeanPrice)} · early-exercise premium
          {priceFmt(result.price - result.europeanPrice)}</span
        >
      </p>
    {/if}
    <p>
      Calls work the other way: a bond with positive rates is expected to
      rise, so an American call is never worth exercising early, just as for
      a stock without dividends. Pricing any of this needed a fix in the
      library: the trinomial tree's option evaluator marked American exercise
      as unimplemented with
      <code>static_assert("Unimplemented!")</code>. That assertion never
      fires, because a string literal is always true, so American bond options
      were silently priced as European.
    </p>
  </section>

  <section>
    <h2>4. What mean reversion and volatility do</h2>
    <p>
      The bond itself is worth {tree ? fmt(tree.bondPrice, 3) : '–'} whatever
      <i>a</i> and σ are. The option is not. σ<sub>P</sub> is the short-rate
      volatility, scaled by how sensitive the bond is to rates at expiry
      (the factor (1 − e<sup>−<i>a</i>(<i>s</i>−<i>T</i>)</sup>)/<i>a</i>), and by how
      much rate uncertainty builds up by then (the square root). Raising σ
      raises both; faster mean reversion pulls rates back towards their
      expected path and shrinks both, so the option gets cheaper.
    </p>
    <figure>
      <LineChart
        ariaLabel="European bond option price against the short-rate volatility sigma"
        series={[{name: 'option price', color: 'var(--series-1)', points: bySigma}]}
        xFormat={(v) => pct(v, 1)}
        yFormat={(v) => fmt(v, 2)}
        xLabel="σ (short-rate volatility)"
        markers={[{x: p.sigma, label: 'current σ'}]}
        height={180}
      />
    </figure>
    <figure>
      <LineChart
        ariaLabel="European bond option price against the mean reversion speed a"
        series={[{name: 'option price', color: 'var(--series-1)', points: byMeanReversion}]}
        xFormat={(v) => fmt(v, 2)}
        yFormat={(v) => fmt(v, 2)}
        xLabel="a (mean reversion)"
        markers={[{x: p.meanReversion, label: 'current a'}]}
        height={180}
      />
      <figcaption>
        European prices from the closed form, varying one parameter at a time
        around the current values.
      </figcaption>
    </figure>
    <p>
      In practice, <i>a</i> and σ are chosen to match the market prices of
      liquid options such as caps and swaptions, much as the α's were chosen
      to match the curve. The tree can then price options the market doesn't
      quote, such as American or Bermudan ones.
    </p>
  </section>

  <footer>
    Built with <a href="https://github.com/phaedon/smile-explorer">smile-explorer</a>,
    following Hull's <i>Options, Futures, and Other Derivatives</i> (chapter
    32, example 32.1).
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
  .flag {
    font-style: normal;
    font-family: var(--font-ui);
    font-size: 12px;
    color: var(--text-primary);
    border: 1.5px solid var(--text-primary);
    border-radius: 4px;
    padding: 0 6px;
    margin-left: 6px;
  }
  code {
    font-family: var(--font-code);
    font-size: 0.92em;
  }
</style>
