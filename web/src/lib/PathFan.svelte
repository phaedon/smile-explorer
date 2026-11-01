<!--
  A fan of Monte Carlo paths on a canvas, drawn as they arrive. Each path is
  drawn once, faintly, into a layer that keeps every earlier path, so the fan
  darkens where paths crowd; only the newest block is drawn at full strength
  on top. Paths that reached the target end in a dot. Hover reads the spread
  of the paths at a time.

  The parent adds paths with append() and starts over with clear(), via
  bind:this.
-->
<script lang="ts">
  import {onDestroy, onMount, untrack} from 'svelte';
  import {niceTicks, tickFormat} from './format';

  let {
    xs,
    yDomain,
    xFormat = (v: number) => String(v),
    yFormat = (v: number) => String(v),
    // For the axis ticks; by default as many decimals as the ticks need.
    yTickFormat = null,
    xLabel = '',
    // Horizontal reference lines, or curves given at each of xs.
    references = [],
    height = 260,
    ariaLabel,
  }: {
    xs: ArrayLike<number>;
    yDomain: [number, number];
    xFormat?: (v: number) => string;
    yFormat?: (v: number) => string;
    yTickFormat?: ((v: number) => string) | null;
    xLabel?: string;
    references?: {label: string; values: number | ArrayLike<number>}[];
    height?: number;
    ariaLabel: string;
  } = $props();

  type Row = {values: Float64Array; knockedOut: boolean};

  // Paths drawn at full strength, from the newest block.
  const HIGHLIGHT = 25;
  const PAD = {left: 56, right: 14, top: 12, bottom: 30};

  let container: HTMLDivElement;
  let canvas: HTMLCanvasElement;
  const layer = typeof document !== 'undefined' ? document.createElement('canvas') : null;
  let width = $state(0);
  let rows: Row[] = [];
  let newest: Row[] = [];
  let count = $state(0);
  let knockedOutCount = $state(0);
  let hoverIndex = $state<number | null>(null);
  let colors = {ranToEnd: '#2a78d6', knockedOut: '#eb6834', grid: '#e1e0d9', muted: '#898781', text: '#52514e'};

  const xMin = $derived(xs.length ? xs[0] : 0);
  const xMax = $derived(xs.length ? xs[xs.length - 1] : 1);
  const px = (v: number) => PAD.left + ((v - xMin) / (xMax - xMin || 1)) * (width - PAD.left - PAD.right);
  const py = (v: number) =>
    height - PAD.bottom - ((v - yDomain[0]) / (yDomain[1] - yDomain[0] || 1)) * (height - PAD.top - PAD.bottom);

  const xTicks = $derived(niceTicks(xMin, xMax, width < 480 ? 4 : 6).filter((v) => v >= xMin && v <= xMax));
  const yTicks = $derived(niceTicks(yDomain[0], yDomain[1], 4).filter((v) => v >= yDomain[0] && v <= yDomain[1]));
  const yTickLabel = $derived(yTickFormat ?? tickFormat(yTicks));

  function readColors() {
    const style = getComputedStyle(container);
    const token = (name: string, fallback: string) => style.getPropertyValue(name).trim() || fallback;
    colors = {
      ranToEnd: token('--series-1', colors.ranToEnd),
      knockedOut: token('--series-2', colors.knockedOut),
      grid: token('--gridline', colors.grid),
      muted: token('--text-muted', colors.muted),
      text: token('--text-secondary', colors.text),
    };
  }

  function tracePath(ctx: CanvasRenderingContext2D, values: Float64Array): {x: number; y: number} | null {
    let last: {x: number; y: number} | null = null;
    ctx.beginPath();
    for (let k = 0; k < values.length; ++k) {
      const v = values[k];
      if (!Number.isFinite(v)) break;
      const x = px(xs[k]);
      const y = py(v);
      if (last === null) ctx.moveTo(x, y);
      else ctx.lineTo(x, y);
      last = {x, y};
    }
    return last;
  }

  function drawRow(ctx: CanvasRenderingContext2D, row: Row, faint: boolean) {
    const color = row.knockedOut ? colors.knockedOut : colors.ranToEnd;
    ctx.strokeStyle = color;
    ctx.globalAlpha = faint ? 0.1 : 0.9;
    ctx.lineWidth = faint ? 1 : 1.25;
    const end = tracePath(ctx, row.values);
    ctx.stroke();
    if (row.knockedOut && end) {
      ctx.globalAlpha = faint ? 0.25 : 1;
      ctx.fillStyle = color;
      ctx.beginPath();
      ctx.arc(end.x, end.y, faint ? 1.5 : 3, 0, 2 * Math.PI);
      ctx.fill();
    }
    ctx.globalAlpha = 1;
  }

  function context(c: HTMLCanvasElement): CanvasRenderingContext2D | null {
    const ctx = c.getContext('2d');
    if (!ctx) return null;
    const dpr = window.devicePixelRatio || 1;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    return ctx;
  }

  function clipToPlot(ctx: CanvasRenderingContext2D) {
    ctx.beginPath();
    ctx.rect(PAD.left, PAD.top, width - PAD.left - PAD.right, height - PAD.top - PAD.bottom);
    ctx.clip();
  }

  // Draws rows into the layer that keeps every earlier path.
  function intoLayer(added: Row[]) {
    if (!layer || added.length === 0) return;
    const ctx = context(layer);
    if (!ctx) return;
    ctx.save();
    clipToPlot(ctx);
    for (const row of added) drawRow(ctx, row, true);
    ctx.restore();
  }

  function render() {
    if (!canvas || !layer || width === 0) return;
    const ctx = context(canvas);
    if (!ctx) return;
    ctx.clearRect(0, 0, width, height);

    ctx.font = '12px system-ui, -apple-system, sans-serif';
    ctx.fillStyle = colors.muted;
    ctx.strokeStyle = colors.grid;
    ctx.lineWidth = 1;
    ctx.textBaseline = 'middle';
    ctx.textAlign = 'right';
    for (const v of yTicks) {
      ctx.beginPath();
      ctx.moveTo(PAD.left, py(v));
      ctx.lineTo(width - PAD.right, py(v));
      ctx.stroke();
      ctx.fillText(yTickLabel(v), PAD.left - 8, py(v));
    }
    ctx.textAlign = 'center';
    ctx.textBaseline = 'top';
    for (const v of xTicks) {
      // Kept inside the canvas at the edges.
      const label = xFormat(v);
      const half = ctx.measureText(label).width / 2;
      ctx.fillText(label, Math.min(px(v), width - half - 1), height - PAD.bottom + 8);
    }
    if (xLabel) {
      ctx.textAlign = 'right';
      ctx.textBaseline = 'bottom';
      ctx.fillText(xLabel, width - PAD.right, height);
    }

    const dpr = window.devicePixelRatio || 1;
    ctx.drawImage(layer, 0, 0, layer.width / dpr, layer.height / dpr);

    ctx.save();
    clipToPlot(ctx);
    for (const row of newest) drawRow(ctx, row, false);

    // Reference lines, dashed, labelled at the right.
    ctx.strokeStyle = colors.text;
    ctx.lineWidth = 1.5;
    ctx.setLineDash([5, 4]);
    for (const ref of references) {
      ctx.beginPath();
      if (typeof ref.values === 'number') {
        ctx.moveTo(PAD.left, py(ref.values));
        ctx.lineTo(width - PAD.right, py(ref.values));
      } else {
        for (let k = 0; k < ref.values.length && k < xs.length; ++k) {
          const point = [px(xs[k]), py(ref.values[k])] as const;
          if (k === 0) ctx.moveTo(...point);
          else ctx.lineTo(...point);
        }
      }
      ctx.stroke();
    }
    ctx.setLineDash([]);
    ctx.restore();

    ctx.fillStyle = colors.text;
    ctx.textAlign = 'right';
    ctx.textBaseline = 'bottom';
    for (const ref of references) {
      const last = typeof ref.values === 'number' ? ref.values : ref.values[ref.values.length - 1];
      ctx.fillText(ref.label, width - PAD.right - 4, py(last) - 4);
    }

    if (hoverIndex !== null) {
      ctx.strokeStyle = colors.muted;
      ctx.lineWidth = 1;
      ctx.beginPath();
      ctx.moveTo(px(xs[hoverIndex]), PAD.top);
      ctx.lineTo(px(xs[hoverIndex]), height - PAD.bottom);
      ctx.stroke();
    }
  }

  // Sizes both canvases to the container and redraws every stored path.
  function redrawAll() {
    if (!canvas || !layer) return;
    width = container.clientWidth;
    const dpr = window.devicePixelRatio || 1;
    for (const c of [canvas, layer]) {
      c.width = Math.round(width * dpr);
      c.height = Math.round(height * dpr);
    }
    canvas.style.height = `${height}px`;
    readColors();
    replay();
  }

  // Redraws the faint layer from the stored paths, for new axes or colours.
  function replay() {
    if (!layer) return;
    layer.getContext('2d')?.clearRect(0, 0, layer.width, layer.height);
    intoLayer(rows.slice(0, rows.length - newest.length));
    render();
  }

  export function append(values: Float64Array, rowLength: number, knockouts: ArrayLike<number>) {
    const added: Row[] = [];
    for (let r = 0; r < knockouts.length; ++r) {
      added.push({values: values.subarray(r * rowLength, (r + 1) * rowLength), knockedOut: knockouts[r] >= 0});
    }
    // The previous highlight, and all but the newest block of these, join the
    // faint layer.
    const split = Math.max(0, added.length - HIGHLIGHT);
    intoLayer([...newest, ...added.slice(0, split)]);
    newest = added.slice(split);
    rows.push(...added);
    count = rows.length;
    knockedOutCount += added.filter((row) => row.knockedOut).length;
    render();
  }

  export function clear() {
    rows = [];
    newest = [];
    count = 0;
    knockedOutCount = 0;
    if (layer) layer.getContext('2d')?.clearRect(0, 0, layer.width, layer.height);
    render();
  }

  // The spread of the paths still running at the hovered x, and how many had
  // stopped by then.
  const hover = $derived.by(() => {
    void count;
    if (hoverIndex === null || rows.length === 0) return null;
    const alive: number[] = [];
    for (const row of rows) {
      const v = row.values[hoverIndex];
      if (Number.isFinite(v)) alive.push(v);
    }
    alive.sort((a, b) => a - b);
    const q = (p: number) => alive[Math.min(alive.length - 1, Math.floor(p * alive.length))];
    return {
      x: xs[hoverIndex],
      stopped: rows.length - alive.length,
      quantiles: alive.length ? {p5: q(0.05), p50: q(0.5), p95: q(0.95)} : null,
    };
  });

  function onpointermove(e: PointerEvent) {
    const rect = canvas.getBoundingClientRect();
    const x = e.clientX - rect.left;
    const t = xMin + ((x - PAD.left) / (width - PAD.left - PAD.right)) * (xMax - xMin);
    let best = 0;
    for (let k = 1; k < xs.length; ++k) if (Math.abs(xs[k] - t) < Math.abs(xs[best] - t)) best = k;
    hoverIndex = x >= PAD.left - 8 && x <= width - PAD.right + 8 ? best : null;
  }

  // Replay the paths onto new axes; redraw for new references or hover.
  $effect(() => {
    void xs;
    void yDomain;
    untrack(replay);
  });
  $effect(() => {
    void references;
    void hoverIndex;
    untrack(render);
  });

  let cleanups: (() => void)[] = [];
  onMount(() => {
    redrawAll();
    const resize = new ResizeObserver(() => {
      if (container.clientWidth !== width) redrawAll();
    });
    resize.observe(container);
    // Re-read the colour tokens when the theme changes.
    const dark = window.matchMedia('(prefers-color-scheme: dark)');
    const theme = new MutationObserver(redrawAll);
    theme.observe(document.documentElement, {attributes: true, attributeFilter: ['data-theme']});
    dark.addEventListener('change', redrawAll);
    cleanups = [() => resize.disconnect(), () => theme.disconnect(), () => dark.removeEventListener('change', redrawAll)];
  });
  onDestroy(() => cleanups.forEach((c) => c()));
