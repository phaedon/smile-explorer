<!--
  Controls for a live Monte Carlo run: play/pause, one block at a time, start
  over, and the speed, with the progress so far.
-->
<script lang="ts">
  import {BLOCK_PATHS, type Speed, type TarfRun} from './tarfRun.svelte';

  let {run}: {run: TarfRun} = $props();

  const SPEEDS: {speed: Speed; label: string}[] = [
    {speed: 'slow', label: 'Slow'},
    {speed: 'medium', label: 'Medium'},
    {speed: 'fast', label: 'Fast'},
  ];

  const running = $derived(run.status === 'running');
  const finished = $derived(run.status === 'done');
</script>

<div class="controls">
  <div class="buttons">
    <button
      type="button"
      class="primary"
      disabled={finished}
      onclick={() => (running ? run.pause() : run.play())}
    >
      {running ? 'Pause' : run.done > 0 && !finished ? 'Resume' : 'Run'}
    </button>
    <button type="button" disabled={finished || running} onclick={() => run.step()}>
      +{BLOCK_PATHS} paths
    </button>
    <button type="button" disabled={run.done === 0} onclick={() => run.reset()}>Start over</button>
  </div>
  <div class="speeds" role="group" aria-label="Speed">
    {#each SPEEDS as s (s.speed)}
      <button type="button" aria-pressed={run.speed === s.speed} onclick={() => (run.speed = s.speed)}>
        {s.label}
      </button>
    {/each}
  </div>
</div>

<div
  class="progress"
  role="progressbar"
  aria-label="Paths simulated"
  aria-valuemin={0}
  aria-valuemax={run.total}
  aria-valuenow={run.done}
>
  <div class="fill" style:width="{(run.done / run.total) * 100}%"></div>
</div>
<p class="run-count">
  {run.done.toLocaleString('en-US')} of {run.total.toLocaleString('en-US')} paths{run.status === 'paused'
    ? ' · paused'
    : finished
      ? ' · done'
      : ''}
</p>

<style>
  .controls {
    display: flex;
    flex-wrap: wrap;
    justify-content: space-between;
    gap: 8px;
    margin: 16px 0 8px;
  }
  .buttons,
  .speeds {
    display: flex;
    flex-wrap: wrap;
    gap: 6px;
  }
  button {
    font: 14px var(--font-ui);
    padding: 4px 12px;
    border-radius: 6px;
    border: 1px solid var(--border);
    background: var(--surface-raised);
    color: var(--text-primary);
    cursor: pointer;
  }
  button.primary {
    border-color: var(--accent);
    background: var(--accent);
    color: #fff;
    min-width: 6em;
  }
  button:disabled {
    opacity: 0.45;
    cursor: default;
  }
  .speeds button[aria-pressed='true'] {
    border-color: var(--accent);
    color: var(--accent);
  }
  button:focus-visible {
    outline: 2px solid var(--accent);
    outline-offset: 2px;
  }
  .progress {
    height: 6px;
    border-radius: 3px;
    background: var(--surface-raised);
    border: 1px solid var(--border);
    overflow: hidden;
  }
  .fill {
    height: 100%;
    background: var(--accent);
    transition: width 120ms linear;
  }
  .run-count {
    font: 13px var(--font-ui);
    color: var(--text-secondary);
    margin: 4px 0 0;
    font-variant-numeric: tabular-nums;
  }
</style>
