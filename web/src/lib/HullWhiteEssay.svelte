<!--
  "A tree of interest rates": building a Hull-White trinomial tree for the
  short rate and fitting it to today's zero curve.
-->
<script lang="ts">
  import DraggableNumber from './DraggableNumber.svelte';
  import LineChart from './LineChart.svelte';
  import StepControls, {type StepLabels} from './StepControls.svelte';
  import TrinomialTreeView from './TrinomialTreeView.svelte';
  import {engine, type HullWhiteTreeResult} from './engine';
  import {fmt, level, pct} from './format';
  import {HW_PILLARS, hwParams as p, hwSpecFor, tryBuildTree} from './hwParams.svelte';

  const rateFmt = (v: number) => pct(v, 3);
  const yearsFmt = (v: number) => `${level(v)}y`;
  const bp = (v: number) => `${fmt(v * 1e4, 1)}bp`;

  const outcome = $derived(tryBuildTree(hwSpecFor(p)));

  // Keep showing the last good tree while the parameters are invalid.
  let lastGood: HullWhiteTreeResult | null = null;
  const tree = $derived.by(() => {
    if (outcome.tree) lastGood = outcome.tree;
    return outcome.tree ?? lastGood;
  });

  const T = $derived(tree ? tree.times.length - 1 : 0);
  const dt = $derived(1 / p.stepsPerYear);
  const sliceOf = (n: number) => {
    if (!tree) return 0;
    let t = 0;
    while (tree.sliceOffsets[t + 1] <= n) ++t;
    return t;
  };
  const middleOf = (t: number) =>
    tree ? (tree.sliceOffsets[t] + tree.sliceOffsets[t + 1] - 1) / 2 : 0;

  // ---- 1. The shape of the tree ---------------------------------------------

  let shapeHover = $state<number | null>(null);
  // Without a hovered node, explain the top node of the last full step.
  const shapeNode = $derived(
    shapeHover ?? (tree ? tree.sliceOffsets[Math.max(0, T - 1) + 1] - 1 : 0),
  );
  const branchName = ['branches straight on', 'branches upwards', 'branches downwards'];

  // ---- 2. Fitting the curve ---------------------------------------------------

  const fitLabels: StepLabels = {
    group: 'Fitting the tree to the curve',
    reset: 'Reset to today',
    undo: 'Undo one step',
    advance: 'Fit one more step',
    status: (pending, last) =>
      pending === 0
        ? 'Fitted: every step'
        : pending === last
          ? 'Today only'
          : `Fitted ${last - pending} of ${last} steps`,
  };
  let fitPending = $state(Number.POSITIVE_INFINITY);
  $effect(() => {
    if (fitPending > T) fitPending = T;
  });
  // The latest slice whose Arrow-Debreu prices and alpha are known.
  const fitted = $derived(T - Math.min(fitPending, T));

  // The arithmetic for alpha at the latest fitted slice.
  const fitStep = $derived.by(() => {
    if (!tree) return null;
    const m = fitted;
    let weighted = 0;
    let states = 0;
    for (let n = tree.sliceOffsets[m]; n < tree.sliceOffsets[m + 1]; ++n) {
      weighted += tree.arrowDebreu[n] * Math.exp(-tree.x[n] * dt);
      states++;
    }
    let nextDf: number;
    try {
      nextDf = engine.evaluateCurve(hwSpecFor(p).curve, [tree.times[m] + dt])[0];
    } catch {
      return null;
    }
    return {m, weighted, states, nextDf, alpha: tree.alphas[m]};
  });

  let fitHover = $state<number | null>(null);

  // Alpha against the market's one-step forward rates.
  const alphaSeries = $derived.by(() => {
    if (!tree) return null;
    const alpha = [];
    const forward = [];
    for (let t = 0; t < T; ++t) {
      alpha.push({x: tree.times[t], y: tree.alphas[t]});
      forward.push({x: tree.times[t], y: Math.log(tree.marketDfs[t] / tree.marketDfs[t + 1]) / dt});
    }
    const gap = T > 0 ? alpha[T - 1].y - forward[T - 1].y : NaN;
    return {alpha, forward, gap};
  });

  // Zero rates: the market curve against the tree's discount factors.
  const zeroSeries = $derived.by(() => {
    if (!tree) return null;
    const times = Array.from({length: 60}, (_, k) => ((k + 1) * tree.times[T]) / 60);
    let dfs: Float64Array;
    try {
      dfs = engine.evaluateCurve(hwSpecFor(p).curve, times);
    } catch {
      return null;
    }
    const market = times.map((t, k) => ({x: t, y: -Math.log(dfs[k]) / t}));
    const fittedTree = Array.from(tree.times)
      .map((t, k) => ({x: t, y: -Math.log(tree.treeDfs[k]) / t}))
      .slice(1);
    const maxGap = Math.max(...Array.from(tree.treeDfs).map((df, k) => Math.abs(df - tree.marketDfs[k])));
    return {market, fittedTree, maxGap};
  });

  // ---- 3. Pricing a bond ------------------------------------------------------

  const bondLabels: StepLabels = {
    group: 'Backward induction',
    reset: 'Reset to maturity',
    undo: 'Step forward in time',
    advance: 'Step back in time',
    status: (from, last) =>
      from === 0
        ? 'Priced: every step computed'
        : from === last
          ? 'Face value at maturity'
          : `Computed back to step ${from} of ${last}`,
  };
  let bondFrom = $state(Number.POSITIVE_INFINITY);
  $effect(() => {
    if (bondFrom > T) bondFrom = T;
  });
  let bondHover = $state<number | null>(null);
  const bondNode = $derived(bondHover ?? middleOf(Math.min(bondFrom, T)));
  const bondStep = $derived.by(() => {
    if (!tree) return null;
    const n = bondNode;
    const t = sliceOf(n);
    const m = tree.successorMid[n];
    return {n, t, m, computed: t >= Math.min(bondFrom, T)};
  });

  // The range of bond values one step in: these depend on a and sigma, even
  // though their discounted average does not.
  const firstStepRange = $derived.by(() => {
    if (!tree || T < 1) return null;
    const values = Array.from(tree.bondValues.slice(tree.sliceOffsets[1], tree.sliceOffsets[2]));
    return {lo: Math.min(...values), hi: Math.max(...values)};
  });
