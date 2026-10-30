<!--
  "Options on a currency": an exchange rate on a binomial tree, drifting at
  the difference between two interest rate curves.
-->
<script lang="ts">
  import DraggableNumber from './DraggableNumber.svelte';
  import LineChart from './LineChart.svelte';
  import NodeFormula from './NodeFormula.svelte';
  import StepControls from './StepControls.svelte';
  import ToggleToken from './ToggleToken.svelte';
  import TreeView, {type NodeRef} from './TreeView.svelte';
  import {engine, nodeIndex, tryPriceFx, type VanillaPricingResult} from './engine';
  import {fmt, level, pct} from './format';
  import {fxParams as p, fxRequestFor, PILLARS} from './fxParams.svelte';

  const rateFmt = (v: number) => pct(v, 2);
  const fxFmt = (v: number) => fmt(v, 4);
  const yearsFmt = (v: number) => fmt(v, 2);
  const pillarLabel = (t: number) => (t < 1 ? `${t * 12}m` : `${t}y`);
  const priceDecimals = 5;

  function attempt<T>(fn: () => T): {value: T; error?: undefined} | {value?: undefined; error: string} {
    try {
      return {value: fn()};
    } catch (e) {
      return {error: e instanceof Error ? e.message : String(e)};
    }
  }

  const curve = (rates: number[]) => ({maturities: PILLARS, rates: [...rates]});

  // Discount factors from both curves at the given times.
  function discountFactors(times: number[]) {
    return {
      domestic: engine.evaluateCurve(curve(p.domesticRates), times),
      foreign: engine.evaluateCurve(curve(p.foreignRates), times),
    };
  }

  // ---- 1. The curves and the forward ------------------------------------------

  const CURVE_TIMES = Array.from({length: 100}, (_, k) => (k + 1) * 0.05);
  const curves = $derived(
    attempt(() => {
      const dfs = discountFactors(CURVE_TIMES);
      const zero = (df: Float64Array) =>
        CURVE_TIMES.map((t, k) => ({x: t, y: -Math.log(df[k]) / t}));
      return {
        domestic: zero(dfs.domestic),
        foreign: zero(dfs.foreign),
        forward: [
          {x: 0, y: p.spot},
          ...CURVE_TIMES.map((t, k) => ({
            x: t,
            y: (p.spot * dfs.foreign[k]) / dfs.domestic[k],
          })),
        ],
      };
    }),
  );
  const forwardAt = (t: number) =>
    curves.value?.forward.find((d) => Math.abs(d.x - t) < 1e-9)?.y ?? NaN;

  // ---- 2. The tree ------------------------------------------------------------

  const outcome = $derived(tryPriceFx(fxRequestFor(p)));

  // Keep showing the last good result while the parameters are invalid.
  let lastGood: VanillaPricingResult | null = null;
  const result = $derived.by(() => {
    if (outcome.result) lastGood = outcome.result;
    return outcome.result ?? lastGood;
  });

  const T = $derived(result?.expiryIndex ?? 0);
  const u = $derived(result ? result.assetValues[2] / result.assetValues[0] : NaN);
  const d = $derived(result ? result.assetValues[1] / result.assetValues[0] : NaN);
  const dt = $derived(result ? result.times[1] : NaN);

  // CRR moves are the same at every node, so each step has one up-probability.
  const upProbs = $derived(
    result
      ? Array.from({length: T}, (_, t) => ({x: result.times[t], y: result.upProbs[nodeIndex(t, 0)]}))
      : [],
  );

  // The tree's expected exchange rate at each step, from its Arrow-Debreu
  // prices (which sum to the domestic discount factor), against the forward.
  const expectation = $derived.by(() => {
    if (!result) return null;
    return attempt(() => {
      const times = Array.from(result.times);
      const dfs = discountFactors(times);
      const tree = times.map((t, ti) => {
        let ad = 0;
        let adS = 0;
        for (let i = 0; i <= ti; ++i) {
          ad += result.arrowDebreu[nodeIndex(ti, i)];
          adS += result.arrowDebreu[nodeIndex(ti, i)] * result.assetValues[nodeIndex(ti, i)];
        }
        return {x: t, y: adS / ad};
      });
      const forward = times.map((t, k) => ({x: t, y: (p.spot * dfs.foreign[k]) / dfs.domestic[k]}));
      const maxGap = Math.max(...tree.map((pt, k) => Math.abs(pt.y - forward[k].y)));
      return {tree, forward, maxGap};
    });
  });

  let assetHover = $state<NodeRef | null>(null);

  // ---- 3. Pricing -------------------------------------------------------------

  let optionHover = $state<NodeRef | null>(null);
  let revealedFrom = $state(Number.POSITIVE_INFINITY);
  $effect(() => {
    if (revealedFrom > T) revealedFrom = T;
  });
  const shownNode = $derived.by(() => {
    if (optionHover) return optionHover;
    const t = Math.min(revealedFrom, T);
    return {t, i: Math.ceil(t / 2)};
  });

  // Put-call parity holds exactly on the tree, for European options.
  const parity = $derived.by(() => {
    const call = tryPriceFx(fxRequestFor(p, {isCall: true, isAmerican: false})).result;
    const put = tryPriceFx(fxRequestFor(p, {isCall: false, isAmerican: false})).result;
    if (!call || !put) return null;
    const expiry = call.times[call.expiryIndex];
    const dfs = attempt(() => discountFactors([expiry]));
    if (!dfs.value) return null;
    return {
      call: call.price,
      put: put.price,
      dfDomestic: dfs.value.domestic[0],
      dfForeign: dfs.value.foreign[0],
    };
  });

  // European prices against the number of steps to expiry (so the expiry is
  // always on the grid).
  const convergence = $derived.by(() => {
    const points: {x: number; y: number}[] = [];
    let closedForm = NaN;
    for (let n = 1; n <= 60; ++n) {
      const r = tryPriceFx(fxRequestFor(p, {stepsPerYear: n / p.expiryYears, isAmerican: false})).result;
      if (!r) continue;
      points.push({x: n, y: r.price});
      closedForm = r.bsmPrice;
    }
    return {points, closedForm};
  });

  // ---- 4. Early exercise --------------------------------------------------------

  const american = $derived(tryPriceFx(fxRequestFor(p, {isAmerican: true})).result);
  const european = $derived(tryPriceFx(fxRequestFor(p, {isAmerican: false})).result);

  function loadHullExample() {
    p.spot = 0.61;
    p.strike = 0.6;
    p.vol = 0.12;
    p.expiryYears = 0.25;
    p.stepsPerYear = 12;
    p.isCall = true;
    p.isAmerican = true;
    p.domesticRates = PILLARS.map(() => 0.05);
    p.foreignRates = PILLARS.map(() => 0.07);
    revealedFrom = 0;
  }

  function swapRates() {
    [p.domesticRates, p.foreignRates] = [p.foreignRates, p.domesticRates];
  }
