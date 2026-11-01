<!--
  "Natural gas: options on a seasonal curve": a futures curve with a winter
  hump, what storage says about it, mean reversion and the Samuelson effect,
  and a trinomial tree fitted to the curve to price options on futures.
-->
<script lang="ts">
  import DraggableNumber from './DraggableNumber.svelte';
  import LineChart from './LineChart.svelte';
  import StepControls, {type StepLabels} from './StepControls.svelte';
  import ToggleToken from './ToggleToken.svelte';
  import TrinomialTreeView from './TrinomialTreeView.svelte';
  import {engine, type CommodityOptionResult, type CommodityTreeResult} from './engine';
  import {fmt, pct} from './format';
  import {
    CONTRACT_TIMES,
    futuresPrices,
    gasParams as p,
    gasSpecFor,
    monthAt,
    monthName,
    NUM_CONTRACTS,
    shapePrice,
    tryBuildGasTree,
    tryPriceGasOption,
  } from './gasParams.svelte';

  const usd = (v: number, digits = 2) => (Number.isFinite(v) ? `$${v.toFixed(digits)}` : '–');
  const monthsFmt = (v: number) => (v === 1 ? '1 month' : `${v} months`);
  // "Dec", or "Dec next year" for the second year's contracts.
  const contractName = (m: number) => `${monthName(m)}${m > 12 ? ' next year' : ''}`;

  // Keep showing the last good result while the parameters are invalid.
  function lastGood<T>() {
    let last: T | null = null;
    return (value: T | undefined) => {
      if (value !== undefined) last = value;
      return value ?? last;
    };
  }

  // ---- 1. The futures curve -----------------------------------------------------

  const prices = $derived(futuresPrices(p));
  // Today (spot) and each contract, as one curve.
  const curvePoints = $derived([
    {x: 0, y: p.spot},
    ...CONTRACT_TIMES.map((t, k) => ({x: t, y: prices[k]})),
  ]);
  const monthTicks = (to: number, every: number) =>
    Array.from({length: Math.floor(to * 12 / every) + 1}, (_, k) => (k * every) / 12);

  const steps = $derived(
    curvePoints.slice(1).map((pt, k) => ({from: curvePoints[k], to: pt})),
  );
  const contango = $derived(steps.filter((s) => s.to.y > s.from.y).length);
  const dearest = $derived(prices.slice(0, 12).reduce((best, v, k) => (v > prices[best] ? k : best), 0));
  const cheapest = $derived(prices.slice(0, 12).reduce((best, v, k) => (v < prices[best] ? k : best), 0));

  function resetTweaks() {
    p.tweaks = p.tweaks.map(() => 0);
  }
  const tweaked = $derived(p.tweaks.some((v) => v !== 0));

  // ---- 2. Storage -------------------------------------------------------------

  // Buying in month i (0 for spot today), storing, and selling in month j: the
  // profit per MMBtu, financing the purchase at r and paying storage monthly.
  const storageTrades = $derived.by(() => {
    const at = (i: number) => (i === 0 ? p.spot : prices[i - 1]);
    let best = {i: 0, j: 1, profit: -Infinity};
    let breakEven = -Infinity;
    for (let i = 0; i < NUM_CONTRACTS; ++i) {
      for (let j = i + 1; j <= NUM_CONTRACTS; ++j) {
        const financed = at(i) * Math.exp((p.rate * (j - i)) / 12);
        const profit = at(j) - financed - p.storage * (j - i);
        if (profit > best.profit) best = {i, j, profit};
        breakEven = Math.max(breakEven, (at(j) - financed) / (j - i));
      }
    }
    return {best, breakEven, buy: at(best.i), sell: at(best.j)};
  });

  // The convenience yield over each month, implied by F_next = F e^((r + u - y) / 12),
  // with the storage cost u as a yield on that month's price.
  const convenience = $derived(
    steps.map(({from, to}) => ({
      x: (from.x + to.x) / 2,
      y: p.rate + (12 * p.storage) / from.y - 12 * Math.log(to.y / from.y),
    })),
  );
  const negativeMonths = $derived(convenience.filter((c) => c.y < 0).length);

  // ---- 3 and 4. Mean reversion and vols -------------------------------------------

  const fullOutcome = $derived(tryBuildGasTree(gasSpecFor(p)));
  const keepFull = lastGood<CommodityTreeResult>();
  const full = $derived(keepFull(fullOutcome.tree));

  const cone = $derived.by(() => {
    if (!full) return null;
    const pts = (ys: Float64Array) => Array.from(full.times, (t, k) => ({x: t, y: ys[k]}));
    const last = full.times.length - 1;
    return {
      series: [
        {name: 'mean-reverting, 95%', group: 'mean-reverting: 5%, median, 95%', color: 'var(--series-1)', points: pts(full.spotQ95)},
        {name: 'mean-reverting, median', group: 'mean-reverting: 5%, median, 95%', color: 'var(--series-1)', points: pts(full.spotQ50)},
        {name: 'mean-reverting, 5%', group: 'mean-reverting: 5%, median, 95%', color: 'var(--series-1)', points: pts(full.spotQ05)},
        {name: 'GBM, 95%', group: 'GBM with the same σ', color: 'var(--series-2)', dashed: true, points: pts(full.gbmQ95)},
        {name: 'GBM, 5%', group: 'GBM with the same σ', color: 'var(--series-2)', dashed: true, points: pts(full.gbmQ05)},
      ],
      mr: [full.spotQ05[last], full.spotQ95[last]],
      gbm: [full.gbmQ05[last], full.gbmQ95[last]],
      // How much of the long-run spread of ln S is there after a year.
      oneYear: Math.sqrt(-Math.expm1(-2 * p.meanReversion)),
    };
  });
  const halfLifeMonths = $derived((12 * Math.LN2) / p.meanReversion);

  const vols = $derived.by(() => {
    if (!full) return null;
    const pts = (ys: Float64Array) => CONTRACT_TIMES.map((t, k) => ({x: t, y: ys[k]}));
    return {futures: pts(full.futuresVols), options: pts(full.optionVols)};
  });

  // ---- 5. The fitted tree and an option --------------------------------------------

  const optionOutcome = $derived(tryPriceGasOption(p));
  const treeOutcome = $derived(tryBuildGasTree(gasSpecFor(p, p.contract / 12)));
  const keepOption = lastGood<CommodityOptionResult>();
  const keepTree = lastGood<CommodityTreeResult>();
  const option = $derived(keepOption(optionOutcome.option));
  const tree = $derived(keepTree(treeOutcome.tree));
  const error = $derived(fullOutcome.error ?? treeOutcome.error ?? optionOutcome.error);

  const T = $derived(tree ? tree.times.length - 1 : 0);
  const dt = $derived(1 / (12 * p.stepsPerMonth));
  const sliceOf = (n: number) => {
    if (!tree) return 0;
    let t = 0;
    while (tree.sliceOffsets[t + 1] <= n) ++t;
    return t;
  };
  const treeTicks = $derived(monthTicks(p.contract / 12, p.contract > 12 ? 3 : 1));

  const fitLabels: StepLabels = {
    group: 'Fitting the tree to the futures curve',
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
  const fitted = $derived(T - Math.min(fitPending, T));
  let fitHover = $state<number | null>(null);

  const fitStep = $derived.by(() => {
    if (!tree) return null;
    const m = fitted;
    let expectedExpX = 0;
    for (let n = tree.sliceOffsets[m]; n < tree.sliceOffsets[m + 1]; ++n) {
      expectedExpX += tree.probabilities[n] * Math.exp(tree.x[n]);
    }
    return {m, expectedExpX, alpha: tree.alphas[m], curve: tree.curvePrices[m], expected: tree.expectedSpots[m]};
  });

  // Backward induction for the option.
  const expiry = $derived(option ? option.expiryIndex : 0);
  let optionFrom = $state(Number.POSITIVE_INFINITY);
  $effect(() => {
    if (optionFrom > expiry) optionFrom = expiry;
  });
  let optionHover = $state<number | null>(null);
  const exercisedFlags = $derived(option ? Array.from(option.exercised, (v) => v === 1) : null);
  const optionNode = $derived.by(() => {
    if (!tree || !option) return null;
    const from = Math.min(optionFrom, expiry);
    const n = optionHover ?? tree.sliceOffsets[from] + (tree.sliceOffsets[from + 1] - tree.sliceOffsets[from] - 1) / 2;
    const t = sliceOf(n);
    const m = tree.successorMid[n];
    const payoff = Math.max(p.isCall ? option.futuresValues[n] - p.strike : p.strike - option.futuresValues[n], 0);
    const continuation =
      t < expiry && m >= 0
        ? Math.exp(-p.rate * dt) *
          (tree.pu[n] * option.optionValues[m + 1] + tree.pm[n] * option.optionValues[m] + tree.pd[n] * option.optionValues[m - 1])
        : NaN;
    return {n, t, payoff, continuation, computed: t >= from && t <= expiry};
  });

  // The tree's European price at more and more steps a month.
  const convergence = $derived.by(() => {
    if (!option) return null;
    const rows = [];
    for (const stepsPerMonth of [1, 2, 4, 8, 16]) {
      try {
        const r = engine.priceCommodityOption({
          commodity: {...gasSpecFor(p, p.contract / 12), stepsPerMonth},
          option: {
            deliveryYears: p.contract / 12,
            expiryYears: Math.max(1, p.contract - p.monthsEarly) / 12,
            strike: p.strike,
            isCall: p.isCall,
            isAmerican: false,
          },
        });
        rows.push({stepsPerMonth, price: r.europeanPrice, error: r.europeanPrice - r.blackPrice});
      } catch {
        return null;
      }
    }
    return rows;
  });
</script>

{#snippet curveTable()}
  <div class="months" role="group" aria-label="Futures prices by delivery month">
    <table>
      <thead>
        <tr>
          <th scope="col">delivery</th>
          {#each CONTRACT_TIMES.slice(0, 12) as _, k (k)}<th scope="col">{monthName(k + 1)}</th>{/each}
        </tr>
      </thead>
      <tbody>
        {#each [0, 1] as year (year)}
          <tr>
            <th scope="row">{year === 0 ? 'this year' : 'next year'}</th>
            {#each CONTRACT_TIMES.slice(0, 12) as _, c (c)}
              {@const k = year * 12 + c}
              <td
                ><DraggableNumber
                  bind:value={() => prices[k], (v) => (p.tweaks[k] = v / shapePrice(p, k) - 1)}
                  min={0.5}
                  max={12}
                  step={0.01}
                  label="{contractName(k + 1)} futures price"
                  format={(v) => fmt(v, 2)}
                /></td
              >
            {/each}
          </tr>
        {/each}
      </tbody>
    </table>
  </div>
{/snippet}

{#snippet modelControls()}
  <i>a</i> = <DraggableNumber bind:value={p.meanReversion} min={0.1} max={6} step={0.1} label="mean reversion speed a" format={(v) => fmt(v, 1)} />
  and σ = <DraggableNumber bind:value={p.sigma} min={0.1} max={1.5} step={0.05} label="spot volatility sigma" format={(v) => pct(v, 0)} />
{/snippet}

<article class="essay">
  <header>
    <p class="kicker">smile-explorer · interactive notes</p>
    <h1>Natural gas: options on a seasonal curve</h1>
    <p class="lede">
      A share of stock is the same thing in January and July. Natural gas isn't:
      gas delivered in winter, when it heats homes, is a different product from
      gas delivered in summer, and it's priced differently. That seasonality
      shapes everything here. Futures replace a single spot price, storage
      bounds how the curve can bend, and prices that revert to the mean make
      near contracts far wilder than distant ones. The tree that prices options
      here is the one we used for <a href="#/hull-white">interest rates</a>,
      fitted to a futures curve instead of a yield curve.
      <span class="hint">Drag any <span class="sample">underlined number</span>.</span>
    </p>
  </header>

  {#if error}
    <div class="error sticky" role="alert">
      <strong>The engine rejected these parameters:</strong>
      {error}. Showing the last valid results.
    </div>
  {/if}

  <section>
    <h2>1. A price for every month</h2>
    <p>
      Gas trades today for delivery in each future month, and each month has its
      own futures price. Here the spot price is
      <DraggableNumber bind:value={p.spot} min={1} max={8} step={0.05} label="spot price" format={(v) => usd(v)} />
      per MMBtu (a million British thermal units). The futures curve averages
      <DraggableNumber bind:value={p.level} min={1} max={8} step={0.05} label="curve level" format={(v) => usd(v)} />,
      with a winter premium of
      <DraggableNumber bind:value={p.winter} min={0} max={0.4} step={0.01} label="winter premium" format={(v) => `±${pct(v, 0)}`} />
      peaking in January and a drift of
      <DraggableNumber bind:value={p.slope} min={-0.2} max={0.2} step={0.01} label="curve slope per year" format={(v) => `${v >= 0 ? '+' : ''}${pct(v, 0)}`} />
      a year. These numbers are illustrative, not market data. You can also
      drag any single month:
    </p>

    {@render curveTable()}
    {#if tweaked}
      <p><button type="button" class="inline" onclick={resetTweaks}>Reset the months to the curve</button></p>
    {/if}

    <figure>
      <LineChart
        ariaLabel="Futures prices for each delivery month over two years, starting from today's spot price"
        series={[
          {name: 'futures price', group: 'futures', color: 'var(--series-1)', points: curvePoints},
          {name: 'contract', group: 'futures', color: 'var(--series-1)', points: curvePoints.slice(1), dotsOnly: true},
        ]}
        xFormat={monthAt}
        yFormat={(v) => usd(v)}
        markers={[
          {x: 2 / 12, label: 'this winter'},
          {x: 14 / 12, label: 'next winter'},
        ]}
        xLabel="delivery"
        height={240}
      />
      <figcaption>
        Today's spot price, then the futures price for delivery in each month.
        Over the next twelve months the dearest is {monthName(dearest + 1)} at
        {usd(prices[dearest])}, and the cheapest {monthName(cheapest + 1)} at {usd(prices[cheapest])}.
      </figcaption>
    </figure>

    <p>
      A curve that rises with delivery date is in <em>contango</em>. One that
      falls is in <em>backwardation</em>. A stock index's futures are almost
      always in gentle contango. This curve does both, every year: of its
      {steps.length} month-to-month steps, <strong>{contango}</strong> rise (into
      winter) and <strong>{steps.length - contango}</strong> fall (out of it).
    </p>
    <p>
      For any asset you can hold, the futures price is the spot price carried
      forward:
    </p>
    <p class="display">
      F(<i>T</i>) = S e<sup>(<i>r</i> + <i>u</i> − <i>y</i>)<i>T</i></sup>
    </p>
    <p>
      Here <i>r</i> is the cost of financing the purchase, <i>u</i> the cost of
      storing it, and <i>y</i> the <em>convenience yield</em>, the benefit of
      having the physical thing on hand. For a stock, <i>u</i> is nil and
      <i>y</i> is the dividend yield. For gold, storage is cheap and the
      convenience yield small, so its curve sits close to full carry. For gas,
      storage is expensive and the convenience yield swings with the seasons.
      The next section looks at what that means.
    </p>
  </section>

  <section>
    <h2>2. Storage caps contango, not backwardation</h2>
    <p>
      Suppose you can store gas at
      <DraggableNumber bind:value={p.storage} min={0} max={0.5} step={0.01} label="storage cost per MMBtu per month" format={(v) => usd(v)} />
      per MMBtu a month, and borrow at
      <DraggableNumber bind:value={p.rate} min={0} max={0.1} step={0.0025} label="interest rate" format={(v) => pct(v, 2)} />.
      Then any two months set a bound. Buying in month <i>i</i>, storing, and
      selling forward for month <i>j</i> locks in
    </p>
    <p class="display">
      F<sub><i>j</i></sub> − F<sub><i>i</i></sub> e<sup><i>r</i>(<i>t<sub>j</sub></i> − <i>t<sub>i</sub></i>)</sup> − <i>u</i> × (months stored)
    </p>
    {#if storageTrades.best.profit > 0}
      <p>
        On this curve the best such trade is to buy
        {storageTrades.best.i === 0 ? 'spot gas today' : `${contractName(storageTrades.best.i)} gas`}
        at {usd(storageTrades.buy)} and sell it for {contractName(storageTrades.best.j)} at
        {usd(storageTrades.sell)}. That earns <strong>{usd(storageTrades.best.profit, 3)}</strong>
        per MMBtu after financing and storage. This is the <em>intrinsic value</em>
        of a unit of storage. Anyone with spare capacity at this cost would fill
        it, buying the cheap month and selling the dear one, until the spread
        closed. The curve pays for storage at up to
        <strong>{usd(storageTrades.breakEven, 3)}</strong> a month. Raise the
        storage cost past that and the trade disappears.
      </p>
    {:else}
      <p>
        At this storage cost no such trade pays: the best one loses
        {usd(-storageTrades.best.profit, 3)} per MMBtu. The curve pays for storage
        at up to <strong>{usd(storageTrades.breakEven, 3)}</strong> a month, and
        storage costs more than that. Lower the cost below it, or drag a winter
        month up, and storing starts to pay.
      </p>
    {/if}
    <p>
      So storage puts a ceiling on how fast the curve can rise. Nothing
      similar limits how fast it falls. To profit from a winter price that's too
      high relative to spring, you would sell gas now and buy it back later. But
      that needs gas in storage to sell, and gas can't be borrowed from the
      future. In backwardation the curve can fall as steeply as the market
      wants.
    </p>
    <p>
      In the language of the cost of carry, each month's step implies a
      convenience yield. The storage bound says it can't go below zero for
      anyone with storage at this cost. The months where it does are where
      storing pays:
    </p>
    <figure>
      <LineChart
        ariaLabel="Convenience yield implied by each month-to-month step of the curve"
        series={[{name: 'implied convenience yield', color: 'var(--series-2)', points: convenience, dotsOnly: true}]}
        xFormat={monthAt}
        yFormat={(v) => pct(v, 0)}
        xLabel="month"
        height={200}
      />
      <figcaption>
        <i>y</i> = <i>r</i> + <i>u</i> − 12 ln(F<sub>next</sub> / F), with
        storage as a yield on each month's price, for each step of the curve.
        {#if negativeMonths > 0}
          {negativeMonths} {negativeMonths === 1 ? 'step is' : 'steps are'} below
          zero, rising into winter faster than carry allows.
        {:else}
          Every step is above zero: the curve never rises faster than carry
          allows.
        {/if}
        The large positive values are the months heading out of winter.
      </figcaption>
    </figure>
  </section>

  <section>
    <h2>3. Prices that come back</h2>
    <p>
      A stock can drift anywhere. Gas prices tend to come back: a cold snap
      sends spot prices up, then production and storage respond and pull them
      back down. The standard one-factor model writes the log of the spot price
      as
    </p>
    <p class="display">
      ln S(<i>t</i>) = X(<i>t</i>) + α(<i>t</i>), &nbsp; dX = −<i>a</i> X d<i>t</i> + σ d<i>W</i>, &nbsp; X(0) = 0
    </p>
    <p>
      X is a shock that decays at speed <i>a</i>, and α(<i>t</i>) is a
      deterministic level that will be fitted to the futures curve. Take
      {@render modelControls()}. A shock then has a half-life of
      <strong>{fmt(halfLifeMonths, 1)} months</strong>.
    </p>
    {#if cone}
      <figure>
        <LineChart ariaLabel="Spot price quantiles over two years, with mean reversion and under geometric Brownian motion" series={cone.series} xFormat={monthAt} yFormat={(v) => usd(v)} xLabel="time" height={260} />
        <figcaption>
          The 5%–95% range of the spot price over time, around the futures
          curve. After two years, mean reversion keeps it between
          {usd(cone.mr[0])} and {usd(cone.mr[1])}. Geometric Brownian motion with the same
          σ spreads it from {usd(cone.gbm[0])} to {usd(cone.gbm[1])}, and keeps widening.
        </figcaption>
      </figure>
      <p>
        Under geometric Brownian motion the variance of ln S grows as
        σ²<i>t</i>, forever. With mean reversion it levels off at
        σ²/(2<i>a</i>). By one year it has reached
        {pct(cone.oneYear, 0)} of that ceiling (as a standard deviation), so the
        cone stops widening. The wiggles in the cone are the futures curve
        showing through: the spot price is expected to be dear in winter, and
        uncertainty is measured around that.
      </p>
    {/if}
  </section>

  <section>
    <h2>4. Near months are wild, far months calm</h2>
    <p>
      The futures price for delivery at <i>T</i> is the expected spot price
      then. A shock to X today has decayed by a factor of
      e<sup>−<i>a</i>(<i>T</i> − <i>t</i>)</sup> by delivery, so it moves
      distant futures much less than near ones. Each contract's volatility is
      σe<sup>−<i>a</i>(<i>T</i> − <i>t</i>)</sup>. This is the
      <em>Samuelson effect</em>: futures get more volatile as delivery
      approaches.
    </p>
    <p>
      An option expiring at delivery sees that volatility rise over its life.
      Its Black-76 implied vol is the root-mean-square average,
      σ√((1 − e<sup>−2<i>aT</i></sup>) / (2<i>aT</i>)).
    </p>
    {#if vols && full}
      <figure>
        <LineChart
          ariaLabel="Volatility of each futures contract today, and the implied vol of an option on it expiring at delivery"
          series={[
            {name: "futures' volatility today", color: 'var(--series-1)', points: vols.futures},
            {name: 'implied vol of an option expiring at delivery', color: 'var(--series-2)', points: vols.options},
          ]}
          xFormat={monthAt}
          yFormat={(v) => pct(v, 0)}
          xLabel="delivery"
          height={220}
        />
        <figcaption>
          The December contract's options imply {pct(full.optionVols[0], 0)};
          options on next November's imply {pct(full.optionVols[NUM_CONTRACTS - 1], 0)}.
          A shorter half-life widens the gap.
        </figcaption>
      </figure>
      <p>
        So one set of model parameters produces a different implied vol for
        every contract. A gas trader's "vol surface" is laid out by delivery
        month as much as by strike. The model here is lognormal, so it has no
        smile within a contract. It does capture the term structure that
        dominates gas.
      </p>
    {/if}
  </section>

  <section>
    <h2>5. The tree, fitted to the curve</h2>
    <p>
      The tree for X is exactly the Hull–White tree from the
      <a href="#/hull-white">interest rate essay</a>. It has the same spacing,
      Δx = σ√(3Δt), the same branch probabilities, and the same edges where
      branching bends inwards, with Δt =
      1/<DraggableNumber bind:value={p.stepsPerMonth} min={1} max={8} step={1} label="steps per month" />
      of a month. Only the fitting changes. There, each α repriced a bond. Here,
      each α makes the expected spot price equal the futures price. Carry the
      probabilities Q of reaching each node forward (no discounting this time),
      and set
    </p>
    <p class="display">
      α<sub><i>m</i></sub> = ln F(<i>t<sub>m</sub></i>) − ln Σ<sub><i>j</i></sub> Q<sub><i>m</i>,<i>j</i></sub> e<sup><i>j</i>Δx</sup>
    </p>

    <pre><code>FuturesCurve curve(spot, contract_times, futures_prices);
CommodityTree tree(curve, /*mean_reversion=*/<DraggableNumber bind:value={p.meanReversion} min={0.1} max={6} step={0.1} label="mean reversion speed a" format={(v) => fmt(v, 1)} />, /*sigma=*/<DraggableNumber bind:value={p.sigma} min={0.1} max={1.5} step={0.05} label="spot volatility sigma" format={(v) => fmt(v, 2)} />,
                   /*dt=*/1 / (12. * <DraggableNumber bind:value={p.stepsPerMonth} min={1} max={8} step={1} label="steps per month" />), /*horizon_years=*/<DraggableNumber bind:value={p.contract} min={1} max={NUM_CONTRACTS} step={1} label="delivery month of the option's contract" format={(v) => fmt(v / 12, 3)} />);</code></pre>

    {#if tree && fitStep}
      <figure>
        <StepControls bind:revealedFrom={fitPending} expiryIndex={T} labels={fitLabels} />
        <TrinomialTreeView
          {tree}
          yMode="price"
          values={tree.probabilities}
          shownTo={fitted}
          xTicks={treeTicks}
          xFormat={monthAt}
          bind:hovered={fitHover}
        />
        <div class="panel" aria-live="polite">
          <div class="head">Step {fitStep.m} · {fmt(fitStep.m / p.stepsPerMonth, 2)} months in</div>
          <div>
            α<sub>{fitStep.m}</sub> = ln {fmt(fitStep.curve, 4)} − ln {fmt(fitStep.expectedExpX, 5)} = <strong>{fmt(fitStep.alpha, 5)}</strong>
          </div>
          <div class="muted">
            expected spot on the tree {usd(fitStep.expected, 4)} · futures curve {usd(fitStep.curve, 4)}
          </div>
          {#if fitHover !== null && sliceOf(fitHover) <= fitted}
            <div class="muted">
              Hovered node: S = {usd(tree.spots[fitHover], 3)}, Q = {fmt(tree.probabilities[fitHover], 5)}
            </div>
          {/if}
        </div>
        <figcaption>
          The spot price tree up to delivery of the {contractName(p.contract)} contract,
          shaded by the probability of reaching each node. Hollow nodes aren't
          fitted yet. Press play to fit it one step at a time. The tree's centre
          follows the futures curve, and its nodes bunch up at low prices
          because they're evenly spaced in log.
        </figcaption>
      </figure>
    {/if}

    <p>
      With the tree fitted, the futures price at any node is the expected spot
      at delivery from there: backward induction, again without discounting.
      An option on the futures then rolls back as usual, discounting at
      <i>r</i>, with early exercise checked at every node if it's American.
      Take a
      <ToggleToken bind:value={p.isCall} on="call" off="put" label="option type" />
      on the
      <DraggableNumber bind:value={p.contract} min={1} max={NUM_CONTRACTS} step={1} label="contract delivery month" format={contractName} />
      contract, struck at
      <DraggableNumber bind:value={p.strike} min={0.5} max={10} step={0.05} label="strike" format={(v) => usd(v)} />,
      expiring
      <DraggableNumber
        bind:value={p.monthsEarly}
        min={0}
        max={Math.max(0, p.contract - 1)}
        step={1}
        label="months before delivery the option expires"
        format={(v) => (v === 0 ? 'at delivery' : `${monthsFmt(v)} before delivery`)}
      />, with
      <ToggleToken bind:value={p.isAmerican} on="American" off="European" label="exercise style" />
      exercise.
    </p>

    {#if tree && option && optionNode}
      <figure>
        <StepControls bind:revealedFrom={optionFrom} expiryIndex={expiry} />
        <TrinomialTreeView
          {tree}
          yMode="price"
          values={option.optionValues}
          marked={exercisedFlags}
          shownFrom={Math.min(optionFrom, expiry)}
          lastSlice={expiry}
          xTicks={treeTicks}
          xFormat={monthAt}
          bind:hovered={optionHover}
        />
        <div class="panel" aria-live="polite">
          <div class="head">
            Step {optionNode.t} · S = {usd(tree.spots[optionNode.n], 3)} · futures {usd(option.futuresValues[optionNode.n], 3)}
          </div>
          {#if !optionNode.computed}
            <div class="muted">Not computed yet: step the induction back to here.</div>
          {:else if optionNode.t === expiry}
            <div>payoff at expiry = <strong>{usd(option.optionValues[optionNode.n], 4)}</strong></div>
          {:else}
            <div>
              continue: e<sup>−<i>r</i>Δt</sup> × E[V] = {usd(optionNode.continuation, 4)} ·
              exercise now: {usd(optionNode.payoff, 4)}
            </div>
            <div class="muted">
              {option.exercised[optionNode.n] ? 'Exercised early here.' : 'Held.'} Value <strong>{usd(option.optionValues[optionNode.n], 4)}</strong>
            </div>
          {/if}
        </div>
        <figcaption>
          The option's value at each node, from lowest (light) to highest (dark),
          with a ring wherever the holder exercises (at expiry, wherever it
          ends in the money). Press play to roll it back from expiry.
        </figcaption>
      </figure>

      <p class="stat">
        On the tree <strong>{usd(option.price, 4)}</strong>
        <span class="muted"
          >· European {usd(option.europeanPrice, 4)} · Black-76 at the Samuelson vol of
          {pct(option.impliedVol, 1)}: {usd(option.blackPrice, 4)}</span
        >
      </p>
      <p>
        The European price converges on Black-76 as the steps shrink. That's
        the check that the tree and the closed form describe the same model. The
        error wobbles as the strike moves between nodes:
      </p>
      {#if convergence}
        <div class="months">
          <table>
            <thead>
              <tr><th scope="col">steps a month</th>{#each convergence as row (row.stepsPerMonth)}<th scope="col">{row.stepsPerMonth}</th>{/each}</tr>
            </thead>
            <tbody>
              <tr><th scope="row">European on the tree</th>{#each convergence as row (row.stepsPerMonth)}<td>{usd(row.price, 4)}</td>{/each}</tr>
              <tr><th scope="row">minus Black-76</th>{#each convergence as row (row.stepsPerMonth)}<td>{fmt(row.error, 4)}</td>{/each}</tr>
            </tbody>
          </table>
        </div>
      {/if}
      <p>
        {#if p.isAmerican}Early exercise is worth {usd(option.price - option.europeanPrice, 4)} here.{/if}
        A futures position costs nothing to hold, so the only reason to exercise
        early is to collect the payoff now and earn interest on it. That matters
        only deep in the money and with rates well above zero, and at
        <i>r</i> = 0 an American call on futures is worth exactly the European.
      </p>
    {/if}
  </section>

  <section>
    <h2>6. What this model leaves out</h2>
    <p>
      One factor drives everything, so every contract moves together. They
      differ in size (the Samuelson effect) but are perfectly correlated, so
      winter and summer can't diverge. That understates calendar-spread
      options, and with them the extra value of storage beyond its intrinsic
      value (a real facility is an option to inject and withdraw as spreads
      move, within limits on its capacity and rates). Two-factor models add a
      slowly moving long-term level.
    </p>
    <p>
      Gas prices also jump. A cold snap can multiply a regional spot price in
      days, which lognormal diffusion can't produce, and options markets price
      that as a smile within each contract. Regional hubs trade at a basis to
      the benchmark, and some have traded below zero when pipelines were full.
      A lognormal model can't reach negative prices (the
      <a href="#/swaptions">Bachelier model</a> can). And real contracts stop
      trading a few days before their delivery month, with options expiring
      shortly before that. Here, expiry and delivery fall exactly at month
      starts.
    </p>
    <p>
      What carries over is the structure: a curve to fit, a mean-reverting
      tree to fit it with, and the same backward induction as everywhere else.
    </p>
  </section>

  <footer>
    Built with <a href="https://github.com/phaedon/smile-explorer">smile-explorer</a>,
    following the one-factor model for commodity prices in Hull's <i>Options,
    Futures, and Other Derivatives</i> (the chapter on energy and commodity
    derivatives), on the Hull–White tree of the
    <a href="#/hull-white">interest rate essay</a>.
  </footer>
</article>

<style>
  .months {
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
    padding: 4px 8px;
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
