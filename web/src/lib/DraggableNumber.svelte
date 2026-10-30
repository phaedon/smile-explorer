<!--
  A number you can drag left/right (or nudge with the arrow keys) to change,
  in the spirit of Bret Victor's Tangle. Used inline in prose and code.
-->
<script lang="ts">
  let {
    value = $bindable(),
    min,
    max,
    step = 1,
    label,
    format = (v: number) => String(v),
  }: {
    value: number;
    min: number;
    max: number;
    step?: number;
    label: string;
    format?: (v: number) => string;
  } = $props();

  // Horizontal pixels of drag per step.
  const PX_PER_STEP = 5;

  let drag = $state<{x: number; start: number} | null>(null);

  function clamp(v: number): number {
    const snapped = Math.round(v / step) * step;
    // Avoid accumulating float noise such as 0.15000000000000002.
    const decimals = (String(step).split('.')[1] ?? '').length;
    return Math.min(max, Math.max(min, Number(snapped.toFixed(decimals))));
  }

  function onpointerdown(e: PointerEvent) {
    (e.currentTarget as HTMLElement).setPointerCapture(e.pointerId);
    drag = {x: e.clientX, start: value};
    e.preventDefault();
  }

  function onpointermove(e: PointerEvent) {
    if (!drag) return;
    const steps = Math.round((e.clientX - drag.x) / PX_PER_STEP);
    value = clamp(drag.start + steps * step);
  }

  function endDrag() {
    drag = null;
  }

  function onkeydown(e: KeyboardEvent) {
    const delta = {ArrowRight: 1, ArrowUp: 1, ArrowLeft: -1, ArrowDown: -1}[
      e.key
    ];
    if (delta === undefined) return;
    value = clamp(value + delta * step);
    e.preventDefault();
  }
</script>

<span
  class="draggable"
  class:dragging={drag !== null}
  role="slider"
  tabindex="0"
  aria-label={label}
  aria-valuemin={min}
  aria-valuemax={max}
  aria-valuenow={value}
  aria-valuetext={format(value)}
  title="Drag, or use the arrow keys, to change {label}"
  {onpointerdown}
  {onpointermove}
  onpointerup={endDrag}
  onpointercancel={endDrag}
  {onkeydown}>{format(value)}</span
>

<style>
  .draggable {
    color: var(--accent);
    border-bottom: 1px dashed currentColor;
    cursor: ew-resize;
    user-select: none;
    touch-action: none;
    font-variant-numeric: tabular-nums;
    white-space: nowrap;
  }
  .draggable:hover,
  .dragging {
    border-bottom-style: solid;
  }
  .draggable:focus-visible {
    outline: 2px solid var(--accent);
    outline-offset: 2px;
    border-radius: 2px;
  }
</style>
