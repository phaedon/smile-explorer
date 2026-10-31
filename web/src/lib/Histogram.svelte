<!--
  A histogram of samples, with optional vertical markers (a mean, an
  expected value) and hover to read each bin.
-->
<script lang="ts">
  import {niceTicks, tickFormat} from './format';

  let {
    samples,
    bins = 40,
    domain = null,
    xLabel = '',
    xFormat = (v: number) => String(v),
    markers = [],
    height = 220,
    ariaLabel,
  }: {
    samples: ArrayLike<number>;
    bins?: number;
    // A fixed x range, so that histograms can be compared; otherwise the
    // samples' own range.
    domain?: [number, number] | null;
    xLabel?: string;
    xFormat?: (v: number) => string;
    markers?: {x: number; label: string}[];
    height?: number;
    ariaLabel: string;
  } = $props();

  const W = 760;
  const PAD = {left: 48, right: 20, top: 22, bottom: 38};
  const H = $derived(height);

  const range = $derived.by((): [number, number] => {
    if (domain) return domain;
    let lo = Infinity;
    let hi = -Infinity;
    for (let k = 0; k < samples.length; ++k) {
      lo = Math.min(lo, samples[k]);
      hi = Math.max(hi, samples[k]);
    }
    return lo < hi ? [lo, hi] : [lo - 1, lo + 1];
  });

  const counts = $derived.by(() => {
    const out = new Array<number>(bins).fill(0);
    const [lo, hi] = range;
    for (let k = 0; k < samples.length; ++k) {
      const b = Math.floor(((samples[k] - lo) / (hi - lo)) * bins);
      if (b >= 0 && b < bins) out[b]++;
      else if (samples[k] === hi) out[bins - 1]++;
    }
    return out;
  });
  const maxCount = $derived(Math.max(1, ...counts));

  const x = (v: number) => PAD.left + ((v - range[0]) / (range[1] - range[0])) * (W - PAD.left - PAD.right);
  const y = (c: number) => H - PAD.bottom - (c / maxCount) * (H - PAD.top - PAD.bottom);
  const binWidth = $derived((range[1] - range[0]) / bins);

  const xTicks = $derived(niceTicks(range[0], range[1], 6).filter((v) => v >= range[0] && v <= range[1]));
  const yTicks = $derived(niceTicks(0, maxCount, 3));
  const yTickFmt = $derived(tickFormat(yTicks));

  let hovered = $state<number | null>(null);
  let svg: SVGSVGElement;

  function onpointermove(e: PointerEvent) {
    const ctm = svg.getScreenCTM();
    if (!ctm) return;
    const p = new DOMPoint(e.clientX, e.clientY).matrixTransform(ctm.inverse());
    const b = Math.floor(((p.x - PAD.left) / (W - PAD.left - PAD.right)) * bins);
    hovered = b >= 0 && b < bins ? b : null;
  }
</script>

<div class="wrap">
  <svg
    bind:this={svg}
    viewBox="0 0 {W} {H}"
    role="img"
    aria-label={ariaLabel}
    {onpointermove}
    onpointerleave={() => (hovered = null)}
  >
    {#each yTicks as v (v)}
      <line class="grid" x1={PAD.left} x2={W - PAD.right} y1={y(v)} y2={y(v)} />
      <text class="tick" x={PAD.left - 8} y={y(v)} text-anchor="end" dy="0.32em">{yTickFmt(v)}</text>
    {/each}
    {#each counts as c, b (b)}
      {@const x0 = x(range[0] + b * binWidth)}
      {@const w = x(range[0] + (b + 1) * binWidth) - x0}
      {#if c > 0}
        <rect
          class="bar"
          class:active={hovered === b}
          x={x0 + 1}
          y={y(c)}
          width={Math.max(1, w - 2)}
          height={H - PAD.bottom - y(c)}
          rx="2"
        />
      {/if}
    {/each}
    {#each markers as m (m.label)}
      {#if m.x >= range[0] && m.x <= range[1]}
        <line class="marker" x1={x(m.x)} x2={x(m.x)} y1={PAD.top - 6} y2={H - PAD.bottom} />
        <text class="tick" x={x(m.x) + 5} y={PAD.top - 8}>{m.label}</text>
      {/if}
    {/each}
    {#each xTicks as v (v)}
      <text class="tick" x={x(v)} y={H - PAD.bottom + 18} text-anchor="middle">{xFormat(v)}</text>
    {/each}
    {#if xLabel}
      <text class="tick" x={W - PAD.right} y={H - 2} text-anchor="end">{xLabel}</text>
    {/if}
  </svg>
  {#if hovered !== null}
    <div class="tooltip" style:left="{(x(range[0] + (hovered + 0.5) * binWidth) / W) * 100}%">
      <div>
        {xFormat(range[0] + hovered * binWidth)} to {xFormat(range[0] + (hovered + 1) * binWidth)}
      </div>
      <strong>{counts[hovered]}</strong> of {samples.length} paths
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
  .bar {
    fill: var(--series-1);
  }
  .bar.active {
    fill: var(--seq-8);
  }
  .marker {
    stroke: var(--text-primary);
    stroke-dasharray: 4 4;
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
</style>