</script>

{#snippet rateTable()}
  <div class="rates" role="group" aria-label="Zero rates by maturity">
    <table>
      <thead>
        <tr>
          <th scope="col">zero rate</th>
          {#each PILLARS as t (t)}<th scope="col">{pillarLabel(t)}</th>{/each}
        </tr>
      </thead>
      <tbody>
        <tr>
          <th scope="row"><i class="swatch" style:background="var(--series-1)"></i>USD (domestic)</th>
          {#each PILLARS as t, k (t)}
            <td
              ><DraggableNumber
                bind:value={p.domesticRates[k]}
                min={-0.01}
                max={0.12}
                step={0.0025}
                label="USD {pillarLabel(t)} zero rate"
                format={rateFmt}
              /></td
            >
          {/each}
        </tr>
        <tr>
          <th scope="row"><i class="swatch" style:background="var(--series-2)"></i>EUR (foreign)</th>
          {#each PILLARS as t, k (t)}
            <td
              ><DraggableNumber
                bind:value={p.foreignRates[k]}
                min={-0.01}
                max={0.12}
                step={0.0025}
                label="EUR {pillarLabel(t)} zero rate"
                format={rateFmt}
              /></td
            >
          {/each}
        </tr>
      </tbody>
    </table>
  </div>
{/snippet}

<article class="essay">
  <header>
    <p class="kicker">smile-explorer · interactive notes</p>
    <h1>Options on a currency</h1>
    <p class="lede">
      An exchange rate is an asset that pays interest: hold euros, and they
      earn the euro rate. That one fact changes how the
      <a href="#/binomial">binomial tree</a> drifts, where the forward sits,
      and when an American option is worth exercising early. And because the
      rates come from whole curves rather than single numbers, the tree's
      drift can change from one step to the next.
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
    <h2>1. Two interest rates, one exchange rate</h2>
    <p>
      Take EUR-USD at S<sub>0</sub> =
      <DraggableNumber bind:value={p.spot} min={0.5} max={2} step={0.005} label="EUR-USD spot" format={fxFmt} />
      dollars per euro. Quoted this way, the dollar is the <em>domestic</em>
      currency, the one prices are paid in, and the euro is the
      <em>foreign</em> one, the asset being priced. Each currency has its own
      curve of zero rates, with constant forward rates between maturities:
    </p>

    {@render rateTable()}

    <p>
      Buying a euro forward for delivery at <i>T</i> must cost the same as
      buying P<sub>EUR</sub>(<i>T</i>) euros today, which grow to one euro by
      <i>T</i>, and borrowing the dollars to pay for them. So the forward is
      pinned by the two discount factors. This is <em>covered interest parity</em>:
    </p>
    <p class="display">
      F(<i>T</i>) = S<sub>0</sub> × P<sub>EUR</sub>(<i>T</i>) / P<sub>USD</sub>(<i>T</i>),
      &nbsp; F(1y) = <strong>{fxFmt(forwardAt(1))}</strong>
    </p>

    {#if curves.error}
      <div class="error" role="alert">{curves.error}</div>
    {:else if curves.value}
      <figure>
        <LineChart
          ariaLabel="USD and EUR zero rates by maturity"
          series={[
            {name: 'USD (domestic)', color: 'var(--series-1)', points: curves.value.domestic},
            {name: 'EUR (foreign)', color: 'var(--series-2)', points: curves.value.foreign},
          ]}
          xFormat={(v) => `${fmt(v, v % 1 === 0 ? 0 : 1)}y`}
          yFormat={(v) => pct(v, 1)}
          xLabel="maturity"
          height={200}
        />
      </figure>
      <figure>
        <LineChart
          ariaLabel="The EUR-USD forward rate by maturity"
          series={[{name: 'EUR-USD forward', color: 'var(--series-1)', points: curves.value.forward}]}
          xFormat={(v) => `${fmt(v, v % 1 === 0 ? 0 : 1)}y`}
          yFormat={fxFmt}
          xLabel="delivery date"
          markers={[{x: p.expiryYears, label: 'option expiry'}]}
          height={200}
        />
        <figcaption>
          While dollar rates are above euro rates, the forward rises above
          spot: a euro delivered later costs more dollars, because the dollars
          could have been earning the higher rate in the meantime. This is
          what it means for the higher-yielding currency to trade at a forward
          discount. Make the euro rates higher than the dollar rates and the
          forward falls below spot instead.
        </figcaption>
      </figure>
    {/if}
  </section>

  <section>
    <h2>2. A tree that drifts at the rate differential</h2>
    <p>
      Now build a Cox–Ross–Rubinstein tree for the exchange rate, with
      volatility σ =
      <DraggableNumber bind:value={p.vol} min={0.03} max={0.3} step={0.005} label="volatility" format={(v) => pct(v, 1)} />
      over
      <DraggableNumber bind:value={p.expiryYears} min={0.25} max={2} step={0.25} label="expiry in years" format={yearsFmt} />
      years, in steps of Δt = 1/<DraggableNumber bind:value={p.stepsPerYear} min={1} max={40} step={1} label="steps per year" />
      of a year. The up and down moves are the same as for a stock:
      <i>u</i> = e<sup>σ√Δt</sup> = {fmt(u, 4)} and <i>d</i> = 1/<i>u</i>.
    </p>
    <p>
      The risk-neutral probability is what changes. A dollar investor who holds
      euros earns the euro rate on them, so under the dollar risk-neutral
      measure the exchange rate needs to grow only by the <em>difference</em>
      between the two rates. Over the step from <i>t</i> to <i>t</i> + Δt, that
      growth is the dollar forward discount factor divided by the euro one,
      and with flat curves it is e<sup>(<i>r</i><sub>USD</sub> − <i>r</i><sub>EUR</sub>)Δt</sup>:
    </p>
    {#if result}
      <p class="display">
        <i>g</i> = (P<sub>USD</sub>(<i>t</i>) / P<sub>USD</sub>(<i>t</i>+Δt)) /
        (P<sub>EUR</sub>(<i>t</i>) / P<sub>EUR</sub>(<i>t</i>+Δt)) = {fmt(result.forwardGrowth[0], 5)},
        &nbsp; <i>p</i> = (<i>g</i> − <i>d</i>) / (<i>u</i> − <i>d</i>) =
        <strong>{fmt(result.upProbs[0], 4)}</strong>
      </p>

      <figure>
        <TreeView {result} mode="asset" bind:hovered={assetHover} />
        <figcaption>
          {#if assetHover}
            Step {assetHover.t}, t = {fmt(result.times[assetHover.t], 3)}y:
            S = {fxFmt(result.assetValues[nodeIndex(assetHover.t, assetHover.i)])}{assetHover.t < T
              ? `, up-probability ${fmt(result.upProbs[nodeIndex(assetHover.t, assetHover.i)], 4)}`
              : ''}.
          {:else}
            The exchange-rate tree: {T} steps of {fmt(dt * 12, 2)} months. Hover a
            node to read it.
          {/if}
        </figcaption>
      </figure>

      <p>
        Each step uses the forward rates over that step, so when the curves
        slope, the drift changes along the tree, even though the moves
        <i>u</i> and <i>d</i> don't. With the curves above, the up-probability
        jumps where a pillar starts a new forward rate:
      </p>
      <figure>
        <LineChart
          ariaLabel="Risk-neutral up-probability at each step of the tree"
          series={[{name: 'up-probability', color: 'var(--series-1)', points: upProbs, dotsOnly: true}]}
          xFormat={(v) => `${level(v)}y`}
          yFormat={(v) => fmt(v, 4)}
          xLabel="start of step"
          markers={PILLARS.filter((t) => t < result.times[T]).map((t) => ({x: t, label: pillarLabel(t)}))}
          height={180}
        />
      </figure>

      {#if expectation?.value}
        <p>
          A good test of the tree: weight every node by its Arrow–Debreu price
          and the expected exchange rate at each step should be the forward.
          Across all {T + 1} time slices it misses by at most
          <strong>{expectation.value.maxGap.toExponential(1)}</strong>.
        </p>
        <figure>
          <LineChart
            ariaLabel="The tree's expected exchange rate at each step, against the forward from the curves"
            series={[
              {name: 'forward from the curves', color: 'var(--series-1)', points: expectation.value.forward},
              {name: 'expected value on the tree', color: 'var(--series-2)', points: expectation.value.tree, dotsOnly: true},
            ]}
            xFormat={(v) => `${level(v)}y`}
            yFormat={fxFmt}
            xLabel="time"
            height={180}
          />
        </figure>
      {/if}
    {/if}
  </section>

  <section>
    <h2>3. Pricing against Garman–Kohlhagen</h2>
    <p>
      Pricing works exactly as for a stock: payoffs at expiry, then backward
      induction, discounting each step at the <em>dollar</em> rate. Here is a
      <ToggleToken bind:value={p.isCall} on="call" off="put" label="option type" />
      on the euro, struck at <i>K</i> =
      <DraggableNumber bind:value={p.strike} min={0.5} max={2} step={0.005} label="strike" format={fxFmt} />.
      The library prices it with <code>CurrencyDerivative</code>, a single-asset
      derivative whose up-probabilities come from two curves:
    </p>

    <pre><code>ZeroSpotCurve usd({'{'}{PILLARS.join(', ')}{'}'}, {'{'}{p.domesticRates.map((r) => fmt(r, 4)).join(', ')}{'}'});
ZeroSpotCurve eur({'{'}{PILLARS.join(', ')}{'}'}, {'{'}{p.foreignRates.map((r) => fmt(r, 4)).join(', ')}{'}'});
CurrencyDerivative fx_option(&amp;asset.binomialTree(), &amp;usd, &amp;eur);
double price = fx_option.price(
    VanillaOption(<DraggableNumber
          bind:value={p.strike}
          min={0.5}
          max={2}
          step={0.005}
          label="strike"
          format={fxFmt}
        />, OptionPayoff::<ToggleToken
          bind:value={p.isCall}
          on="Call"
          off="Put"
          label="option type"
        />, ExerciseStyle::<ToggleToken
          bind:value={p.isAmerican}
          on="American"
          off="European"
          label="exercise style"
        />),
    <DraggableNumber
          bind:value={p.expiryYears}
          min={0.25}
          max={2}
          step={0.25}
          label="expiry in years"
          format={yearsFmt}
        />);  // = {result ? fmt(result.price, priceDecimals) : '–'}</code></pre>

    {#if result}
      <figure>
        <StepControls bind:revealedFrom expiryIndex={T} />
        <TreeView
          {result}
          mode="option"
          strike={p.strike}
          isAmerican={p.isAmerican}
          revealedFrom={Math.min(revealedFrom, T)}
          bind:hovered={optionHover}
        />
        <NodeFormula
          {result}
          node={shownNode}
          strike={p.strike}
          isCall={p.isCall}
          isAmerican={p.isAmerican}
          revealedFrom={Math.min(revealedFrom, T)}
          digits={4}
        />
      </figure>

      <p>
        For a European option there is a closed form, Black–Scholes with the
        foreign rate playing the part of a dividend yield. In FX it is called
        the Garman–Kohlhagen formula:
      </p>
      <p class="display">
        C = S<sub>0</sub> P<sub>EUR</sub>(<i>T</i>) N(d<sub>1</sub>) − <i>K</i>
        P<sub>USD</sub>(<i>T</i>) N(d<sub>2</sub>)
      </p>
      <p class="stat">
        Tree price <strong>{fmt(result.price, priceDecimals)}</strong>
        <span class="muted"
          >· Garman–Kohlhagen (European) {fmt(result.bsmPrice, priceDecimals)}</span
        >
      </p>
    {/if}

    {#if convergence.points.length > 0}
      <figure>
        <LineChart
          ariaLabel="European tree price against the number of steps, converging to Garman-Kohlhagen"
          series={[
            {name: 'tree (European)', color: 'var(--series-1)', points: convergence.points},
            {
              name: 'Garman–Kohlhagen',
              color: 'var(--series-2)',
              points: convergence.points.map((pt) => ({x: pt.x, y: convergence.closedForm})),
            },
          ]}
          xFormat={(v) => fmt(v, 0)}
          yFormat={(v) => fmt(v, 4)}
          xLabel="steps to expiry"
          height={200}
        />
        <figcaption>
          As for a stock, the tree price zig-zags towards the formula as the
          steps shrink.
        </figcaption>
      </figure>
    {/if}

    {#if parity}
      <p>
        Calls and puts are tied together, too. A call minus a put with the same
        strike is a forward contract, worth S<sub>0</sub>P<sub>EUR</sub>(<i>T</i>) −
        <i>K</i>P<sub>USD</sub>(<i>T</i>) whatever the volatility, and the tree
        gets this exactly:
      </p>
      <p class="display">
        {fmt(parity.call, priceDecimals)} − {fmt(parity.put, priceDecimals)} =
        <strong>{fmt(parity.call - parity.put, priceDecimals)}</strong> &nbsp; vs &nbsp;
        {fxFmt(p.spot)} × {fmt(parity.dfForeign, 4)} − {fxFmt(p.strike)} ×
        {fmt(parity.dfDomestic, 4)} =
        <strong>{fmt(p.spot * parity.dfForeign - p.strike * parity.dfDomestic, priceDecimals)}</strong>
      </p>
    {/if}
  </section>

  <section>
    <h2>4. When early exercise pays</h2>
    <p>
      For a stock without dividends an American call is never worth
      exercising early. A currency does pay a "dividend", its interest rate,
      and that changes things. Exercising a call early hands over the strike
      in dollars, giving up the dollar interest on it, and in return you start
      earning the euro rate on the euros you receive. Deep in the money, when
      euro rates are higher than dollar rates, that trade is worth making.
    </p>
    <p>
      <button class="inline" onclick={loadHullExample}>Load Hull's example</button>
      to see this. It is a 3-month call struck at 0.60 on a currency at 0.61,
      with 12% vol, a 5% domestic rate and a 7% foreign one (Hull,
      <i>Options, Futures, and Other Derivatives</i>, figure 13.12). The
      highlighted nodes in the option tree above are where exercising beats
      holding.
    </p>
    {#if american && european}
      <p class="stat">
        {p.isCall ? 'Call' : 'Put'}: American <strong>{fmt(american.price, priceDecimals)}</strong>
        <span class="muted"
          >· European {fmt(european.price, priceDecimals)} · early-exercise premium
          {fmt(american.price - european.price, priceDecimals)}</span
        >
      </p>
    {/if}
    <p>
      Now <button class="inline" onclick={swapRates}>swap the two curves</button>.
      With dollar rates above euro rates, there is little reason to take the
      euros early, and the call's premium shrinks or vanishes. Switch to a
      <ToggleToken bind:value={p.isCall} on="call" off="put" label="option type" />
      and the logic runs the other way: exercising a put early gets you dollars
      now, which earn the higher dollar rate.
    </p>
  </section>

  <section>
    <h2>Where the curves come from</h2>
    <p>
      Here, both curves are inputs: a handful of zero rates, joined by
      constant forwards. Their only effect on the tree is through the
      discount factors at each step. The library can also build a curve from
      a model of the short rate, the Hull–White trinomial tree, which is
      calibrated so that its discount factors match the market's. It is the
      subject of a later essay.
    </p>
  </section>

  <footer>
    Built with <a href="https://github.com/phaedon/smile-explorer">smile-explorer</a>,
    following Hull's <i>Options, Futures, and Other Derivatives</i> and Garman
    &amp; Kohlhagen, <i>Foreign Currency Option Values</i>
    (1983).
  </footer>
</article>

<style>
  .rates {
    overflow-x: auto;
    margin: 12px 0 16px;
  }
  table {
    border-collapse: collapse;
    font: 14px var(--font-ui);
    font-variant-numeric: tabular-nums;
  }
  th,
  td {
    padding: 4px 10px;
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
    color: var(--text-primary);
  }
  .swatch {
    display: inline-block;
    width: 12px;
    height: 3px;
    border-radius: 2px;
    margin-right: 6px;
    vertical-align: 3px;
  }
  code {
    font-family: var(--font-code);
    font-size: 0.92em;
  }
</style>
