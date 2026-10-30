<!--
  "When volatility isn't constant": term structures of volatility, local
  volatility, and how a local-vol tree produces an implied-vol smile.
-->
<script lang="ts">
  import {onDestroy} from 'svelte';

  import DraggableNumber from './DraggableNumber.svelte';
  import LineChart from './LineChart.svelte';
  import TreeView, {type NodeRef} from './TreeView.svelte';
  import {engine, nodeIndex, tryPrice} from './engine';
  import {fmt, pct} from './format';
  import type {SmileRequest, SmileResponse} from './smileWorker';
  import {volParams as p} from './volParams.svelte';
  import {smileSpec, TERM_MATURITIES, termSpec} from './volSpecs';

  const volFmt = (v: number) => pct(v, 1);
  const decimalVol = (v: number) => fmt(v, 3);

  function attempt<T>(fn: () => T): {value: T; error?: undefined} | {value?: undefined; error: string} {
    try {
      return {value: fn()};
    } catch (e) {
      return {error: e instanceof Error ? e.message : String(e)};
    }
  }

  // ---- Term structure -------------------------------------------------------

  const forwardVols = $derived(
    attempt(() => {
      const xs = Array.from({length: 301}, (_, k) => k * 0.01);
      const vols = engine.evaluateVol(termSpec(p), p.spot, xs);
      return xs.map((x, k) => ({x, y: vols[k]}));
    }),
  );
  const forwardBetween = (t: number) =>
    forwardVols.value?.find((d) => Math.abs(d.x - t) < 1e-9)?.y ?? NaN;

  const termTree = $derived(
    tryPrice({
      tree: {
        model: engine.TreeModel.CoxRossRubinstein,
        spot: p.spot,
        vol: termSpec(p),
        rate: p.rate,
        drift: 0,
        dtYears: 1 / p.termStepsPerYear,
      },
      option: {strike: p.strike, expiryYears: 3, isCall: true, isAmerican: false},
    }),
  );

  // The average timestep within each year of the term-structure tree.
  const stepsByYear = $derived.by(() => {
    const times = termTree.result?.times;
    if (!times) return [];
    return TERM_MATURITIES.map((end) => {
      let count = 0;
      for (let t = 1; t < times.length; ++t) {
        if (times[t] > end - 1 + 1e-9 && times[t] <= end + 1e-9) count++;
      }
      return count;
    });
  });

  let termHover = $state<NodeRef | null>(null);

  // ---- Local volatility ------------------------------------------------------

  const localVolCurve = $derived(
    attempt(() => {
      const xs = Array.from({length: 81}, (_, k) => 60 + k);
      const vols = engine.evaluateVol(smileSpec(p), p.spot, xs);
      return xs.map((x, k) => ({x, y: vols[k]}));
    }),
  );

  const localTree = $derived(
    tryPrice({
      tree: {
        model: engine.TreeModel.LocalVolatility,
        spot: p.spot,
        vol: smileSpec(p),
        rate: p.rate,
        drift: 0,
        dtYears: 1 / p.smileStepsPerYear,
      },
      option: {strike: p.strike, expiryYears: 1, isCall: true, isAmerican: false},
    }),
  );

  let localHover = $state<NodeRef | null>(null);
  const localHoverVol = $derived.by(() => {
    if (!localHover || !localTree.result) return null;
    const s = localTree.result.assetValues[nodeIndex(localHover.t, localHover.i)];
    return {s, vol: engine.evaluateVol(smileSpec(p), p.spot, [s])[0]};
  });

  // ---- The implied smile -------------------------------------------------------

  // European calls across strikes, priced on a fine local-vol tree and
  // converted to Black-Scholes implied vols. That's dozens of pricings, so it
  // runs in a worker to keep dragging smooth; stale responses are dropped.
  const STRIKES = Array.from({length: 31}, (_, k) => 70 + 2 * k);
  const smileWorker = new Worker(new URL('./smileWorker.ts', import.meta.url), {
    type: 'module',
  });
  let smileRequestId = 0;
  let impliedSmile = $state<
    {value: {x: number; y: number}[]; error?: undefined} | {value?: undefined; error: string} | null
  >(null);
  smileWorker.onmessage = (e: MessageEvent<SmileResponse>) => {
    if (e.data.id !== smileRequestId) return;
    impliedSmile = e.data.error ? {error: e.data.error} : {value: e.data.points!};
  };
  $effect(() => {
    const request: SmileRequest = {
      id: ++smileRequestId,
      smileBase: p.smileBase,
      smileRange: p.smileRange,
      smileStretch: p.smileStretch,
      spot: p.spot,
      rate: p.rate,
      expiryYears: 1,
      dtYears: 1 / 100,
      strikes: STRIKES,
    };
    smileWorker.postMessage(request);
  });
  onDestroy(() => smileWorker.terminate());

  // Slopes near the money: implied vol against strike, local vol against price.
  const slopes = $derived.by(() => {
    const smile = impliedSmile?.value;
    const local = localVolCurve.value;
    if (!smile || !local) return null;
    const at = (pts: {x: number; y: number}[], x: number) =>
      pts.find((d) => d.x === x)?.y ?? NaN;
    const implied = (at(smile, p.spot + 4) - at(smile, p.spot - 4)) / 8;
    const localSlope = (at(local, p.spot + 4) - at(local, p.spot - 4)) / 8;
    return {implied, local: localSlope, ratio: implied / localSlope};
  });
