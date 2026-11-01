<!--
  "Target redemption forwards": a strip of FX forwards at a better strike,
  which ends once the customer's gains reach a target. Priced by Monte Carlo,
  live: the paths are drawn as the worker simulates them.
-->
<script lang="ts">
  import {onDestroy, onMount, untrack} from 'svelte';

  import BarChart from './BarChart.svelte';
  import DraggableNumber from './DraggableNumber.svelte';
  import Histogram from './Histogram.svelte';
  import LineChart from './LineChart.svelte';
  import PathFan from './PathFan.svelte';
  import SimControls from './SimControls.svelte';
  import ToggleToken from './ToggleToken.svelte';
  import {engine, type TarfValuation} from './engine';
  import {fmt, pct} from './format';
  import type {AnalysisJob, AnalysisReply} from './tarfAnalysisWorker';
  import {NOTIONAL, STEPS_PER_FIXING, tarfParams as p, tarfSpecFor} from './tarfParams.svelte';
  import {BLOCK_PATHS, RECORDED_PATHS, SEED, TarfRun, type RecordedPaths} from './tarfRun.svelte';

  const TOTAL_PATHS = 10_000;
  const VALUATION_PATHS = 20_000;
  const SWEEP_PATHS = 4000;

  // Dollars, compactly: $198k, −$1.24m.
  function usd(v: number): string {
    if (!Number.isFinite(v)) return '–';
    const sign = v < 0 ? '−' : '';
    const a = Math.abs(v);
    if (a >= 1e6) return `${sign}$${(a / 1e6).toFixed(2)}m`;
    if (a >= 1e4) return `${sign}$${(a / 1e3).toFixed(0)}k`;
    if (a >= 1e3) return `${sign}$${(a / 1e3).toFixed(1)}k`;
    return `${sign}$${a.toFixed(0)}`;
  }
  const rate = (v: number) => fmt(v, 4);
  const cents = (v: number) => fmt(v * 100, 1);
  const monthLabel = (m: number) => `${fmt(m, 0)}m`;

  // ---- The zero-cost strike, and the sweeps ------------------------------------

  const VOLS = [0.04, 0.06, 0.08, 0.1, 0.12, 0.14, 0.16];
  const TARGETS = [50_000, 100_000, 150_000, 200_000, 300_000, 400_000, 500_000];
  const LEVERAGES = [1, 1.5, 2, 2.5, 3];

  // The terms the zero-cost strike depends on (everything but the strike).
  const termsKey = $derived(`${p.vol}|${p.target}|${p.leverage}|${p.fixings}`);

  type Sweeps = Extract<AnalysisReply, {kind: 'sweeps'}>;
  let valuation = $state<(TarfValuation & {key: string}) | null>(null);
  let sweeps = $state<Sweeps | null>(null);
  let analysisError = $state<string | null>(null);

  // One job at a time: while dragging, only the latest terms wait their turn.
  const analysis = new Worker(new URL('./tarfAnalysisWorker.ts', import.meta.url), {type: 'module'});
  let jobId = 0;
  let inFlight: {id: number; key: string} | null = null;
  let queued: {job: AnalysisJob; key: string} | null = null;
  function send(job: AnalysisJob, key: string) {
    if (inFlight) {
      queued = {job, key};
      return;
    }
    inFlight = {id: job.id, key};
    analysis.postMessage(job);
  }
  analysis.onmessage = (e: MessageEvent<AnalysisReply>) => {
    const reply = e.data;
    if (!inFlight || reply.id !== inFlight.id) return;
    if (reply.error !== undefined) {
      analysisError = reply.error;
    } else if (reply.kind === 'valuation') {
      analysisError = null;
      if (p.zeroCost && inFlight.key === termsKey) p.strike = Number(reply.valuation.zeroCostStrike.toFixed(4));
      valuation = {...reply.valuation, key: inFlight.key};
      return;
    } else {
      sweeps = reply;
    }
    inFlight = null;
    if (queued) {
      const next = queued;
      queued = null;
      send(next.job, next.key);
    }
  };
  analysis.onerror = (e) => {
    analysisError = `the valuation failed to load (${e.message || 'worker error'})`;
  };
  $effect(() => {
    const job: AnalysisJob = {
      id: ++jobId,
      tarf: tarfSpecFor(p, {stepsPerFixing: 1}),
      valuationPaths: VALUATION_PATHS,
      sweepPaths: SWEEP_PATHS,
      vols: VOLS,
      targets: TARGETS,
      leverages: LEVERAGES,
    };
    const key = termsKey;
    untrack(() => send(job, key));
  });

  function useZeroCost() {
    p.zeroCost = true;
    if (valuation?.key === termsKey) p.strike = Number(valuation.zeroCostStrike.toFixed(4));
  }

  // With the strike following the zero-cost strike, the run waits until it
  // has been solved for the current terms.
  const ready = $derived(!p.zeroCost || valuation?.key === termsKey);
  const runSpec = $derived(ready ? tarfSpecFor(p) : null);

  // ---- The live run ------------------------------------------------------------

  let spotFan = $state<PathFan>();
  let profitFan = $state<PathFan>();

  function onPaths(paths: RecordedPaths) {
    const n = paths.fixingTimes.length;
    spotFan?.append(paths.spots, paths.times.length, paths.knockouts);
    // Accumulated profit from time 0 (nothing yet), then after each fixing.
    const rows = new Float64Array(paths.count * (n + 1));
    for (let r = 0; r < paths.count; ++r) {
      rows[r * (n + 1)] = 0;
      rows.set(paths.profits.subarray(r * n, (r + 1) * n), r * (n + 1) + 1);
    }
    profitFan?.append(rows, n + 1, paths.knockouts);
  }

  const run = new TarfRun(TOTAL_PATHS, onPaths, () => {
    spotFan?.clear();
    profitFan?.clear();
  });
  let started = false;
  $effect(() => {
    const spec = runSpec;
    if (!spec) return;
    untrack(() => run.restart(spec, !started || run.status === 'paused' ? 'stop' : 'play'));
  });

  // Start the run when the fan first scrolls into view.
  let fanFigure: HTMLElement;
  onMount(() => {
    const observer = new IntersectionObserver(
      (entries) => {
      if (entries.some((e) => e.isIntersecting)) {
        observer.disconnect();
        started = true;
        if (run.status === 'idle') run.play();
      }
      },
      {threshold: 0.3},
    );
    observer.observe(fanFigure);
    return () => observer.disconnect();
  });
  onDestroy(() => {
    run.destroy();
    analysis.terminate();
  });

  // Axes in months: every simulation step for the FX rate, and the fixings
  // for the accumulated profit.
  const spotXs = $derived(Array.from({length: p.fixings * STEPS_PER_FIXING + 1}, (_, k) => k / STEPS_PER_FIXING));
  const fixingXs = $derived(Array.from({length: p.fixings + 1}, (_, k) => k));
  const spotDomain = $derived.by((): [number, number] => {
    const spread = 2.6 * p.vol * Math.sqrt(p.fixings / 12);
    return [
      Math.floor(Math.min(p.spot * Math.exp(-spread), p.strike - 0.01) * 50) / 50,
      Math.ceil(p.spot * Math.exp(spread * 0.8) * 50) / 50,
    ];
  });
  // The forward at each step, from the two curves.
  const forwardCurve = $derived.by(() => {
    const spec = tarfSpecFor(p);
    const times = spotXs.map((m) => m / 12);
    const domestic = engine.evaluateCurve(spec.domestic, times);
    const foreign = engine.evaluateCurve(spec.foreign, times);
    return times.map((_, k) => (p.spot * foreign[k]) / domestic[k]);
  });

  // The live statistics, recomputed as paths arrive.
  const stats = $derived.by(() => {
    void run.version;
    const n = run.done;
    const npvs = run.npvs.subarray(0, n);
    const counts = new Array<number>(p.fixings + 1).fill(0);
    let koSum = 0;
    let koCount = 0;
    let neverSum = 0;
    let lifeSum = 0;
    for (let k = 0; k < n; ++k) {
      const ko = run.knockouts[k];
      if (ko >= 0) {
        counts[ko] += 1;
        koSum += npvs[k];
        koCount += 1;
        lifeSum += ko + 1;
      } else {
        counts[p.fixings] += 1;
        neverSum += npvs[k];
        lifeSum += p.fixings;
      }
    }
    const sorted = Float64Array.from(npvs).sort();
    const quantile = (q: number) => (n ? sorted[Math.min(n - 1, Math.floor(q * n))] : NaN);
    return {
      n,
      npvs,
      counts,
      koShare: n ? koCount / n : NaN,
      koMean: koCount ? koSum / koCount : NaN,
      neverMean: n - koCount ? neverSum / (n - koCount) : NaN,
      meanLife: n ? lifeSum / n : NaN,
      p1: quantile(0.01),
      p5: quantile(0.05),
      median: quantile(0.5),
      worst: n ? sorted[0] : NaN,
      best: n ? sorted[n - 1] : NaN,
    };
  });

  const koBars = $derived(
    stats.counts.map((value, k) =>
      k < p.fixings ? {label: String(k + 1), value, secondary: true} : {label: 'never', value},
    ),
  );

  const historySeries = $derived([
    {name: 'estimate', color: 'var(--series-1)', points: run.history.map((h) => ({x: h.paths, y: h.mean}))},
    {
      name: '95% band, upper',
      color: 'var(--text-muted)',
      points: run.history.filter((h) => Number.isFinite(h.halfWidth)).map((h) => ({x: h.paths, y: h.mean + h.halfWidth})),
    },
    {
      name: '95% band, lower',
      color: 'var(--text-muted)',
      points: run.history.filter((h) => Number.isFinite(h.halfWidth)).map((h) => ({x: h.paths, y: h.mean - h.halfWidth})),
    },
  ]);

  // ---- One path, fixing by fixing -----------------------------------------------

  let showHit = $state(true);
  // The first path of the run that reaches the target after a few fixings,
  // and the first that never does (searching the first few blocks).
  const examples = $derived.by(() => {
    if (!runSpec) return null;
    const rowLength = p.fixings * STEPS_PER_FIXING + 1;
    let hit = null;
    let miss = null;
    try {
      for (let block = 0; block < 8 && !(hit && miss); ++block) {
        const b = engine.simulateTarfBatch({
          tarf: runSpec,
          paths: BLOCK_PATHS,
          seed: SEED,
          batchIndex: block,
          recordedPaths: BLOCK_PATHS,
        });
        for (let r = 0; r < BLOCK_PATHS; ++r) {
          const ko = b.knockoutFixings[r];
          const path = {
            index: block * BLOCK_PATHS + r + 1,
            ko,
            npv: b.npvs[r],
            spots: b.spots.slice(r * rowLength, (r + 1) * rowLength),
            payments: b.payments.slice(r * p.fixings, (r + 1) * p.fixings),
            profits: b.accumulatedProfits.slice(r * p.fixings, (r + 1) * p.fixings),
          };
          if (!hit && ko >= 2) hit = path;
          if (!miss && ko < 0) miss = path;
        }
      }
    } catch {
      return null;
    }
    return {hit, miss};
  });
  const example = $derived(examples ? ((showHit ? examples.hit : examples.miss) ?? examples.hit ?? examples.miss) : null);
  const exampleRows = $derived.by(() => {
    if (!example) return [];
    const out = [];
    for (let i = 0; i < p.fixings && Number.isFinite(example.payments[i]); ++i) {
      out.push({
        month: i + 1,
        fixing: example.spots[(i + 1) * STEPS_PER_FIXING],
        payment: example.payments[i],
        profit: example.profits[i],
        last: i === example.ko,
      });
    }
    return out;
  });
  const exampleSeries = $derived(
    example
      ? [
          {
            name: 'EUR-USD',
            color: example.ko >= 0 ? 'var(--series-2)' : 'var(--series-1)',
            points: spotXs.map((m, k) => ({x: m, y: example.spots[k]})).filter((pt) => Number.isFinite(pt.y)),
          },
          {name: 'strike', color: 'var(--text-muted)', points: [{x: 0, y: p.strike}, {x: p.fixings, y: p.strike}]},
        ]
      : [],
  );

  // ---- The terms -----------------------------------------------------------------

  const forward = $derived(valuation?.weightedForward ?? NaN);
  const discount = $derived(forward - p.strike);
  // Typical size of a gaining fixing at the start: spot less the strike.
  const firstGain = $derived((p.spot - p.strike) * NOTIONAL);

  const strikeVsTarget = $derived(
    sweeps
      ? [
          {name: 'zero-cost strike', color: 'var(--series-1)', points: TARGETS.map((t, k) => ({x: t / 1e3, y: sweeps!.target.strikes[k]}))},
          {name: 'forward', color: 'var(--text-muted)', points: TARGETS.map((t) => ({x: t / 1e3, y: sweeps!.target.forward}))},
        ]
      : [],
  );
  const strikeVsLeverage = $derived(
    sweeps
      ? [
          {name: 'zero-cost strike', color: 'var(--series-1)', points: LEVERAGES.map((l, k) => ({x: l, y: sweeps!.leverage.strikes[k]}))},
          {name: 'forward', color: 'var(--text-muted)', points: LEVERAGES.map((l) => ({x: l, y: sweeps!.leverage.forward}))},
        ]
      : [],
  );
  const npvVsVol = $derived(
    sweeps
      ? [{name: 'NPV to the company', color: 'var(--series-1)', points: VOLS.map((v, k) => ({x: v * 100, y: sweeps!.vol.npvs[k]}))}]
      : [],
  );

  // The 95% half-width of one sweep point's own Monte Carlo error.
  const sweepNoise = $derived(1.96 * (valuation?.standardError ?? NaN) * Math.sqrt(VALUATION_PATHS / SWEEP_PATHS));

  const error = $derived(run.error ?? analysisError);
