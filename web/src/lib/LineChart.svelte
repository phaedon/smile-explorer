<!--
  A small line chart for one or more series sharing an x and y axis, with a
  legend (for two or more series), optional vertical markers, and a hover
  crosshair whose tooltip reads every series at the nearest point.
-->
<script lang="ts">
  import {niceTicks} from './format';

  export type Series = {
    name: string;
    // A CSS colour, normally a var(--series-n) token.
    color: string;
    points: {x: number; y: number}[];
    // Draw markers at the points instead of a line through them.
    dotsOnly?: boolean;
    dashed?: boolean;
    // Series with the same group share one legend entry, named after it (the
    // two edges of a band, say).
    group?: string;
  };

  let {
    series,
    xLabel = '',
    xFormat = (v: number) => String(v),
    yFormat = (v: number) => String(v),
    markers = [],
    height = 240,
    ariaLabel,
  }: {
    series: Series[];
    xLabel?: string;
    xFormat?: (v: number) => string;
    yFormat?: (v: number) => string;
    markers?: {x: number; label: string}[];
    height?: number;
    ariaLabel: string;
  } = $props();

  const W = 760;
  const PAD = {left: 56, right: 20, top: 14, bottom: 38};
  const H = $derived(height);

  const all = $derived(series.flatMap((s) => s.points));
  const legend = $derived(
    series.filter((s, k) => s.group === undefined || series.findIndex((o) => o.group === s.group) === k),
  );
  const xDomain = $derived.by(() => {
    const xs = all.map((p) => p.x);
    return [Math.min(...xs), Math.max(...xs)];
  });
  const yDomain = $derived.by(() => {
    const ys = all.map((p) => p.y).filter(Number.isFinite);
    const lo = Math.min(...ys);
    const hi = Math.max(...ys);
    const pad = (hi - lo) * 0.12 || Math.abs(hi) * 0.1 || 0.01;
    return [lo - pad, hi + pad];
  });

  const x = (v: number) =>
    PAD.left +
    ((v - xDomain[0]) / (xDomain[1] - xDomain[0] || 1)) *
      (W - PAD.left - PAD.right);
  const y = (v: number) =>
    H -
    PAD.bottom -
    ((v - yDomain[0]) / (yDomain[1] - yDomain[0] || 1)) *
      (H - PAD.top - PAD.bottom);

  function path(points: {x: number; y: number}[]): string {
    return points
      .filter((p) => Number.isFinite(p.y))
      .map((p, k) => `${k === 0 ? 'M' : 'L'}${x(p.x)},${y(p.y)}`)
      .join('');
  }

  const xTicks = $derived(
    niceTicks(xDomain[0], xDomain[1], 6).filter(
      (v) => v >= xDomain[0] && v <= xDomain[1],
    ),
  );
  const yTicks = $derived(
    niceTicks(yDomain[0], yDomain[1], 4).filter(
      (v) => v >= yDomain[0] && v <= yDomain[1],
    ),
  );

  let svg: SVGSVGElement;
  let hoverX = $state<number | null>(null);

  function nearest(points: {x: number; y: number}[], target: number) {
    let best = points[0];
    for (const p of points) {
      if (Math.abs(p.x - target) < Math.abs(best.x - target)) best = p;
    }
    return best;
  }

  const hover = $derived.by(() => {
    if (hoverX === null || series.length === 0) return null;
    const anchor = nearest(series[0].points, hoverX);
    return {
      x: anchor.x,
      rows: series.map((s) => ({series: s, point: nearest(s.points, anchor.x)})),
    };
  });

  function onpointermove(e: PointerEvent) {
    const ctm = svg.getScreenCTM();
    if (!ctm) return;
    const px = new DOMPoint(e.clientX, e.clientY).matrixTransform(
      ctm.inverse(),
    ).x;
    hoverX =
      xDomain[0] +
      ((px - PAD.left) / (W - PAD.left - PAD.right)) *
        (xDomain[1] - xDomain[0]);
  }
</script>