</script>

{#snippet rateTable()}
  <div class="rates" role="group" aria-label="Market zero rates by maturity">
    <table>
      <thead>
        <tr>
          <th scope="col">maturity</th>
          {#each HW_PILLARS as t (t)}<th scope="col">{yearsFmt(t)}</th>{/each}
        </tr>
      </thead>
      <tbody>
        <tr>
          <th scope="row">zero rate</th>
          {#each HW_PILLARS as t, k (t)}
            <td
              ><DraggableNumber
                bind:value={p.rates[k]}
                min={-0.01}
                max={0.12}
                step={0.0005}
                label="{yearsFmt(t)} zero rate"
                format={(v) => pct(v, 2)}
              /></td
            >
          {/each}
        </tr>
      </tbody>
    </table>
  </div>
{/snippet}

{#snippet modelControls()}
  <i>a</i> = <DraggableNumber bind:value={p.meanReversion} min={0.01} max={1} step={0.01} label="mean reversion speed a" format={(v) => fmt(v, 2)} />
  and σ = <DraggableNumber bind:value={p.sigma} min={0.001} max={0.04} step={0.0005} label="short-rate volatility sigma" format={(v) => pct(v, 2)} />
{/snippet}

<article class="essay">
  <header>
    <p class="kicker">smile-explorer · interactive notes</p>
    <h1>A tree of interest rates</h1>
    <p class="lede">
      The <a href="#/binomial">binomial trees</a> so far model one asset, and
      take interest rates as given. Pricing an interest rate option needs the
      rates themselves to move. The Hull–White model does this with a
      <em>trinomial</em> tree for the short rate, built in two stages: first
      a tree with the right shape, then a shift at each step that makes it
      reprice today's yield curve exactly.
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
    <h2>1. A tree that reverts to the mean</h2>
    <p>
      In the Hull–White model the short rate <i>r</i> follows
      d<i>r</i> = (θ(<i>t</i>) − <i>a r</i>) d<i>t</i> + σ d<i>W</i>: normally
      distributed moves of size σ, and a pull back towards a level that can
      vary over time, at speed <i>a</i>. Start with {@render modelControls()},
      and steps of Δt = 1/<DraggableNumber bind:value={p.stepsPerYear} min={1} max={12} step={1} label="steps per year" />
      of a year over
      <DraggableNumber bind:value={p.horizonYears} min={1} max={10} step={1} label="horizon in years" format={yearsFmt} />.
    </p>
    <p>
      The first stage ignores θ and builds a tree for <i>x</i>, a rate that
      starts at zero and reverts to zero. Its nodes are evenly spaced at
      <i>x</i> = <i>j</i>ΔR, with
    </p>
    {#if tree}
      <p class="display">
        ΔR = σ√(3Δt) = <strong>{bp(tree.dr)}</strong>, &nbsp;
        <i>j</i><sub>max</sub> = ⌈0.184 / (<i>a</i>Δt)⌉ = <strong>{tree.jMax}</strong>
      </p>
      <p>
        From each node, <i>x</i> can move up by ΔR, stay, or move down by ΔR.
        The probabilities are chosen so that the expected change is
        −<i>a x</i>Δt and its variance σ²Δt. Far from the centre, mean
        reversion is strong enough that the "stay" probability,
        2/3 − <i>a</i>²<i>j</i>²Δt², would turn negative. So at
        <i>j</i><sub>max</sub> the tree stops widening: nodes on its edges
        branch inwards instead (the diamonds below), and the tree has at most
        {2 * tree.jMax + 1} states per step.
      </p>

      <figure>
        <TrinomialTreeView {tree} yMode="x" bind:hovered={shapeHover} />
        <div class="panel" aria-live="polite">
          <div class="head">
            Step {sliceOf(shapeNode)} · <i>j</i> = {tree.j[shapeNode]} · <i>x</i> = {bp(tree.x[shapeNode])} ·
            {branchName[tree.branchStyle[shapeNode]]}
          </div>
          {#if tree.successorMid[shapeNode] >= 0}
            <div>
              p<sub>up</sub> = {fmt(tree.pu[shapeNode], 4)}, p<sub>mid</sub> = {fmt(tree.pm[shapeNode], 4)},
              p<sub>down</sub> = {fmt(tree.pd[shapeNode], 4)}
            </div>
            <div class="muted">
              expected move {bp(tree.x[tree.successorMid[shapeNode]] - tree.x[shapeNode] + tree.dr * (tree.pu[shapeNode] - tree.pd[shapeNode]))}
              vs −<i>a x</i>Δt = {bp(-p.meanReversion * tree.x[shapeNode] * dt)}
            </div>
          {:else}
            <div class="muted">The last step: no successors.</div>
          {/if}
        </div>
        <figcaption>
          The unfitted tree of <i>x</i>, with {T} steps and
          {tree.x.length} nodes. Hover a node for its branch probabilities. Try
          more steps per year, or a slower mean reversion: <i>j</i><sub>max</sub>
          grows and the tree spreads further before it stops widening.
        </figcaption>
      </figure>
    {/if}
  </section>

  <section>
    <h2>2. Fitting today's curve</h2>
    <p>
      The tree of <i>x</i> has the right volatility and mean reversion, but its
      rates centre on zero. The second stage shifts each step by an amount
      α(<i>t</i>), so that the short rate at a node is
      <i>r</i> = α(<i>t</i>) + <i>j</i>ΔR. Each α is chosen to reprice one
      more zero-coupon bond on today's curve:
    </p>

    {@render rateTable()}

    <p>
      Fitting runs forwards in time, using Arrow–Debreu prices: Q at a node is
      the value today of $1 paid only if that node is reached. Today Q = 1.
      Given the Q's of step <i>m</i>, the bond maturing one step later must be
      worth Σ<sub><i>j</i></sub> Q<sub><i>m</i>,<i>j</i></sub>
      e<sup>−(α<sub><i>m</i></sub> + <i>j</i>ΔR)Δt</sup>, which fixes
      α<sub><i>m</i></sub>. Then the Q's roll forward to step <i>m</i> + 1,
      discounted at each node's rate, and the process repeats.
    </p>

    <pre><code>ZeroSpotCurve market({'{'}{HW_PILLARS.join(', ')}{'}'}, {'{'}{p.rates.map((r) => fmt(r, 5)).join(', ')}{'}'});
ShortRateTreeCurve tree(
    std::make_unique&lt;HullWhitePropagator&gt;(<DraggableNumber bind:value={p.meanReversion} min={0.01} max={1} step={0.01} label="mean reversion speed a" format={(v) => fmt(v, 2)} />, <DraggableNumber bind:value={p.sigma} min={0.001} max={0.04} step={0.0005} label="short-rate volatility sigma" format={(v) => fmt(v, 4)} />, 1 / <DraggableNumber bind:value={p.stepsPerYear} min={1} max={12} step={1} label="steps per year" />.),
    market, <DraggableNumber bind:value={p.horizonYears} min={1} max={10} step={1} label="horizon in years" format={(v) => fmt(v, 1)} />);</code></pre>

    {#if tree && fitStep}
      <figure>
        <StepControls bind:revealedFrom={fitPending} expiryIndex={T} labels={fitLabels} />
        <TrinomialTreeView {tree} yMode="rate" values={tree.arrowDebreu} shownTo={fitted} bind:hovered={fitHover} />
        <div class="panel" aria-live="polite">
          <div class="head">Step {fitStep.m} · t = {level(tree.times[fitStep.m])}y · {fitStep.states} states</div>
          <div>
            α<sub>{fitStep.m}</sub> = ln(Σ Q e<sup>−<i>j</i>ΔRΔt</sup> / P(0, {level(tree.times[fitStep.m] + dt)}y)) / Δt
            = ln({fmt(fitStep.weighted, 5)} / {fmt(fitStep.nextDf, 5)}) / {fmt(dt, 4)}
            = <strong>{rateFmt(fitStep.alpha)}</strong>
          </div>
          {#if fitHover !== null && sliceOf(fitHover) <= fitted}
            <div class="muted">
              Hovered node: <i>r</i> = {rateFmt(tree.shortRates[fitHover])}, Q = {fmt(tree.arrowDebreu[fitHover], 5)}
            </div>
          {/if}
        </div>
        <figcaption>
          The short-rate tree, with nodes shaded by their Arrow–Debreu price
          (darker is larger; diamonds mark the edges); hollow nodes aren't fitted yet. Press play to
          fit it one step at a time.
          {#if T === 3 && p.stepsPerYear === 1}
            With Hull's numbers, the first three α's are 3.824%, 5.205% and
            6.252%, as in his section 32.6.
          {/if}
        </figcaption>
      </figure>
    {/if}

    {#if alphaSeries && zeroSeries}
      <p>
        α(<i>t</i>) is the centre of the tree, and it tracks the market's
        forward rates. It is not quite equal to them: the discount factor is
        an average of e<sup>−<i>r</i>Δt</sup>, and since that is convex in
        <i>r</i>, rates that are uncertain discount less on average. α has to sit
        a little above the forward to compensate. By the last step the gap is
        <strong>{bp(alphaSeries.gap)}</strong>; it grows with σ and with time.
      </p>
      <figure>
        <LineChart
          ariaLabel="Alpha at each step, against the market's forward rate over that step"
          series={[
            {name: 'market forward rate', color: 'var(--series-1)', points: alphaSeries.forward},
            {name: 'α(t), the centre of the tree', color: 'var(--series-2)', points: alphaSeries.alpha, dotsOnly: true},
          ]}
          xFormat={yearsFmt}
          yFormat={(v) => pct(v, 2)}
          xLabel="start of step"
          height={200}
        />
      </figure>
      <p>
        The result: whatever <i>a</i> and σ are, the tree's discount factors
        reproduce the curve. They differ from the market's by at most
        {zeroSeries.maxGap.toExponential(1)}.
      </p>
      <figure>
        <LineChart
          ariaLabel="Zero rates from the market curve, and from the fitted tree"
          series={[
            {name: 'market zero rates', color: 'var(--series-1)', points: zeroSeries.market},
            {name: 'zero rates from the tree', color: 'var(--series-2)', points: zeroSeries.fittedTree, dotsOnly: true},
          ]}
          xFormat={yearsFmt}
          yFormat={(v) => pct(v, 2)}
          xLabel="maturity"
          height={200}
        />
      </figure>
    {/if}
  </section>

  <section>
    <h2>3. A bond priced on the tree</h2>
    <p>
      Pricing on the tree works backwards, as on a binomial tree, with two
      differences: each node has three successors, and each node discounts at
      its own short rate. Take a zero-coupon bond paying
      <DraggableNumber bind:value={p.bondFace} min={1} max={1000} step={1} label="bond face value" format={(v) => fmt(v, 0)} />
      at the end of the tree:
    </p>
    <p class="display">
      V = e<sup>−<i>r</i>Δt</sup> × (p<sub>up</sub> V<sub>up</sub> + p<sub>mid</sub>
      V<sub>mid</sub> + p<sub>down</sub> V<sub>down</sub>)
    </p>

    <pre><code>FixedCashflowInstrument bond(&amp;tree);
bond.setCashflows({'{'}Cashflow{'{'}.time_years = {tree ? level(tree.times[T]) : '–'}, .amount = <DraggableNumber bind:value={p.bondFace} min={1} max={1000} step={1} label="bond face value" format={(v) => fmt(v, 0)} />{'}'}{'}'});
double price = bond.price();  // = {tree ? fmt(tree.bondPrice, 4) : '–'}</code></pre>

    {#if tree && bondStep}
      <figure>
        <StepControls bind:revealedFrom={bondFrom} expiryIndex={T} labels={bondLabels} />
        <TrinomialTreeView {tree} yMode="rate" values={tree.bondValues} shownFrom={Math.min(bondFrom, T)} bind:hovered={bondHover} />
        <div class="panel" aria-live="polite">
          <div class="head">
            Step {bondStep.t} · <i>r</i> = {rateFmt(tree.shortRates[bondStep.n])}
          </div>
          {#if !bondStep.computed}
            <div class="muted">Not computed yet: step the induction back to here.</div>
          {:else if bondStep.m < 0}
            <div>V = face value = <strong>{fmt(tree.bondValues[bondStep.n], 4)}</strong></div>
          {:else}
            <div>
              V = e<sup>−{fmt(tree.shortRates[bondStep.n], 5)} × {fmt(dt, 4)}</sup> × ({fmt(tree.pu[bondStep.n], 4)} × {fmt(tree.bondValues[bondStep.m + 1], 3)}
              + {fmt(tree.pm[bondStep.n], 4)} × {fmt(tree.bondValues[bondStep.m], 3)} + {fmt(tree.pd[bondStep.n], 4)} × {fmt(tree.bondValues[bondStep.m - 1], 3)})
              = <strong>{fmt(tree.bondValues[bondStep.n], 4)}</strong>
            </div>
          {/if}
        </div>
        <figcaption>
          Nodes shaded by the bond's value, from lowest (light) to highest
          (dark): it is worth less where rates are high. Press play to roll it back from maturity.
        </figcaption>
      </figure>

      <p class="stat">
        Bond on the tree <strong>{fmt(tree.bondPrice, 4)}</strong>
        <span class="muted"
          >· from the market curve: {fmt(p.bondFace, 0)} × P(0, {level(tree.times[T])}y) =
          {fmt(tree.marketBondPrice, 4)}</span
        >
      </p>
      <p>
        Now change {@render modelControls()}. The tree changes shape, and the
        bond's value at each node changes with it: one step in, it ranges from
        {firstStepRange ? fmt(firstStepRange.lo, 2) : '–'} to
        {firstStepRange ? fmt(firstStepRange.hi, 2) : '–'}. But its price today
        doesn't move. The tree was fitted to reprice exactly these bonds, so
        anything that is just a set of fixed cash flows is priced by the curve
        alone, and the model has nothing to add.
      </p>
    {/if}
  </section>

  <section>
    <h2>What the model is for</h2>
    <p>
      If bonds don't depend on <i>a</i> and σ, what does? Anything whose
      payoff isn't linear in bond prices: an option to buy the bond at a fixed
      price, a cap on a floating rate, or the right to enter a swap. Their
      values depend on how far rates can move by the time they expire, which
      is exactly what the shape of the tree describes. That is the subject of
      the next essay.
    </p>
  </section>

  <footer>
    Built with <a href="https://github.com/phaedon/smile-explorer">smile-explorer</a>,
    following Hull's <i>Options, Futures, and Other Derivatives</i> (section
    32.6) and Hull &amp; White, <i>Numerical Procedures for Implementing Term
    Structure Models I: Single-Factor Models</i> (1994).
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
