<!-- A clickable token that flips between two labels, e.g. Call / Put. -->
<script lang="ts">
  let {
    value = $bindable(),
    on,
    off,
    label,
  }: {value: boolean; on: string; off: string; label: string} = $props();
</script>

<button
  type="button"
  class="toggle"
  aria-label="{label}: {value ? on : off} (click to switch)"
  title="Click to switch"
  onclick={() => (value = !value)}>{value ? on : off}</button
>

<style>
  .toggle {
    all: unset;
    color: var(--accent);
    border-bottom: 1px dashed currentColor;
    cursor: pointer;
    font: inherit;
  }
  .toggle:hover {
    border-bottom-style: solid;
  }
  .toggle:focus-visible {
    outline: 2px solid var(--accent);
    outline-offset: 2px;
    border-radius: 2px;
  }
</style>
