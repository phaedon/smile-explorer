<!--
  A binomial tree: time on the x axis, the asset price (log scale) on the y
  axis. In "option" mode, nodes are coloured by option value, and only the
  time indices >= revealedFrom are shown as computed, so that backward
  induction can be stepped through. Hovering snaps to the nearest node.
-->
<script lang="ts">
  import {fmt, niceTicks} from './format';
  import {nodeIndex, type VanillaPricingResult} from './engine';

  export type NodeRef = {t: number; i: number};

  let {
    result,
    mode,
    strike,
    isAmerican = false,
    revealedFrom = 0,
    hovered = $bindable(null),
  }: {
    result: VanillaPricingResult;
    mode: 'asset' | 'option';
    strike?: number;
    isAmerican?: boolean;
    revealedFrom?: number;
    hovered?: NodeRef | null;
  } = $props();

  const W = 760;
  const H = 360;
  // The right padding leaves room for the strike label.
  const PAD = {left: 52, right: 64, top: 14, bottom: 34};

  let svg: SVGSVGElement;

  const T = $derived(result.expiryIndex);
  const lo = $derived(Math.log(result.assetValues[nodeIndex(T, 0)]));
  const hi = $derived(Math.log(result.assetValues[nodeIndex(T, T)]));

  const x = (t: number) =>
    PAD.left + (T > 0 ? t / T : 0) * (W - PAD.left - PAD.right);
  const y = (s: number) =>
    PAD.top +
    (hi > lo ? 1 - (Math.log(s) - lo) / (hi - lo) : 0.5) *
      (H - PAD.top - PAD.bottom);

  const radius = $derived(Math.max(1.5, Math.min(6, 150 / (T + 1))));
  const maxOption = $derived(Math.max(...result.optionValues));

  type Node = {
    t: number;
    i: number;
    cx: number;
    cy: number;
    value: number;
    exercised: boolean;
  };

  const nodes = $derived.by(() => {
    const out: Node[] = [];
    for (let t = 0; t <= T; ++t) {
      for (let i = 0; i <= t; ++i) {
        const n = nodeIndex(t, i);
        out.push({
          t,
          i,
          cx: x(t),
          cy: y(result.assetValues[n]),
          value: result.optionValues[n],
          exercised:
            isAmerican &&
            t < T &&
            result.exerciseValues[n] > result.continuationValues[n] + 1e-12,
        });
      }
    }
    return out;
  });

  // All edges as one path: node (t, i) branches to (t+1, i) and (t+1, i+1).
  const edges = $derived.by(() => {
    let d = '';
    for (let t = 0; t < T; ++t) {
      for (let i = 0; i <= t; ++i) {
        const from = `M${x(t)},${y(result.assetValues[nodeIndex(t, i)])}`;
        for (const j of [i, i + 1]) {
          d += `${from}L${x(t + 1)},${y(result.assetValues[nodeIndex(t + 1, j)])}`;
        }
      }
    }
    return d;
  });

  const yTicks = $derived(
    niceTicks(Math.exp(lo), Math.exp(hi), 5).filter(
      (v) => v >= Math.exp(lo) && v <= Math.exp(hi),
    ),
  );
  const xTicks = $derived(niceTicks(0, result.times[T], 4));

  function fillFor(node: Node): string {
    if (mode === 'asset') return 'var(--series-1)';
    if (node.t < revealedFrom) return 'var(--surface)';
    if (!(maxOption > 0) || node.value <= 0) return 'var(--seq-0)';
    return `var(--seq-${1 + Math.min(8, Math.floor((node.value / maxOption) * 9))})`;
  }

  function onpointermove(e: PointerEvent) {
    const ctm = svg.getScreenCTM();
    if (!ctm) return;
    const p = new DOMPoint(e.clientX, e.clientY).matrixTransform(
      ctm.inverse(),
    );
    const t = Math.round(
      ((p.x - PAD.left) / (W - PAD.left - PAD.right)) * T,
    );
    if (t < 0 || t > T) {
      hovered = null;
      return;
    }
    let best = 0;
    for (let i = 1; i <= t; ++i) {
      if (
        Math.abs(y(result.assetValues[nodeIndex(t, i)]) - p.y) <
        Math.abs(y(result.assetValues[nodeIndex(t, best)]) - p.y)
      ) {
        best = i;
      }
    }
    hovered = {t, i: best};
  }

  const hoverEdges = $derived.by(() => {
    if (!hovered || hovered.t >= T) return '';
    const {t, i} = hovered;
    const from = `M${x(t)},${y(result.assetValues[nodeIndex(t, i)])}`;
    return [i, i + 1]
      .map(
        (j) =>
          `${from}L${x(t + 1)},${y(result.assetValues[nodeIndex(t + 1, j)])}`,
      )
      .join('');
  });