</script>

<div class="legend" aria-hidden="true">
  <span><i style:background="var(--series-2)" class="dot"></i>reached the target</span>
  <span><i style:background="var(--series-1)"></i>ran to the end</span>
  {#each references as ref (ref.label)}
    <span><i class="dashed"></i>{ref.label}</span>
  {/each}
</div>

<div
  class="wrap"
  bind:this={container}
  role="img"
  aria-label="{ariaLabel}: {count} paths drawn, {knockedOutCount} of which reached the target."
>
  <canvas
    bind:this={canvas}
    aria-hidden="true"
    {onpointermove}
    onpointerleave={() => (hoverIndex = null)}
  ></canvas>
  {#if hover}
    <div class="tooltip" style:left="{Math.min(Math.max((px(hover.x) / width) * 100, 18), 82)}%">
      <div class="tip-x">{xFormat(hover.x)}</div>
      {#if hover.quantiles}
        <div>5% <strong>{yFormat(hover.quantiles.p5)}</strong></div>
        <div>median <strong>{yFormat(hover.quantiles.p50)}</strong></div>
        <div>95% <strong>{yFormat(hover.quantiles.p95)}</strong></div>
      {/if}
      <div class="tip-x">{hover.stopped} of {count} stopped by then</div>
    </div>
  {/if}
</div>

<style>
  .wrap {
    position: relative;
  }
  canvas {
    display: block;
    width: 100%;
    touch-action: pan-y;
  }
  .legend {
    display: flex;
    flex-wrap: wrap;
    gap: 4px 16px;
    font: 13px var(--font-ui);
    color: var(--text-secondary);
    margin-bottom: 4px;
  }
  .legend i {
    display: inline-block;
    width: 12px;
    height: 3px;
    border-radius: 2px;
    margin-right: 6px;
    vertical-align: 3px;
  }
  .legend i.dot {
    width: 8px;
    height: 8px;
    border-radius: 50%;
    vertical-align: 0;
  }
  .legend i.dashed {
    height: 0;
    border-top: 2px dashed var(--text-secondary);
    border-radius: 0;
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
