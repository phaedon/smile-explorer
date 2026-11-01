<!--
  A trinomial tree: time on the x axis, and on the y axis the unfitted state
  x = j·Δx, a Hull-White tree's fitted short rate x + α(t), or a commodity
  tree's price e^(x + α(t)). Each node
  branches to three successors; nodes on the edges, where branching bends
  inwards, are drawn as diamonds. Nodes can be coloured by a per-node value
  (on a ramp from its smallest to its largest value), with only the time slices in [shownFrom, shownTo] treated as computed, so
  that a forward or backward pass can be stepped through. Nodes whose value is
  NaN (an option after its expiry, say) are drawn faded, and `marked` nodes
  (early exercise) get a ring. Hovering snaps to the nearest node.
-->
<script lang="ts">
  import {niceTicks, tickFormat} from './format';

  // The parts of a tree result this draws: HullWhiteTreeResult and
  // CommodityTreeResult both have them.
  export type TrinomialLayout = {
    times: Float64Array;
    sliceOffsets: Int32Array;
    successorMid: Int32Array;
    branchStyle: Int32Array;
    x: Float64Array;
    shortRates?: Float64Array;
    spots?: Float64Array;
  };

  let {
    tree,
    yMode,
    values = null,
    shownFrom = 0,
    shownTo = Number.POSITIVE_INFINITY,
    marked = null,
    lastSlice = Number.POSITIVE_INFINITY,
    hovered = $bindable(null),
    xTicks: xTickValues = null,
    xFormat = (v: number) => `${v}y`,
  }: {
    tree: TrinomialLayout;
    // Rates are shown in percent, prices in dollars.
    yMode: 'x' | 'rate' | 'price';
    // Per-node values to colour by (on a sequential ramp), or null.
    values?: Float64Array | null;
    shownFrom?: number;
    shownTo?: number;
    // Per-node flags for nodes to ring, or null.
    marked?: boolean[] | null;
    // Draw only the time slices up to this one (an option's expiry, say).
    lastSlice?: number;
    // Index of the hovered node.
    hovered?: number | null;
    // Times (in years) to label on the x axis, and how; by default, round
    // numbers of years.
    xTicks?: number[] | null;
    xFormat?: (v: number) => string;
  } = $props();

  const W = 760;
  const H = 340;
  const PAD = {left: 56, right: 20, top: 14, bottom: 34};

  let svg: SVGSVGElement;

  const T = $derived(Math.min(tree.times.length - 1, lastSlice));
  // Nodes are stored slice by slice, so the visible ones are a prefix.
  const ys = $derived(
    (yMode === 'x' ? tree.x : ((yMode === 'rate' ? tree.shortRates : tree.spots) ?? tree.x)).subarray(
      0,
      tree.sliceOffsets[T + 1],
    ),
  );
  const sliceOf = $derived.by(() => {
    const out = new Int32Array(ys.length);
    for (let t = 0; t <= T; ++t) {
      for (let n = tree.sliceOffsets[t]; n < tree.sliceOffsets[t + 1]; ++n) out[n] = t;
    }
    return out;
  });

  const lo = $derived(Math.min(...ys));
  const hi = $derived(Math.max(...ys));
  const x = (t: number) =>
    PAD.left + (T > 0 ? tree.times[t] / tree.times[T] : 0.5) * (W - PAD.left - PAD.right);
  const y = (v: number) =>
    PAD.top + (hi > lo ? 1 - (v - lo) / (hi - lo) : 0.5) * (H - PAD.top - PAD.bottom);

  // The closest two nodes in a slice (prices on a commodity tree are closer
  // at the bottom).
  const minGap = $derived.by(() => {
    let gap = Number.POSITIVE_INFINITY;
    for (let n = 1; n < ys.length; ++n) {
      if (sliceOf[n] === sliceOf[n - 1]) gap = Math.min(gap, ys[n] - ys[n - 1]);
    }
    return gap;
  });

  // Small enough that neighbouring nodes don't touch, in either direction.
  const radius = $derived.by(() => {
    const dx = (W - PAD.left - PAD.right) / Math.max(1, T);
    const dy = hi > lo && Number.isFinite(minGap) ? ((H - PAD.top - PAD.bottom) * minGap) / (hi - lo) : 40;
    return Math.max(1.2, Math.min(6, dx / 3, dy / 3));
  });

  const shown = (t: number) => t >= shownFrom && t <= shownTo;
  // The ramp spans the values' own range: bond values, say, all sit near
  // their face value.
  const finiteValues = $derived(values ? Array.from(values).filter(Number.isFinite) : []);
  const minValue = $derived(finiteValues.length ? Math.min(...finiteValues) : 0);
  const maxValue = $derived(finiteValues.length ? Math.max(...finiteValues) : 0);
  const valueless = (n: number) => values !== null && !Number.isFinite(values[n]);
  function fillFor(n: number): string {
    if (!values) return 'var(--series-1)';
    if (!shown(sliceOf[n]) || valueless(n)) return 'var(--surface)';
    if (!(maxValue > minValue)) return 'var(--seq-5)';
    const level = (values[n] - minValue) / (maxValue - minValue);
    return `var(--seq-${Math.min(9, Math.floor(level * 10))})`;
  }

  const edges = $derived.by(() => {
    let d = '';
    for (let n = 0; n < ys.length; ++n) {
      const m = tree.successorMid[n];
      const t = sliceOf[n];
      if (m < 0 || t >= T) continue;
      const from = `M${x(t)},${y(ys[n])}`;
      for (const k of [m - 1, m, m + 1]) d += `${from}L${x(t + 1)},${y(ys[k])}`;
    }
    return d;
  });

  const hoverEdges = $derived.by(() => {
    if (hovered === null || tree.successorMid[hovered] < 0 || sliceOf[hovered] >= T) return '';
    const n = hovered;
    const m = tree.successorMid[n];
    const t = sliceOf[n];
    return [m - 1, m, m + 1]
      .map((k) => `M${x(t)},${y(ys[n])}L${x(t + 1)},${y(ys[k])}`)
      .join('');
  });

  const yTicks = $derived(niceTicks(lo, hi, 5).filter((v) => v >= lo - 1e-12 && v <= hi + 1e-12));
  // Rates are shown in percent, prices in dollars.
  const yScale = $derived(yMode === 'price' ? 1 : 100);
  const yTickFmt = $derived(tickFormat(yTicks.map((v) => Number((v * yScale).toPrecision(10)))));
  const yLabel = (v: number) => {
    const text = yTickFmt(Number((v * yScale).toPrecision(10)));
    return yMode === 'price' ? `$${text}` : `${text}%`;
  };
  const xTicks = $derived(
    (xTickValues ?? niceTicks(0, tree.times[T], 5)).filter((v) => v <= tree.times[T] + 1e-9),
  );

  function onpointermove(e: PointerEvent) {
    const ctm = svg.getScreenCTM();
    if (!ctm) return;
    const p = new DOMPoint(e.clientX, e.clientY).matrixTransform(ctm.inverse());
    let best = -1;
    let bestDist = Number.POSITIVE_INFINITY;
    for (let n = 0; n < ys.length; ++n) {
      const dist = (x(sliceOf[n]) - p.x) ** 2 + (y(ys[n]) - p.y) ** 2;
      if (dist < bestDist) {
        best = n;
        bestDist = dist;
      }
    }
    hovered = bestDist < 40 ** 2 ? best : null;
  }
