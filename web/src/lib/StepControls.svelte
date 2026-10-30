<!--
  Play/step controls for backward induction. `revealedFrom` runs from the
  expiry index (only payoffs known) down to 0 (fully priced).
-->
<script lang="ts">
  import {onDestroy} from 'svelte';

  let {
    revealedFrom = $bindable(),
    expiryIndex,
  }: {revealedFrom: number; expiryIndex: number} = $props();

  let timer: ReturnType<typeof setInterval> | null = $state(null);

  function stop() {
    if (timer) clearInterval(timer);
    timer = null;
  }

  function play() {
    if (timer) return stop();
    if (revealedFrom === 0) revealedFrom = expiryIndex;
    const ms = Math.max(60, Math.min(400, 4000 / Math.max(1, expiryIndex)));
    timer = setInterval(() => {
      if (revealedFrom <= 0) return stop();
      revealedFrom -= 1;
    }, ms);
  }

  onDestroy(stop);
</script>

<div class="controls" role="group" aria-label="Backward induction">
  <button onclick={() => { stop(); revealedFrom = expiryIndex; }} disabled={revealedFrom === expiryIndex}>Reset to expiry</button>
  <button onclick={() => { stop(); revealedFrom = Math.min(expiryIndex, revealedFrom + 1); }} disabled={revealedFrom === expiryIndex} aria-label="Step forward in time">◀</button>
  <button class="primary" onclick={play}>{timer ? 'Pause' : revealedFrom === 0 ? 'Replay' : 'Play'}</button>
  <button onclick={() => { stop(); revealedFrom = Math.max(0, revealedFrom - 1); }} disabled={revealedFrom === 0} aria-label="Step back in time">▶</button>
  <button onclick={() => { stop(); revealedFrom = 0; }} disabled={revealedFrom === 0}>Show all</button>
  <span class="status">
    {revealedFrom === 0
      ? 'Priced: every step computed'
      : revealedFrom === expiryIndex
        ? 'Payoffs at expiry'
        : `Computed back to step ${revealedFrom} of ${expiryIndex}`}
  </span>
</div>

<style>
  .controls {
    display: flex;
    flex-wrap: wrap;
    align-items: center;
    gap: 6px;
    font-family: var(--font-ui);
    font-size: 14px;
    margin: 8px 0;
  }
  button {
    font-size: 14px;
    padding: 4px 10px;
    border-radius: 6px;
    border: 1px solid var(--border);
    background: var(--surface-raised);
    color: var(--text-primary);
    cursor: pointer;
  }
  button.primary {
    background: var(--accent);
    border-color: var(--accent);
    color: white;
    min-width: 5.5em;
  }
  button:disabled {
    opacity: 0.45;
    cursor: default;
  }
  .status {
    color: var(--text-secondary);
    margin-left: 6px;
    font-variant-numeric: tabular-nums;
  }
</style>