</script>

<article class="essay">
  <header>
    <p class="kicker">smile-explorer · interactive notes</p>
    <h1>When volatility isn't constant</h1>
    <p class="lede">
      In <a href="#/binomial">the first essay</a>, volatility was a single
      number. Option markets disagree: implied volatility depends on the expiry
      (the <em>term structure</em>) and on the strike (the <em>smile</em>).
      Both can be built into a binomial tree, in quite different ways.
      <span class="hint">As before, drag any
        <span class="sample">underlined number</span>.</span
      >
    </p>
  </header>

  <section>
    <h2>1. A term structure of volatility</h2>
    <p>
      Suppose one-, two- and three-year options trade at implied vols of
      <DraggableNumber bind:value={p.termVols[0]} min={0.05} max={0.6} step={0.005} label="1-year implied vol" format={volFmt} />,
      <DraggableNumber bind:value={p.termVols[1]} min={0.05} max={0.6} step={0.005} label="2-year implied vol" format={volFmt} />
      and
      <DraggableNumber bind:value={p.termVols[2]} min={0.05} max={0.6} step={0.005} label="3-year implied vol" format={volFmt} />.
      An implied vol is an <em>average</em> over the option's life, so rising
      implied vols mean that volatility in later years must be higher still.
      The <em>forward</em> vol between two expiries comes from subtracting
      total variances:
    </p>
    <p class="display">
      σ<sub>1→2</sub> = √((2σ<sub>2</sub>² − 1σ<sub>1</sub>²) / (2 − 1)) =
      <strong>{volFmt(forwardBetween(1.5))}</strong>, &nbsp;
      σ<sub>2→3</sub> = <strong>{volFmt(forwardBetween(2.5))}</strong>
    </p>

    {#if forwardVols.error}
      <div class="error" role="alert">{forwardVols.error}</div>
    {:else if forwardVols.value}
      <figure>
        <LineChart
          ariaLabel="Forward volatility over time, with the implied vols it reproduces"
          series={[
            {name: 'forward vol', color: 'var(--series-1)', points: forwardVols.value},
            {
              name: 'implied vol (market quotes)',
              color: 'var(--series-2)',
              points: TERM_MATURITIES.map((t, k) => ({x: t, y: p.termVols[k]})),
              dotsOnly: true,
            },
          ]}
          xFormat={(v) => `${fmt(v, v % 1 === 0 ? 0 : 1)}y`}
          yFormat={(v) => pct(v)}
          xLabel="time"
        />
        <figcaption>
          Forward vol is flat between expiries, and higher than any of the
          implied vols once they rise. Try making the 3-year vol much lower than
          the 2-year vol: the forward variance would turn negative, which no
          model can produce.
        </figcaption>
      </figure>
    {/if}
  </section>

  <section>
    <h2>2. A tree that stretches time</h2>
    <p>
      A CRR tree moves up by <i>u</i> = e<sup>σ√Δt</sup>. If σ rises and Δt
      stays fixed, the moves grow and the tree stops recombining. So instead
      hold σ²Δt constant: when forward vol rises, the timesteps get shorter.
      Starting from Δt = 1/<DraggableNumber bind:value={p.termStepsPerYear} min={2} max={30} step={1} label="first-year steps per year" />
      of a year, this tree takes {stepsByYear.join(', ')} steps in years 1, 2
      and 3.
    </p>

    <pre><code>PiecewiseTermStructureVol vol({'{'}1, 2, 3{'}'}, {'{'}<DraggableNumber bind:value={p.termVols[0]} min={0.05} max={0.6} step={0.005} label="1-year implied vol" format={decimalVol} />, <DraggableNumber bind:value={p.termVols[1]} min={0.05} max={0.6} step={0.005} label="2-year implied vol" format={decimalVol} />, <DraggableNumber bind:value={p.termVols[2]} min={0.05} max={0.6} step={0.005} label="3-year implied vol" format={decimalVol} />{'}'});
StochasticTreeModel&lt;CRRPropagator&gt; asset(
    BinomialTree(3.0, 1 / <DraggableNumber bind:value={p.termStepsPerYear} min={2} max={30} step={1} label="first-year steps per year" />.), CRRPropagator(100));
asset.forwardPropagate(Volatility(vol));</code></pre>

    {#if termTree.error}
      <div class="error" role="alert">{termTree.error}</div>
    {:else if termTree.result}
      {@const r = termTree.result}
      <figure>
        <TreeView result={r} mode="asset" bind:hovered={termHover} />
        <figcaption>
          {#if termHover}
            Step {termHover.t} at t = {fmt(r.times[termHover.t], 3)}y{termHover.t < r.expiryIndex
              ? `, Δt = ${fmt(r.times[termHover.t + 1] - r.times[termHover.t], 4)}y`
              : ''}: S = {fmt(r.assetValues[nodeIndex(termHover.t, termHover.i)])}.
          {:else}
            The columns of nodes crowd together in years 2 and 3, where forward
            vol is higher. Hover a node to see its timestep.
          {/if}
        </figcaption>
      </figure>
      <p class="stat">
        3-year at-the-money call: tree {fmt(r.price, 4)}
        <span class="muted"
          >· Black–Scholes at the 3-year implied vol of {volFmt(r.bsmVol)}:
          {fmt(r.bsmPrice, 4)}</span
        >
      </p>
      <p>
        The tree prices the 3-year option close to the market, because the
        variance it accumulates over three years matches the 3-year implied
        vol. With more (and so shorter) steps, the two converge.
      </p>
    {/if}
  </section>

  <section>
    <h2>3. A smile: local volatility</h2>
    <p>
      The smile is harder. Implied vols that vary by strike can't come from a
      vol that varies only with time. A <em>local volatility</em> model makes
      volatility depend on the stock price itself. Here, markets are calm
      when they rally and turbulent when they fall:
    </p>
    <p class="display">
      σ(S) = <DraggableNumber bind:value={p.smileBase} min={0.05} max={0.4} step={0.005} label="base vol" format={volFmt} />
      + <DraggableNumber bind:value={p.smileRange} min={0} max={0.4} step={0.005} label="vol range" format={volFmt} />
      / (1 + e<sup><DraggableNumber bind:value={p.smileStretch} min={0} max={0.3} step={0.005} label="stretch" format={(v) => fmt(v, 3)} />(S − {p.spot})</sup>)
    </p>

    {#if localVolCurve.value}
      <figure>
        <LineChart
          ariaLabel="Local volatility as a function of the stock price"
          series={[{name: 'local vol σ(S)', color: 'var(--series-1)', points: localVolCurve.value}]}
          xFormat={(v) => fmt(v, 0)}
          yFormat={(v) => pct(v)}
          xLabel="stock price S"
          markers={[{x: p.spot, label: 'spot'}]}
        />
      </figure>
    {/if}

    <p>
      With vol depending on S, the CRR recipe no longer recombines either. The
      Derman–Kani <em>implied tree</em> keeps the centre of the tree at spot,
      then works outwards, placing each node so that the step from its parent
      matches both the forward price F and the local variance σ(S)²S²Δt:
    </p>
    <p class="display">
      S<sub>up</sub> = F + σ(S)² S² Δt / (F − S<sub>down</sub>)
    </p>

    <pre><code>SigmoidSmileVol smile{'{'}.base = <DraggableNumber bind:value={p.smileBase} min={0.05} max={0.4} step={0.005} label="base vol" format={decimalVol} />, .range = <DraggableNumber bind:value={p.smileRange} min={0} max={0.4} step={0.005} label="vol range" format={decimalVol} />,
                      .stretch = <DraggableNumber bind:value={p.smileStretch} min={0} max={0.3} step={0.005} label="stretch" format={decimalVol} />, .center = 100{'}'};
StochasticTreeModel&lt;LocalVolatilityPropagator&gt; asset(
    BinomialTree(1.0, 1 / <DraggableNumber bind:value={p.smileStepsPerYear} min={4} max={40} step={1} label="steps per year" />.), LocalVolatilityPropagator(curve, 100));
asset.forwardPropagate(Volatility(smile));</code></pre>

    {#if localTree.error}
      <div class="error" role="alert">{localTree.error}</div>
    {:else if localTree.result}
      <figure>
        <TreeView result={localTree.result} mode="asset" bind:hovered={localHover} />
        <figcaption>
          {#if localHoverVol}
            S = {fmt(localHoverVol.s)}, where local vol is {volFmt(localHoverVol.vol)}.
          {:else}
            Below spot, where vol is higher, the nodes spread further apart than
            above it. Hover a node to read its local vol.
          {/if}
        </figcaption>
      </figure>
    {/if}
  </section>

  <section>
    <h2>4. From local vol to the implied smile</h2>
    <p>
      Now price European calls at every strike from 70 to 130 on a fine local
      vol tree, and ask Black–Scholes which single vol would give each price.
      Those implied vols trace out a skew, like the ones seen in equity index
      markets.
    </p>

    {#if impliedSmile?.error}
      <div class="error" role="alert">{impliedSmile.error}</div>
    {:else if impliedSmile?.value && localVolCurve.value}
      <figure>
        <LineChart
          ariaLabel="Implied volatility by strike, compared with local volatility by stock price"
          series={[
            {
              name: 'local vol, by stock price',
              color: 'var(--series-1)',
              points: localVolCurve.value.filter((d) => d.x >= 70 && d.x <= 130),
            },
            {name: 'implied vol, by strike', color: 'var(--series-2)', points: impliedSmile.value},
          ]}
          xFormat={(v) => fmt(v, 0)}
          yFormat={(v) => pct(v)}
          xLabel="stock price / strike"
          markers={[{x: p.spot, label: 'spot'}]}
        />
      </figure>
      {#if slopes && Number.isFinite(slopes.ratio)}
        <p>
          Near the money, implied vol changes by {fmt(slopes.implied * 100, 3)}
          points per unit of strike, while local vol changes by
          {fmt(slopes.local * 100, 3)} points per unit of stock price: a ratio
          of <strong>{fmt(slopes.ratio, 2)}</strong>. This is Derman's "rule of
          two": an option's implied vol is roughly an average of local vol over
          the paths between spot and the strike, so it changes about half as
          fast.
        </p>
      {/if}
    {/if}
  </section>

  <footer>
    Built with <a href="https://github.com/phaedon/smile-explorer">smile-explorer</a>,
    following Derman's <i>The Volatility Smile</i> (chapters 13–14) and Derman
    &amp; Kani, <i>The Volatility Smile and Its Implied Tree</i> (1994).
  </footer>
</article>
