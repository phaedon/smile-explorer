<!--
  The Arrow-Debreu prices at expiry: the value today of $1 paid only in that
  state. Together they are the discounted risk-neutral distribution of the
  asset price, and the option price is their payoff-weighted sum.
-->
<script lang="ts">
  import {fmt, niceTicks} from './format';
  import {nodeIndex, type VanillaPricingResult} from './engine';

  let {
    result,
    strike,
    isAmerican = false,
  }: {result: VanillaPricingResult; strike: number; isAmerican?: boolean} =
    $props();

  const W = 760;
  const H = 240;
  // The top padding leaves room for the strike label.
  const PAD = {left: 52, right: 20, top: 28, bottom: 34};

  const T = $derived(result.expiryIndex);
  const states = $derived(
    Array.from({length: T + 1}, (_, i) => {
      const n = nodeIndex(T, i);
      return {
        i,
        s: result.assetValues[n],
        ad: result.arrowDebreu[n],
        payoff: result.exerciseValues[n],
      };
    }),
  );
  // Pad the price range so that the outermost bars aren't clipped.
  const sLo = $derived.by(() => {
    const lo = Math.min(states[0].s, strike);
    return lo - (Math.max(states[T].s, strike) - lo) * 0.04;
  });
  const sHi = $derived.by(() => {
    const hi = Math.max(states[T].s, strike);
    return hi + (hi - Math.min(states[0].s, strike)) * 0.04;
  });
  const adMax = $derived(Math.max(...states.map((d) => d.ad)));

  const x = (s: number) =>
    PAD.left +
    ((s - sLo) / (sHi - sLo || 1)) * (W - PAD.left - PAD.right);
  const y = (v: number) =>
    H - PAD.bottom - (v / (adMax || 1)) * (H - PAD.top - PAD.bottom);

  // Bars are as wide as the tightest spacing between neighbouring states
  // allows, leaving a 2px gap.
  const barWidth = $derived.by(() => {
    let gap = Infinity;
    for (let i = 1; i <= T; ++i) {
      gap = Math.min(gap, x(states[i].s) - x(states[i - 1].s));
    }
    return Math.max(1.5, Math.min(28, (Number.isFinite(gap) ? gap : 28) - 2));
  });

  function barPath(cx: number, top: number): string {
    const base = H - PAD.bottom;
    const half = barWidth / 2;
    const r = Math.min(4, half, Math.max(0, base - top));
    return (
      `M${cx - half},${base}V${top + r}Q${cx - half},${top} ${cx - half + r},${top}` +
      `H${cx + half - r}Q${cx + half},${top} ${cx + half},${top + r}V${base}Z`
    );
  }

  const yTicks = $derived(niceTicks(0, adMax, 3));
  const xTicks = $derived(niceTicks(sLo, sHi, 6));
  const price = $derived(states.reduce((sum, d) => sum + d.ad * d.payoff, 0));

  let hovered = $state<number | null>(null);
  const tip = $derived(hovered === null ? null : states[hovered]);
</script>

<div class="legend" aria-hidden="true">
  <span><i style:background="var(--series-2)"></i>pays off at expiry</span>
  <span><i style:background="var(--series-1)"></i>expires worthless</span>
</div>

<div class="wrap">
  <svg
    viewBox="0 0 {W} {H}"
    role="img"
    aria-label="Arrow-Debreu prices of the {T + 1} states at expiry"
    onpointerleave={() => (hovered = null)}
  >
    {#each yTicks as v (v)}
      <line class="grid" x1={PAD.left} x2={W - PAD.right} y1={y(v)} y2={y(v)} />
      <text class="tick" x={PAD.left - 8} y={y(v)} text-anchor="end" dy="0.32em"
        >{fmt(v, 3)}</text
      >
    {/each}
    {#each xTicks as v (v)}
      <text class="tick" x={x(v)} y={H - PAD.bottom + 20} text-anchor="middle"
        >{fmt(v, 0)}</text
      >
    {/each}

    {#each states as d (d.i)}
      <path
        d={barPath(x(d.s), y(d.ad))}
        style:fill={d.payoff > 0 ? 'var(--series-2)' : 'var(--series-1)'}
        opacity={hovered === null || hovered === d.i ? 1 : 0.55}
      />
      <!-- A hit target wider than the bar. -->
      <rect
        class="hit"
        x={x(d.s) - Math.max(barWidth, 10) / 2}
        y={PAD.top}
        width={Math.max(barWidth, 10)}
        height={H - PAD.top - PAD.bottom}
        role="presentation"
        onpointerenter={() => (hovered = d.i)}
      />
    {/each}

    <line class="strike" x1={x(strike)} x2={x(strike)} y1={PAD.top} y2={H - PAD.bottom} />
    <text class="tick" x={x(strike)} y={PAD.top - 10} text-anchor="middle"
      >K = {fmt(strike, 0)}</text
    >
  </svg>

  {#if tip}
    <div
      class="tooltip"
      style:left="{(x(tip.s) / W) * 100}%"
      style:top="{(y(tip.ad) / H) * 100}%"
    >
      <div>S = <strong>{fmt(tip.s)}</strong></div>
      <div>AD price {fmt(tip.ad, 4)}</div>
      <div>payoff {fmt(tip.payoff)}</div>
      <div>contribution {fmt(tip.ad * tip.payoff, 4)}</div>
    </div>
  {/if}
</div>

<p class="sum">
  Σ AD price × payoff = <strong>{fmt(price, 4)}</strong>
  {#if isAmerican}
    is the price of the <em>European</em> option. The American option is worth
    {fmt(result.price, 4)}: the difference of {fmt(result.price - price, 4)} is
    the value of being able to exercise early, which no weighted sum of payoffs
    at expiry can capture.
  {:else}
    is exactly the tree price {fmt(result.price, 4)}.
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
  .strike {
    stroke: var(--text-muted);
    stroke-dasharray: 4 4;
  }
  .hit {
    fill: transparent;
  }
  .legend {
    display: flex;
    gap: 16px;
    font: 13px var(--font-ui);
    color: var(--text-secondary);
    margin-bottom: 4px;
  }
  .legend i {
    display: inline-block;
    width: 10px;
    height: 10px;
    border-radius: 2px;
    margin-right: 6px;
    vertical-align: -1px;
  }
  .tooltip {
    position: absolute;
    transform: translate(-50%, calc(-100% - 10px));
    pointer-events: none;
    background: var(--surface-raised);
    border: 1px solid var(--border);
    border-radius: 6px;
    padding: 6px 10px;
    font: 12.5px/1.45 var(--font-ui);
    color: var(--text-primary);
    font-variant-numeric: tabular-nums;
    white-space: nowrap;
    box-shadow: 0 2px 8px rgba(0, 0, 0, 0.12);
  }
  .sum {
    font: 14px var(--font-ui);
    color: var(--text-secondary);
    font-variant-numeric: tabular-nums;
  }
</style>
