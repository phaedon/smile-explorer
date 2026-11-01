<!--
  Bars for a handful of categories (such as months), with hover to read each.
  A bar can be set apart in the secondary series colour.
-->
<script lang="ts">
  import {niceTicks, tickFormat} from './format';

  let {
    bars,
    total,
    unit = 'paths',
    height = 200,
    ariaLabel,
  }: {
    bars: {label: string; value: number; secondary?: boolean}[];
    // The count the shares in the tooltip are out of.
    total: number;
    unit?: string;
    height?: number;
    ariaLabel: string;
  } = $props();

  const W = 760;
  const PAD = {left: 48, right: 12, top: 14, bottom: 30};
  const H = $derived(height);

  const maxValue = $derived(Math.max(1, ...bars.map((b) => b.value)));
  const slot = $derived((W - PAD.left - PAD.right) / Math.max(1, bars.length));
  const y = (v: number) => H - PAD.bottom - (v / maxValue) * (H - PAD.top - PAD.bottom);
  const yTicks = $derived(niceTicks(0, maxValue, 3));
  const yTickFmt = $derived(tickFormat(yTicks));

  let hovered = $state<number | null>(null);
  let svg: SVGSVGElement;

  function onpointermove(e: PointerEvent) {
    const ctm = svg.getScreenCTM();
    if (!ctm) return;
    const p = new DOMPoint(e.clientX, e.clientY).matrixTransform(ctm.inverse());
    const b = Math.floor((p.x - PAD.left) / slot);
    hovered = b >= 0 && b < bars.length ? b : null;
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
    {#each bars as bar, b (bar.label)}
      {#if bar.value > 0}
        <rect
          class="bar"
          class:secondary={bar.secondary}
          class:active={hovered === b}
          x={PAD.left + b * slot + 2}
          y={y(bar.value)}
          width={Math.max(1, slot - 4)}
          height={H - PAD.bottom - y(bar.value)}
          rx="3"
        />
      {/if}
      <text class="tick" x={PAD.left + (b + 0.5) * slot} y={H - PAD.bottom + 18} text-anchor="middle"
        >{bar.label}</text
      >
    {/each}
  </svg>
  {#if hovered !== null}
    {@const bar = bars[hovered]}
    <div class="tooltip" style:left="{Math.min(Math.max(((PAD.left + (hovered + 0.5) * slot) / W) * 100, 15), 85)}%">
      <div class="tip-x">{bar.label}</div>
      <strong>{bar.value.toLocaleString('en-US')}</strong> {unit}
      ({total > 0 ? ((bar.value / total) * 100).toFixed(1) : '0'}%)
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
  .bar.secondary {
    fill: var(--series-2);
  }
  .bar.active {
    opacity: 0.75;
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
