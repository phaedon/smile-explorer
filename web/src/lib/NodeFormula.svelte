<!--
  The arithmetic behind one node of the backward induction, with the actual
  numbers substituted in.
-->
<script lang="ts">
  import {fmt} from './format';
  import {nodeIndex, type VanillaPricingResult} from './engine';
  import type {NodeRef} from './TreeView.svelte';

  let {
    result,
    node,
    strike,
    isCall,
    isAmerican,
    revealedFrom = 0,
  }: {
    result: VanillaPricingResult;
    node: NodeRef;
    strike: number;
    isCall: boolean;
    isAmerican: boolean;
    revealedFrom?: number;
  } = $props();

  const T = $derived(result.expiryIndex);
  const n = $derived(nodeIndex(node.t, node.i));
  const S = $derived(result.assetValues[n]);
  const atExpiry = $derived(node.t === T);
  const payoffExpr = $derived(
    isCall
      ? `max(${fmt(S)} − ${fmt(strike)}, 0)`
      : `max(${fmt(strike)} − ${fmt(S)}, 0)`,
  );
  const up = $derived(atExpiry ? 0 : result.optionValues[nodeIndex(node.t + 1, node.i + 1)]);
  const down = $derived(atExpiry ? 0 : result.optionValues[nodeIndex(node.t + 1, node.i)]);
  const p = $derived(result.upProbs[n]);
  const exercised = $derived(
    isAmerican && !atExpiry && result.exerciseValues[n] > result.continuationValues[n] + 1e-12,
  );
</script>

<div class="panel" aria-live="polite">
  <div class="head">
    {node.t === 0 ? 'Today' : `Step ${node.t}`} · t = {fmt(result.times[node.t], 3)}y ·
    S = {fmt(S)}
  </div>
  {#if node.t < revealedFrom}
    <p class="note">Not computed yet: step the induction back to here.</p>
  {:else if atExpiry}
    <div class="eq">
      <span class="lhs">V</span><span>= payoff = {payoffExpr}</span>
      <span></span><span>= <strong>{fmt(result.optionValues[n])}</strong></span>
    </div>
  {:else}
    <div class="eq">
      <span class="lhs">hold</span>
      <span>= DF × (p · V<sub>up</sub> + (1 − p) · V<sub>down</sub>)</span>
      <span></span>
      <span
        >= {fmt(result.forwardDfs[node.t], 4)} × ({fmt(p, 4)} × {fmt(up)} + {fmt(
          1 - p,
          4,
        )} × {fmt(down)})</span
      >
      <span></span><span>= {fmt(result.continuationValues[n])}</span>
      {#if isAmerican}
        <span class="lhs">exercise</span><span>= {payoffExpr} = {fmt(result.exerciseValues[n])}</span>
        <span class="lhs">V</span>
        <span
          >= max(exercise, hold) = <strong>{fmt(result.optionValues[n])}</strong>
          {#if exercised}<em class="flag">exercise early</em>{/if}</span
        >
      {:else}
        <span class="lhs">V</span><span>= hold = <strong>{fmt(result.optionValues[n])}</strong></span>
      {/if}
    </div>
  {/if}
</div>

<style>
  .panel {
    font-family: var(--font-code);
    font-size: 13.5px;
    line-height: 1.6;
    background: var(--surface-raised);
    border: 1px solid var(--border);
    border-radius: 8px;
    padding: 10px 14px;
    font-variant-numeric: tabular-nums;
    min-height: 7.5em;
    overflow-x: auto;
  }
  .head {
    font-family: var(--font-ui);
    color: var(--text-secondary);
    font-size: 13px;
    margin-bottom: 4px;
  }
  .eq {
    display: grid;
    grid-template-columns: max-content 1fr;
    column-gap: 8px;
  }
  .lhs {
    color: var(--text-secondary);
    text-align: right;
  }
  .note {
    margin: 0;
    color: var(--text-secondary);
    font-family: var(--font-ui);
  }
  .flag {
    font-style: normal;
    font-family: var(--font-ui);
    font-size: 12px;
    color: var(--text-primary);
    border: 1.5px solid var(--series-2);
    border-radius: 4px;
    padding: 0 6px;
    margin-left: 6px;
  }
</style>