</script>

<svg
  bind:this={svg}
  viewBox="0 0 {W} {H}"
  role="img"
  aria-label="Binomial tree with {T} steps"
  {onpointermove}
  onpointerleave={() => (hovered = null)}
>
  <!-- Axes -->
  {#each yTicks as v (v)}
    <line class="grid" x1={PAD.left} x2={W - PAD.right} y1={y(v)} y2={y(v)} />
    <text class="tick" x={PAD.left - 8} y={y(v)} text-anchor="end" dy="0.32em"
      >{fmt(v, v < 10 ? 1 : 0)}</text
    >
  {/each}
  {#each xTicks as v (v)}
    <text
      class="tick"
      x={PAD.left + (v / result.times[T]) * (W - PAD.left - PAD.right)}
      y={H - PAD.bottom + 20}
      text-anchor="middle">{v}y</text
    >
  {/each}

  <path class="edges" d={edges} />

  {#if mode === 'option' && strike !== undefined && Math.log(strike) >= lo && Math.log(strike) <= hi}
    <line
      class="strike"
      x1={PAD.left}
      x2={W - PAD.right}
      y1={y(strike)}
      y2={y(strike)}
    />
    <text class="tick" x={W - PAD.right + 10} y={y(strike)} dy="0.32em"
      >K = {fmt(strike, 0)}</text
    >
  {/if}

  <path class="hover-edges" d={hoverEdges} />

  {#each nodes as node (`${node.t},${node.i}`)}
    {#if mode === 'option' && node.exercised && node.t >= revealedFrom}
      <rect
        class="node exercised"
        x={node.cx - radius}
        y={node.cy - radius}
        width={radius * 2}
        height={radius * 2}
        transform="rotate(45 {node.cx} {node.cy})"
        style:fill={fillFor(node)}
      />
    {:else}
      <circle
        class="node"
        class:pending={mode === 'option' && node.t < revealedFrom}
        cx={node.cx}
        cy={node.cy}
        r={radius}
        style:fill={fillFor(node)}
      />
    {/if}
  {/each}

  {#if hovered}
    {@const n = nodeIndex(hovered.t, hovered.i)}
    <circle
      class="hover-ring"
      cx={x(hovered.t)}
      cy={y(result.assetValues[n])}
      r={radius + 4}
    />
  {/if}
</svg>

<style>
  svg {
    display: block;
    width: 100%;
    height: auto;
    font-family: var(--font-ui);
    touch-action: none;
  }
  .grid {
    stroke: var(--gridline);
    stroke-width: 1;
  }
  .tick {
    fill: var(--text-muted);
    font-size: 12px;
    font-variant-numeric: tabular-nums;
  }
  .edges {
    stroke: var(--gridline);
    stroke-width: 1;
    fill: none;
  }
  .hover-edges {
    stroke: var(--text-secondary);
    stroke-width: 2;
    fill: none;
  }
  .strike {
    stroke: var(--text-muted);
    stroke-width: 1;
    stroke-dasharray: 4 4;
  }
  .node {
    transition: fill 200ms ease;
  }
  .node.pending {
    stroke: var(--text-muted);
    stroke-width: 1;
  }
  .exercised {
    stroke: var(--series-2);
    stroke-width: 2;
  }
  .hover-ring {
    fill: none;
    stroke: var(--text-primary);
    stroke-width: 2;
  }
</style>
