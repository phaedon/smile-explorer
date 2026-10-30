<!--
  European tree price as a function of the number of steps, against the
  Black-Scholes price that it converges to.
-->
<script lang="ts">
  import {fmt, niceTicks} from './format';
  import {tryPrice} from './engine';
  import {requestFor, type Params} from './params.svelte';

  let {p, maxSteps = 100}: {p: Params; maxSteps?: number} = $props();

  const W = 760;
  const H = 240;
  const PAD = {left: 52, right: 20, top: 14, bottom: 34};

  const series = $derived.by(() => {
    const out: {n: number; price: number}[] = [];
    let bsm = NaN;
    for (let n = 1; n <= maxSteps; ++n) {
      const request = requestFor(p, n / p.expiryYears);
      // Black-Scholes only prices European options.
      request.option.isAmerican = false;
      const outcome = tryPrice(request);
      if (outcome.result) {
        out.push({n, price: outcome.result.price});
        bsm = outcome.result.bsmPrice;
      }
    }
    return {points: out, bsm};
  });

  const current = $derived(Math.round(p.stepsPerYear * p.expiryYears));
  const label = $derived(p.isCall ? 'call' : 'put');

  // The first few (very coarse) trees can be far off, so leave them out of
  // the y range (they are clipped) to keep the oscillation visible.
  const yRange = $derived.by(() => {
    const prices = series.points
      .filter((d) => d.n >= 4)
      .map((d) => d.price)
      .concat(series.bsm);
    const lo = Math.min(...prices);
    const hi = Math.max(...prices);
    const pad = (hi - lo) * 0.08 || 0.5;
    return [lo - pad, hi + pad];
  });

  const x = (n: number) =>
    PAD.left + ((n - 1) / (maxSteps - 1)) * (W - PAD.left - PAD.right);
  const y = (v: number) =>
    H -
    PAD.bottom -
    ((v - yRange[0]) / (yRange[1] - yRange[0])) * (H - PAD.top - PAD.bottom);

  const line = $derived(
    series.points
      .map((d, k) => `${k === 0 ? 'M' : 'L'}${x(d.n)},${y(d.price)}`)
      .join(''),
  );
  const yTicks = $derived(niceTicks(yRange[0], yRange[1], 4));
  const xTicks = $derived(niceTicks(1, maxSteps, 5).filter((n) => n >= 1));

  let svg: SVGSVGElement;
  let hovered = $state<{n: number; price: number} | null>(null);
  const currentPoint = $derived(
    series.points.find((d) => d.n === current) ?? null,
  );

  function onpointermove(e: PointerEvent) {
    const ctm = svg.getScreenCTM();
    if (!ctm) return;
    const px = new DOMPoint(e.clientX, e.clientY).matrixTransform(
      ctm.inverse(),
    ).x;
    const n = Math.round(
      1 + ((px - PAD.left) / (W - PAD.left - PAD.right)) * (maxSteps - 1),
    );
    hovered = series.points.find((d) => d.n === n) ?? null;
  }
</script>

<div class="wrap">
  <svg
    bind:this={svg}
    viewBox="0 0 {W} {H}"
    role="img"
    aria-label="Tree price against number of steps, converging to Black-Scholes"
    {onpointermove}
    onpointerleave={() => (hovered = null)}
  >
    {#each yTicks as v (v)}
      <line class="grid" x1={PAD.left} x2={W - PAD.right} y1={y(v)} y2={y(v)} />
      <text class="tick" x={PAD.left - 8} y={y(v)} text-anchor="end" dy="0.32em"
        >{fmt(v)}</text
      >
    {/each}
    {#each xTicks as v (v)}
      <text class="tick" x={x(v)} y={H - PAD.bottom + 20} text-anchor="middle">{v}</text>
    {/each}
    <text class="tick" x={W - PAD.right} y={H - 2} text-anchor="end">steps to expiry</text>

    {#if Number.isFinite(series.bsm)}
      <line class="bsm" x1={PAD.left} x2={W - PAD.right} y1={y(series.bsm)} y2={y(series.bsm)} />
      <text class="tick" x={W - PAD.right} y={y(series.bsm) - 6} text-anchor="end"
        >Black–Scholes {fmt(series.bsm, 4)}</text
      >
    {/if}

    <defs>
      <clipPath id="convergence-plot">
        <rect x={PAD.left} y={PAD.top} width={W - PAD.left - PAD.right} height={H - PAD.top - PAD.bottom} />
      </clipPath>
    </defs>
    <path class="line" d={line} clip-path="url(#convergence-plot)" />

    {#if hovered}
      <line class="crosshair" x1={x(hovered.n)} x2={x(hovered.n)} y1={PAD.top} y2={H - PAD.bottom} />
    {/if}
    {#if currentPoint && y(currentPoint.price) >= PAD.top}
      <circle class="dot" cx={x(currentPoint.n)} cy={y(currentPoint.price)} r="4.5" />
    {/if}
    {#if hovered && y(hovered.price) >= PAD.top}
      <circle class="dot" cx={x(hovered.n)} cy={y(hovered.price)} r="4.5" />
    {/if}
  </svg>

  {#if hovered}
    <div
      class="tooltip"
      style:left="{(x(hovered.n) / W) * 100}%"
      style:top="{(Math.max(PAD.top, y(hovered.price)) / H) * 100}%"
    >
      {hovered.n} steps: <strong>{fmt(hovered.price, 4)}</strong>
      <span class="muted">({hovered.price >= series.bsm ? '+' : ''}{fmt(hovered.price - series.bsm, 4)})</span>
    </div>
  {/if}
</div>

<p class="caption">
  {#if currentPoint}
    European {label} prices. The dot is this page's tree ({currentPoint.n} steps):
    {fmt(currentPoint.price, 4)}, off by
    {fmt(Math.abs(currentPoint.price - series.bsm), 4)}. Hover to compare other step counts.
  {/if}
</p>

<style>
  .wrap {
    position: relative;
  }
  svg {
    display: block;
    width: 100%;
    height: auto;
    font-family: var(--font-ui);
  }
  .grid {
    stroke: var(--gridline);
  }
  .tick {
    fill: var(--text-muted);
    font-size: 12px;
    font-variant-numeric: tabular-nums;
  }
  .bsm {
    stroke: var(--text-muted);
    stroke-dasharray: 4 4;
  }
  .line {
    fill: none;
    stroke: var(--series-1);
    stroke-width: 2;
    stroke-linejoin: round;
  }
  .crosshair {
    stroke: var(--text-muted);
    stroke-width: 1;
  }
  .dot {
    fill: var(--series-1);
    stroke: var(--surface);
    stroke-width: 2;
  }
  .tooltip {
    position: absolute;
    transform: translate(-50%, calc(-100% - 12px));
    pointer-events: none;
    background: var(--surface-raised);
    border: 1px solid var(--border);
    border-radius: 6px;
    padding: 4px 10px;
    font: 12.5px var(--font-ui);
    font-variant-numeric: tabular-nums;
    white-space: nowrap;
    box-shadow: 0 2px 8px rgba(0, 0, 0, 0.12);
  }
  .muted {
    color: var(--text-secondary);
  }
  .caption {
    font: 14px/1.5 var(--font-ui);
    color: var(--text-secondary);
    font-variant-numeric: tabular-nums;
  }
</style>
