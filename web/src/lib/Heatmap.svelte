<!--
  A heatmap of z = f(x, y) on a grid, coloured on the sequential ramp, with
  hover to read exact values. The 2D twin of Surface3D: the place for numbers.
-->
<script lang="ts">
  import {niceTicks, tickFormat} from './format';

  let {
    xs,
    ys,
    values,
    xLabel,
    yLabel,
    xFormat = (v: number) => String(v),
    yFormat = (v: number) => String(v),
    zFormat = (v: number) => String(v),
    markers = [],
    height = 260,
    ariaLabel,
  }: {
    xs: number[];
    ys: number[];
    // Row by row: the value at ys[i], xs[j] is values[i * xs.length + j].
    values: ArrayLike<number>;
    xLabel: string;
    yLabel: string;
    xFormat?: (v: number) => string;
    yFormat?: (v: number) => string;
    zFormat?: (v: number) => string;
    markers?: {x: number; label: string}[];
    height?: number;
    ariaLabel: string;
  } = $props();

  const W = 760;
  const PAD = {left: 56, right: 20, top: 10, bottom: 38};
  const H = $derived(height);

  const range = $derived.by(() => {
    let lo = Infinity;
    let hi = -Infinity;
    for (let k = 0; k < values.length; ++k) {
      if (Number.isFinite(values[k])) {
        lo = Math.min(lo, values[k]);
        hi = Math.max(hi, values[k]);
      }
    }
    return [lo, hi];
  });

  // Cells are centred on the grid points.
  const cellW = $derived((W - PAD.left - PAD.right) / xs.length);
  const cellH = $derived((H - PAD.top - PAD.bottom) / ys.length);
  const x = (v: number) =>
    PAD.left + ((v - xs[0]) / (xs[xs.length - 1] - xs[0] || 1)) * (W - PAD.left - PAD.right - cellW) + cellW / 2;
  // The first y is at the bottom.
  const y = (v: number) =>
    H - PAD.bottom - ((v - ys[0]) / (ys[ys.length - 1] - ys[0] || 1)) * (H - PAD.top - PAD.bottom - cellH) - cellH / 2;

  function fill(v: number): string {
    if (!Number.isFinite(v) || !(range[1] > range[0])) return 'var(--seq-0)';
    const level = (v - range[0]) / (range[1] - range[0]);
    return `var(--seq-${Math.min(9, Math.floor(level * 10))})`;
  }

  const xTicks = $derived(niceTicks(xs[0], xs[xs.length - 1], 6).filter((v) => v >= xs[0] && v <= xs[xs.length - 1]));
  const yTicks = $derived(niceTicks(ys[0], ys[ys.length - 1], 4).filter((v) => v >= ys[0] && v <= ys[ys.length - 1]));
  const legendTicks = $derived(niceTicks(range[0], range[1], 4).filter((v) => v >= range[0] && v <= range[1]));
  const legendFmt = $derived(tickFormat(legendTicks));

  let hover = $state<{i: number; j: number} | null>(null);
  let svg: SVGSVGElement;

  function onpointermove(e: PointerEvent) {
    const ctm = svg.getScreenCTM();
    if (!ctm) return;
    const p = new DOMPoint(e.clientX, e.clientY).matrixTransform(ctm.inverse());
    const j = Math.floor((p.x - PAD.left) / cellW);
    const i = Math.floor((H - PAD.bottom - p.y) / cellH);
    hover = i >= 0 && i < ys.length && j >= 0 && j < xs.length ? {i, j} : null;
  }
</script>

<div class="wrap">
  <svg
    bind:this={svg}
    viewBox="0 0 {W} {H}"
    role="img"
    aria-label={ariaLabel}
    {onpointermove}
    onpointerleave={() => (hover = null)}
  >
    {#each ys as yv, i (i)}
      {#each xs as xv, j (j)}
        <rect
          x={x(xv) - cellW / 2}
          y={y(yv) - cellH / 2}
          width={cellW + 0.5}
          height={cellH + 0.5}
          style:fill={fill(values[i * xs.length + j])}
        />
      {/each}
    {/each}
    {#each markers as m (m.label)}
      <line class="marker" x1={x(m.x)} x2={x(m.x)} y1={PAD.top} y2={H - PAD.bottom} />
    {/each}
    {#if hover}
      <rect
        class="hovered"
        x={x(xs[hover.j]) - cellW / 2}
        y={y(ys[hover.i]) - cellH / 2}
        width={cellW}
        height={cellH}
      />
    {/if}
    {#each xTicks as v (v)}
      <text class="tick" x={x(v)} y={H - PAD.bottom + 18} text-anchor="middle">{xFormat(v)}</text>
    {/each}
    {#each yTicks as v (v)}
      <text class="tick" x={PAD.left - 8} y={y(v)} text-anchor="end" dy="0.32em">{yFormat(v)}</text>
    {/each}
    <text class="tick" x={W - PAD.right} y={H - 2} text-anchor="end">{xLabel}</text>
    <text class="tick" x={4} y={PAD.top + 4} dy="0.6em">{yLabel}</text>
  </svg>
  {#if hover}
    {@const v = values[hover.i * xs.length + hover.j]}
    <div class="tooltip" style:left="{(x(xs[hover.j]) / W) * 100}%">
      <div>{xLabel} {xFormat(xs[hover.j])} · {yLabel} {yFormat(ys[hover.i])}</div>
      <strong>{zFormat(v)}</strong>
    </div>
  {/if}
  <div class="legend" aria-hidden="true">
    <span>{legendFmt(legendTicks[0] ?? range[0])}</span>
    <i class="gradient"></i>
    <span>{legendFmt(legendTicks[legendTicks.length - 1] ?? range[1])}</span>
  </div>
</div>

<style>
  .wrap {
    position: relative;
  }
  svg {
    display: block;
    width: 100%;
    height: auto;
    font-family: var(--font-ui);
    touch-action: pan-y;
  }
  .tick {
    fill: var(--text-muted);
    font-size: 12px;
    font-variant-numeric: tabular-nums;
  }
  .marker {
    stroke: var(--surface);
    stroke-dasharray: 4 4;
  }
  .hovered {
    fill: none;
    stroke: var(--text-primary);
    stroke-width: 2;
  }
  .tooltip {
    position: absolute;
    top: 8px;
    transform: translateX(-50%);
    pointer-events: none;
    background: var(--surface-raised);
    border: 1px solid var(--border);
    border-radius: 6px;
    padding: 6px 10px;
    font: 12.5px/1.5 var(--font-ui);
    font-variant-numeric: tabular-nums;
    white-space: nowrap;
    box-shadow: 0 2px 8px rgba(0, 0, 0, 0.12);
  }
  .legend {
    display: flex;
    align-items: center;
    gap: 8px;
    font: 12px var(--font-ui);
    color: var(--text-secondary);
    font-variant-numeric: tabular-nums;
    margin-top: 2px;
  }
  .gradient {
    display: inline-block;
    width: 120px;
    height: 10px;
    border-radius: 2px;
    background: linear-gradient(to right, var(--seq-0), var(--seq-3), var(--seq-6), var(--seq-9));
  }
</style>