</script>

<article class="essay">
  <header>
    <p class="kicker">smile-explorer · interactive notes</p>
    <h1>Target redemption forwards</h1>
    <p class="lede">
      A company that buys euros every month can fix its rate with a strip of
      forwards. A target redemption forward offers a better rate, on
      conditions: the company's gains are capped by a target, and the trade
      ends as soon as they reach it, while its losses are not capped and count
      double. Pricing that means following each possible future to see when,
      or whether, it ends. This is the first Monte Carlo simulation in these
      essays, and it runs live in your browser.
      <span class="hint">Drag any <span class="sample">underlined number</span>.</span>
    </p>
  </header>

  {#if error}
    <div class="error sticky" role="alert">
      <strong>The engine rejected these terms:</strong>
      {error}.
    </div>
  {/if}

  <section>
    <h2>1. A better rate, on conditions</h2>
    <p>
      Take an American company that pays for €1m of imports every month for
      the next <DraggableNumber bind:value={p.fixings} min={6} max={24} label="months" /> months.
      Forwards on the <a href="#/fx">currency essay's</a> curves would fix its
      rate at about <strong>{rate(forward)}</strong> dollars a euro (the
      average of the monthly forwards, weighted by their discount factors).
      A bank offers it a TARF at
      <DraggableNumber
        bind:value={() => p.strike, (v) => {
          p.strike = v;
          p.zeroCost = false;
        }}
        min={0.9}
        max={1.3}
        step={0.0005}
        label="strike"
        format={rate}
      />
      instead, {cents(discount)} cents better{#if p.zeroCost}, the strike at
        which the TARF costs nothing{:else}
        (<button type="button" class="inline" onclick={useZeroCost}>use the zero-cost strike</button>){/if}. The terms:
    </p>
    <ul>
      <li>
        Each month the company buys €1m at the strike. When the month's
        fixing is above the strike, it gains: at today's spot of
        {rate(p.spot)}, about {usd(firstGain)} a month.
      </li>
      <li>
        Its gains count towards a target of
        <DraggableNumber bind:value={p.target} min={50_000} max={500_000} step={10_000} label="target" format={usd} />.
        At the fixing that reaches it the trade ends, paying only what is
        left to reach the target exactly.
      </li>
      <li>
        When the fixing is below the strike, the company loses, on
        <DraggableNumber bind:value={p.leverage} min={1} max={3} step={0.5} label="leverage" format={(v) => `${v}×`} />
        the notional. Losses don't count towards the target, and don't end the
        trade.
      </li>
    </ul>
    <p>
      Here is one simulated future, fixing by fixing: path {example?.index ?? '–'} of
      the run below, chosen as
      <ToggleToken bind:value={showHit} on="one that reaches the target" off="one that doesn't" label="example path" />.
    </p>
    {#if example}
      <figure>
        <LineChart
          series={exampleSeries}
          xLabel="months"
          xFormat={monthLabel}
          yFormat={rate}
          markers={example.ko >= 0 ? [{x: example.ko + 1, label: 'target reached'}] : []}
          height={200}
          ariaLabel="One simulated path of EUR-USD against the strike"
        />
        <div class="table-wrap">
          <table>
            <thead>
              <tr><th>Month</th><th>Fixing</th><th>Payment</th><th>Gains so far</th></tr>
            </thead>
            <tbody>
              {#each exampleRows as row (row.month)}
                <tr class:last={row.last}>
                  <th>{row.month}</th>
                  <td>{rate(row.fixing)}</td>
                  <td class:loss={row.payment < 0}>{usd(row.payment)}</td>
                  <td>{usd(row.profit)}{row.last ? ' · target' : ''}</td>
                </tr>
              {/each}
            </tbody>
          </table>
        </div>
        <figcaption>
          {#if example.ko >= 0}
            The target is reached in month {example.ko + 1}, so the last payment
            is cut to land on it and the trade ends. Worth {usd(example.npv)}
            to the company, discounted.
          {:else}
            The euro falls below the strike, and the losses, at {p.leverage}× the
            notional, keep coming until the last month. Worth {usd(example.npv)}
            to the company, discounted.
          {/if}
        </figcaption>
      </figure>
    {/if}
  </section>

  <section>
    <h2>2. Thousands of futures, as they arrive</h2>
    <p>
      One path tells a story; a price needs thousands of them. Each path
      below follows the exchange rate with a flat vol of
      <DraggableNumber bind:value={p.vol} min={0.04} max={0.16} step={0.01} label="vol" format={(v) => pct(v)} />,
      drifting at the dollar rate less the euro rate as in the
      <a href="#/fx">currency essay</a>, until it reaches the target or the
      months run out. The TARF is worth the average, over all the paths, of
      its discounted payments.
    </p>
    <SimControls {run} />
    <figure bind:this={fanFigure}>
      <PathFan
        bind:this={spotFan}
        xs={spotXs}
        yDomain={spotDomain}
        xFormat={monthLabel}
        yFormat={rate}
        xLabel="months"
        references={[
          {label: 'strike', values: p.strike},
          {label: 'forward', values: forwardCurve},
        ]}
        height={280}
        ariaLabel="Simulated paths of EUR-USD"
      />
      <PathFan
        bind:this={profitFan}
        xs={fixingXs}
        yDomain={[0, p.target * 1.08]}
        xFormat={monthLabel}
        yFormat={usd}
        xLabel="months"
        references={[{label: 'target', values: p.target}]}
        yTickFormat={usd}
        height={180}
        ariaLabel="Gains accumulated towards the target on each path"
      />
      <figcaption>
        Above, the exchange rate on each path; below, the gains it has
        accumulated towards the target after each fixing. A path stops, at a dot, in the month
        its gains reach the target. Only the first {RECORDED_PATHS.toLocaleString('en-US')}
        paths are drawn; the run carries on pricing. Hover to read the spread.
      </figcaption>
    </figure>

    <p>
      The paths come from a worker, a block of {BLOCK_PATHS} at a time, and the
      page asks for the next block only after drawing the last, so the page
      stays responsive however slow the device. Each block has its own
      random seed, so pausing, stepping or changing speed never changes the
      paths, and starting over replays them.
    </p>
    <p class="stat">
      After {stats.n.toLocaleString('en-US')} paths: worth {usd(run.mean)} to the company,
      ± {usd(run.halfWidth)} (95%).
    </p>
    {#if run.history.length > 1}
      <figure>
        <LineChart
          series={historySeries}
          xLabel="paths"
          xFormat={(v) => `${v / 1000}k`}
          yFormat={usd}
          height={200}
          ariaLabel="The running estimate of the TARF's value, with its 95% band"
        />
        <figcaption>
          The estimate settles as paths arrive, and its band narrows like one
          over the square root of the number of paths: four times the paths for
          half the width.
          {#if p.zeroCost}At the zero-cost strike it should settle within a few
            thousand dollars of zero. The strike was solved on a different set of
            paths, so it's not exactly zero on these.{/if}
        </figcaption>
      </figure>
    {/if}
    {#if stats.n > 0}
      <figure>
        <Histogram
          samples={stats.npvs}
          bins={40}
          domain={[stats.p1, stats.best]}
          xLabel="value to the company"
          xFormat={usd}
          markers={[{x: run.mean, label: 'average'}]}
          ariaLabel="Distribution of the TARF's value across paths"
        />
        <figcaption>
          The value of each path to the company. Most cluster just below the
          target; the rest trail far to the left, and the worst 1% are beyond
          the left edge.
        </figcaption>
      </figure>
    {/if}
  </section>

  <section>
    <h2>3. Why not a tree?</h2>
    <p>
      Every other essay priced on a tree. A tree works because an option's
      value at a node depends only on the price there: paths that recombine
      at a node share the same future. A TARF's future also depends on how much of
      the target is used up. Two paths that reach the same exchange rate in
      month six, one with {usd(0.9 * p.target)} of gains behind it and one with
      none, are different contracts from then on: one is almost over, and the
      other has the whole target still to go.
    </p>
    <p>
      A tree can still do it, by carrying the accumulated gain as a second
      state: at each node, values for a range of gains, interpolated between
      them. That is the approach Hull describes for Asian options, whose
      payoff depends on an average. Monte Carlo is simpler. It follows each
      path as it is, and the path dependence comes for free. The price is
      noise. A tree's error shrinks steadily as you add steps (the
      <a href="#/binomial">binomial essay's</a> convergence chart), while
      Monte Carlo's shrinks only as one over the square root of the number of
      paths, and randomly. The <a href="#/greeks">Greeks essay</a> simulated
      paths too, but to test a hedge, not to price.
    </p>
  </section>

  <section>
    <h2>4. Heads you win a little, tails you lose a lot</h2>
    {#if stats.n > 0}
      <figure>
        <BarChart
          bars={koBars}
          total={stats.n}
          ariaLabel="The month each path reached the target, or never"
        />
        <figcaption>
          The month each path reached the target, out of {stats.n.toLocaleString('en-US')}
          paths so far. On average the trade lasts {fmt(stats.meanLife, 1)} of
          its {p.fixings} months.
        </figcaption>
      </figure>
      <p>
        {pct(stats.koShare)} of the paths reach the target, most of them in
        the first few months, because the strike starts so far below the
        forward. They are worth {usd(stats.koMean)} each on average: close to
        the target, which is the most the company can ever gain. The other
        {pct(1 - stats.koShare)} are the futures where the euro fell. Each
        of those is worth {usd(stats.neverMean)} on average, and the worst
        5% of all paths lose {usd(-stats.p5)} or more, with the very worst
        losing {usd(-stats.worst)}.
      </p>
    {:else}
      <p class="muted">Run the simulation above to fill this in.</p>
    {/if}
    <p>
      That is the shape of the deal. The company has bought a strip of
      monthly calls on the euro, struck at the strike, whose total payout is
      capped at the target. It has sold {p.leverage}× as many puts at the same
      strike, with no cap, and the trade's early end takes away the calls
      just when they would pay. The better strike is what the bank pays for
      those puts.
    </p>
  </section>

  <section>
    <h2>5. The zero-cost strike, and who pays for it</h2>
    <p>
      With a target of
      <DraggableNumber bind:value={p.target} min={50_000} max={500_000} step={10_000} label="target" format={usd} />,
      leverage of
      <DraggableNumber bind:value={p.leverage} min={1} max={3} step={0.5} label="leverage" format={(v) => `${v}×`} />
      and vol of
      <DraggableNumber bind:value={p.vol} min={0.04} max={0.16} step={0.01} label="vol" format={(v) => pct(v)} />,
      the TARF costs nothing at a strike of
      <strong>{valuation ? rate(valuation.zeroCostStrike) : '–'}</strong>,
      {valuation ? cents(valuation.weightedForward - valuation.zeroCostStrike) : '–'}
      cents below the forward of {rate(forward)}.
      {#if valuation && !p.zeroCost}
        At your strike of {rate(p.strike)} it is worth {usd(valuation.npv)} to the company.
      {/if}
    </p>
    {#if sweeps}
      <figure>
        <LineChart
          series={strikeVsTarget}
          xLabel="target ($k)"
          yFormat={rate}
          height={190}
          ariaLabel="Zero-cost strike against the target"
        />
        <figcaption>
          A higher target lets the company keep more of its gains, so the
          strike it gets is less generous.
        </figcaption>
      </figure>
      <figure>
        <LineChart
          series={strikeVsLeverage}
          xLabel="leverage (×)"
          yFormat={rate}
          height={190}
          ariaLabel="Zero-cost strike against the leverage"
        />
        <figcaption>
          More leverage, more puts sold: a better strike in exchange for
          larger losses.
        </figcaption>
      </figure>
      <figure>
        <LineChart
          series={npvVsVol}
          xLabel="vol (%)"
          yFormat={usd}
          height={200}
          ariaLabel="The TARF's value to the company against vol, at the current strike"
        />
        <figcaption>
          The TARF's value to the company at a strike of {rate(p.strike)}, as
          vol changes. Vega is negative: the puts the company sold gain value
          with vol, while its calls can't, being capped by the target.
        </figcaption>
      </figure>
      <p>
        So a TARF struck in a calm market loses value for the company as soon
        as vol rises, before the exchange rate has moved at all. Each curve
        here uses the same random numbers at every point, so the differences
        between points come from the terms, not from Monte Carlo noise. With
        fresh paths for each point, the values would wobble by about
        ±{usd(sweepNoise)}.
      </p>
    {:else if !analysisError}
      <p class="muted">Valuing…</p>
    {/if}
  </section>

  <section>
    <h2>6. What this model leaves out</h2>
    <p>
      <strong>The smile.</strong> The vol here is flat. The company's losses
      come from the puts, struck well below the forward, and the market prices
      those at different vols from the at-the-money ones. The
      <a href="#/smile">smile essay's</a> local-vol model would price the TARF
      consistently with the market's options. That's a natural next step, and
      the difference it makes is a good measure of how much the smile matters.
    </p>
    <p>
      <strong>The details of real contracts.</strong> TARFs differ in what
      happens at the fixing that crosses the target (pay it in full, pay
      nothing, or pay exactly what is left, as here), may count the target in
      winning fixings rather than dollars, and fix on business days rather
      than in neat twelfths of a year.
    </p>
    <p>
      <strong>Greeks.</strong> Sensitivities from Monte Carlo are noisy.
      The curves above use the same random numbers at every point, which
      keeps the differences smooth. Even so, a TARF's delta jumps near the
      target, where a small move decides whether the trade ends this month.
    </p>
  </section>

  <footer>
    Built with <a href="https://github.com/phaedon/smile-explorer">smile-explorer</a>.
    The simulation is <code>derivatives/target_redemption_forward</code>; the
    terms are illustrative.
  </footer>
</article>

<style>
  ul {
    padding-left: 1.2em;
  }
  li {
    margin: 6px 0;
  }
  .table-wrap {
    overflow-x: auto;
    margin-top: 12px;
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
  tr.last td,
  tr.last th {
    font-weight: 600;
  }
  td.loss {
    color: var(--text-secondary);
  }
</style>