{#if legend.length > 1}
  <div class="legend" aria-hidden="true">
    {#each legend as s (s.group ?? s.name)}
      <span
        ><i class:dot={s.dotsOnly} class:dash={s.dashed} style:--swatch={s.color}></i>{s.group ?? s.name}</span
      >
    {/each}
  </div>
{/if}

<div class="wrap">
  <svg
    bind:this={svg}
    viewBox="0 0 {W} {H}"
    role="img"
    aria-label={ariaLabel}
    {onpointermove}
    onpointerleave={() => (hoverX = null)}
  >
    {#each yTicks as v (v)}
      <line class="grid" x1={PAD.left} x2={W - PAD.right} y1={y(v)} y2={y(v)} />
      <text class="tick" x={PAD.left - 8} y={y(v)} text-anchor="end" dy="0.32em"
        >{yFormat(v)}</text
      >
    {/each}
    {#each xTicks as v (v)}
      <text class="tick" x={x(v)} y={H - PAD.bottom + 18} text-anchor="middle"
        >{xFormat(v)}</text
      >
    {/each}
    {#if xLabel}
      <text class="tick" x={W - PAD.right} y={H - 2} text-anchor="end"
        >{xLabel}</text
      >
    {/if}

    {#each markers as m (m.label)}
      <line
        class="marker"
        x1={x(m.x)}
        x2={x(m.x)}
        y1={PAD.top}
        y2={H - PAD.bottom}
      />
      <text class="tick" x={x(m.x) + 5} y={PAD.top + 10}>{m.label}</text>
    {/each}

    {#each series as s (s.name)}
      {#if s.dotsOnly}
        {#each s.points as p (p.x)}
          <circle class="dot" cx={x(p.x)} cy={y(p.y)} r="5" style:fill={s.color} />
        {/each}
      {:else}
        <path class="line" class:dashed={s.dashed} d={path(s.points)} style:stroke={s.color} />
      {/if}
    {/each}

    {#if hover}
      <line
        class="crosshair"
        x1={x(hover.x)}
        x2={x(hover.x)}
        y1={PAD.top}
        y2={H - PAD.bottom}
      />
      {#each hover.rows as row (row.series.name)}
        {#if Number.isFinite(row.point.y)}
          <circle
            class="dot"
            cx={x(row.point.x)}
            cy={y(row.point.y)}
            r="4"
            style:fill={row.series.color}
          />
        {/if}
      {/each}
    {/if}
  </svg>

  {#if hover}
    <div class="tooltip" style:left="{(x(hover.x) / W) * 100}%">
      <div class="tip-x">{xFormat(hover.x)}</div>
      {#each hover.rows as row (row.series.name)}
        <div>
          <i style:background={row.series.color}></i>{row.series.name}
          <strong>{yFormat(row.point.y)}</strong>
        </div>
      {/each}
    </div>
  {/if}
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
  .grid {
    stroke: var(--gridline);
  }
  .tick {
    fill: var(--text-muted);
    font-size: 12px;
    font-variant-numeric: tabular-nums;
  }
  .marker {
    stroke: var(--text-muted);
    stroke-dasharray: 4 4;
  }
  .line {
    fill: none;
    stroke-width: 2;
    stroke-linejoin: round;
  }
  .dot {
    stroke: var(--surface);
    stroke-width: 2;
  }
  .crosshair {
    stroke: var(--text-muted);
  }
  .legend {
    display: flex;
    flex-wrap: wrap;
    gap: 16px;
    font: 13px var(--font-ui);
    color: var(--text-secondary);
    margin-bottom: 4px;
  }
  .legend i,
  .tooltip i {
    display: inline-block;
    width: 12px;
    height: 3px;
    border-radius: 2px;
    margin-right: 6px;
    vertical-align: 3px;
  }
  .legend i {
    background: var(--swatch);
  }
  .legend i.dash {
    background: repeating-linear-gradient(90deg, var(--swatch) 0 4px, transparent 4px 7px);
  }
  .line.dashed {
    stroke-dasharray: 6 4;
  }
  .legend i.dot {
    width: 8px;
    height: 8px;
    border-radius: 50%;
    vertical-align: 0;
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
  .tip-x {
    color: var(--text-secondary);
  }
</style>