</script>

<svg
  bind:this={svg}
  viewBox="0 0 {W} {H}"
  role="img"
  aria-label="Trinomial tree with {T} steps and {ys.length} nodes"
  {onpointermove}
  onpointerleave={() => (hovered = null)}
>
  {#each yTicks as v (v)}
    <line class="grid" x1={PAD.left} x2={W - PAD.right} y1={y(v)} y2={y(v)} />
    <text class="tick" x={PAD.left - 8} y={y(v)} text-anchor="end" dy="0.32em"
      >{yLabel(v)}</text
    >
  {/each}
  {#each xTicks as v (v)}
    <text class="tick" x={x(0) + (v / tree.times[T]) * (W - PAD.left - PAD.right)} y={H - PAD.bottom + 20} text-anchor="middle"
      >{xFormat(v)}</text
    >
  {/each}

  <path class="edges" d={edges} />
  <path class="hover-edges" d={hoverEdges} />

  {#each ys as v, n (n)}
    {@const cx = x(sliceOf[n])}
    {@const cy = y(v)}
    {#if tree.branchStyle[n] === 0}
      <circle
        class="node"
        class:pending={values !== null && !shown(sliceOf[n])}
        class:valueless={valueless(n)}
        {cx}
        {cy}
        r={radius}
        style:fill={fillFor(n)}
      />
    {:else}
      <rect
        class="node edge"
        class:pending={values !== null && !shown(sliceOf[n])}
        class:valueless={valueless(n)}
        x={cx - radius}
        y={cy - radius}
        width={radius * 2}
        height={radius * 2}
        transform="rotate(45 {cx} {cy})"
        style:fill={fillFor(n)}
      />
    {/if}
  {/each}

  {#if marked}
    {#each ys as v, n (n)}
      {#if marked[n] && shown(sliceOf[n])}
        <circle class="mark" cx={x(sliceOf[n])} cy={y(v)} r={radius + 2.5} />
      {/if}
    {/each}
  {/if}

  {#if hovered !== null}
    <circle class="ring" cx={x(sliceOf[hovered])} cy={y(ys[hovered])} r={radius + 4} />
  {/if}
</svg>

<style>
  svg {
    display: block;
    width: 100%;
    height: auto;
    font-family: var(--font-ui);
    touch-action: pan-y;
  }
  .grid {
    stroke: var(--gridline);
  }
  .tick {
    fill: var(--text-muted);
    font-size: 12px;
    font-variant-numeric: tabular-nums;
  }
  .edges {
    stroke: var(--border);
    stroke-width: 1;
    fill: none;
  }
  .hover-edges {
    stroke: var(--text-primary);
    stroke-width: 2;
    fill: none;
  }
  .node {
    stroke: var(--surface);
    stroke-width: 1;
  }
  .node.edge {
    stroke: var(--series-2);
    stroke-width: 2;
  }
  .node.pending {
    stroke: var(--text-muted);
  }
  .node.valueless {
    stroke: var(--border);
  }
  .mark {
    fill: none;
    stroke: var(--text-primary);
    stroke-width: 1.5;
  }
  .ring {
    fill: none;
    stroke: var(--text-primary);
    stroke-width: 2;
  }
</style>
