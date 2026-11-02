<script lang="ts">
  import BinomialEssay from './lib/BinomialEssay.svelte';
  import BondOptionEssay from './lib/BondOptionEssay.svelte';
  import ConvexityEssay from './lib/ConvexityEssay.svelte';
  import FxEssay from './lib/FxEssay.svelte';
  import GasEssay from './lib/GasEssay.svelte';
  import GreeksEssay from './lib/GreeksEssay.svelte';
  import HullWhiteEssay from './lib/HullWhiteEssay.svelte';
  import SmileEssay from './lib/SmileEssay.svelte';
  import SwaptionEssay from './lib/SwaptionEssay.svelte';
  import TarfEssay from './lib/TarfEssay.svelte';
  import VolatilityEssay from './lib/VolatilityEssay.svelte';

  // The essays, in reading order, grouped by theme. The ids are the URLs
  // (#/binomial), so they stay fixed even if the order changes.
  const groups = [
    {
      name: 'Options on one asset',
      pages: [
        {
          id: 'binomial',
          title: 'Binomial trees',
          blurb: 'Risk-neutral probabilities, backward induction and early exercise.',
          component: BinomialEssay,
        },
        {
          id: 'volatility',
          title: 'Volatility in the tree',
          blurb: 'Term structures of vol, and local vol producing a smile.',
          component: VolatilityEssay,
        },
        {
          id: 'greeks',
          title: 'Greeks and hedging',
          blurb: 'Sensitivities from the tree, gamma near expiry, and hedging in practice.',
          component: GreeksEssay,
        },
        {
          id: 'fx',
          title: 'Currency options',
          blurb: 'An exchange rate on two interest rate curves.',
          component: FxEssay,
        },
        {
          id: 'tarf',
          title: 'Target redemption forwards',
          blurb: 'A better FX rate with a catch, priced by Monte Carlo, path by path.',
          component: TarfEssay,
        },
      ],
    },
    {
      name: 'Interest rates',
      pages: [
        {
          id: 'hull-white',
          title: 'Interest rate trees',
          blurb: 'A Hull–White tree for the short rate, fitted to today’s curve.',
          component: HullWhiteEssay,
        },
        {
          id: 'bond-options',
          title: 'Options on bonds',
          blurb: 'Pricing on the tree, the closed form, and when to exercise early.',
          component: BondOptionEssay,
        },
        {
          id: 'swaptions',
          title: 'Calibrating the rate tree',
          blurb: 'Swaptions and caps, fitting the model to quotes, and Bermudans.',
          component: SwaptionEssay,
        },
        {
          id: 'convexity',
          title: 'Futures, forwards and convexity',
          blurb: 'SOFR futures against cleared FRAs: margin, PAI, and the hedge that earns the gap.',
          component: ConvexityEssay,
        },
      ],
    },
    {
      name: 'Commodities',
      pages: [
        {
          id: 'gas',
          title: 'Natural gas',
          blurb: 'A seasonal futures curve, storage, mean reversion and options on futures.',
          component: GasEssay,
        },
      ],
    },
    {
      name: 'Putting it together',
      pages: [
        {
          id: 'smile',
          title: 'The smile, calibrated',
          blurb: 'From quoted implied vols to a local-vol tree that prices them back.',
          component: SmileEssay,
        },
      ],
    },
  ];
  const pages = groups.flatMap((group) => group.pages);

  // Hash routing (#/binomial), so the site works as static files anywhere.
  const pageFromHash = () =>
    pages.find((page) => `#/${page.id}` === location.hash) ?? pages[0];

  // Raw state: the pages are plain objects, compared by id.
  let current = $state.raw(pageFromHash());
  const index = $derived(pages.findIndex((page) => page.id === current.id));
  const previous = $derived(pages[index - 1] ?? null);
  const next = $derived(pages[index + 1] ?? null);

  let menuOpen = $state(false);
  let menuButton: HTMLButtonElement;
  let menu = $state<HTMLDivElement>();

  function closeMenu(returnFocus = false) {
    menuOpen = false;
    if (returnFocus) menuButton.focus();
  }

  $effect(() => {
    const onHashChange = () => {
      current = pageFromHash();
      menuOpen = false;
      window.scrollTo(0, 0);
    };
    window.addEventListener('hashchange', onHashChange);
    return () => window.removeEventListener('hashchange', onHashChange);
  });

  // Close the menu on Escape, or on a click outside it.
  $effect(() => {
    if (!menuOpen) return;
    const onKey = (e: KeyboardEvent) => {
      if (e.key === 'Escape') closeMenu(true);
    };
    const onClick = (e: MouseEvent) => {
      const target = e.target as Node;
      if (!menu?.contains(target) && !menuButton.contains(target)) closeMenu();
    };
    window.addEventListener('keydown', onKey);
    window.addEventListener('click', onClick);
    return () => {
      window.removeEventListener('keydown', onKey);
      window.removeEventListener('click', onClick);
    };
  });

  $effect(() => {
    document.title = `${current.title} · smile-explorer`;
  });
</script>

<header class="bar">
  <div class="inner">
    <a class="brand" href="#/{pages[0].id}">smile-explorer</a>
    <span class="where" aria-hidden="true">
      <span class="count">{index + 1}/{pages.length}</span>
      <span class="title">{current.title}</span>
    </span>
    <button
      bind:this={menuButton}
      class="contents"
      aria-expanded={menuOpen}
      aria-controls="contents-menu"
      onclick={() => (menuOpen = !menuOpen)}
    >
      Contents
      <svg viewBox="0 0 12 12" width="12" height="12" aria-hidden="true"
        ><path d={menuOpen ? 'M2 8 6 4l4 4' : 'M2 4l4 4 4-4'} /></svg
      >
    </button>
  </div>
  {#if menuOpen}
    <div class="menu" id="contents-menu" bind:this={menu}>
      <nav aria-label="Essays">
        {#each groups as group (group.name)}
          <h2>{group.name}</h2>
          <ol start={pages.indexOf(group.pages[0]) + 1}>
            {#each group.pages as page (page.id)}
              <li>
                <a href="#/{page.id}" aria-current={page.id === current.id ? 'page' : undefined}>
                  <span class="num">{pages.indexOf(page) + 1}</span>
                  <span class="text">
                    <span class="name">{page.title}</span>
                    <span class="blurb">{page.blurb}</span>
                  </span>
                </a>
              </li>
            {/each}
          </ol>
        {/each}
      </nav>
    </div>
  {/if}
</header>

{#key current.id}
  <current.component />
{/key}

<nav class="sequence" aria-label="Previous and next essays">
  {#if previous}
    <a class="prev" href="#/{previous.id}">
      <span class="dir">← Previous</span>
      <span class="name">{index}. {previous.title}</span>
    </a>
  {/if}
  {#if next}
    <a class="next" href="#/{next.id}">
      <span class="dir">Next →</span>
      <span class="name">{index + 2}. {next.title}</span>
    </a>
  {/if}
</nav>

<style>
  .bar {
    position: sticky;
    top: 0;
    z-index: 10;
    background: var(--surface);
    border-bottom: 1px solid var(--border);
    font: 14px var(--font-ui);
  }
  .inner {
    max-width: 760px;
    margin: 0 auto;
    padding: 10px 16px;
    display: flex;
    align-items: center;
    gap: 12px;
  }
  .brand {
    font-weight: 600;
    color: var(--text-primary);
    text-decoration: none;
    white-space: nowrap;
  }
  .where {
    flex: 1;
    min-width: 0;
    display: flex;
    gap: 8px;
    align-items: baseline;
    color: var(--text-secondary);
  }
  .count {
    font-variant-numeric: tabular-nums;
    color: var(--text-muted);
  }
  .title {
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
  .contents {
    display: inline-flex;
    align-items: center;
    gap: 6px;
    font: inherit;
    color: var(--text-primary);
    background: var(--surface-raised);
    border: 1px solid var(--border);
    border-radius: 6px;
    padding: 4px 10px;
    cursor: pointer;
  }
  .contents svg path {
    fill: none;
    stroke: currentColor;
    stroke-width: 1.6;
    stroke-linecap: round;
    stroke-linejoin: round;
  }
  .menu {
    max-width: 760px;
    margin: 0 auto;
    padding: 4px 16px 16px;
    max-height: calc(100vh - 60px);
    overflow-y: auto;
  }
  .menu h2 {
    font: 600 12px var(--font-ui);
    letter-spacing: 0.06em;
    text-transform: uppercase;
    color: var(--text-muted);
    margin: 14px 0 6px;
  }
  .menu ol {
    list-style: none;
    margin: 0;
    padding: 0;
    display: grid;
    gap: 2px;
  }
  .menu a {
    display: flex;
    gap: 12px;
    padding: 8px 10px;
    border-radius: 6px;
    text-decoration: none;
    color: var(--text-primary);
  }
  .menu a:hover {
    background: var(--surface-raised);
  }
  .menu a[aria-current='page'] {
    background: var(--surface-raised);
    box-shadow: inset 3px 0 0 var(--accent);
  }
  .num {
    width: 1.4em;
    flex: none;
    text-align: right;
    color: var(--text-muted);
    font-variant-numeric: tabular-nums;
  }
  .text {
    display: grid;
    gap: 2px;
  }
  .name {
    font-weight: 500;
  }
  .blurb {
    color: var(--text-secondary);
    font-size: 13px;
  }
  .sequence {
    max-width: 760px;
    margin: 0 auto 48px;
    padding: 0 16px;
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 12px;
    font: 14px var(--font-ui);
  }
  .sequence a {
    display: grid;
    gap: 2px;
    padding: 12px 14px;
    border: 1px solid var(--border);
    border-radius: 8px;
    text-decoration: none;
    color: var(--text-primary);
  }
  .sequence a:hover {
    background: var(--surface-raised);
  }
  .sequence .next {
    grid-column: 2;
    text-align: right;
  }
  .dir {
    color: var(--text-muted);
    font-size: 13px;
  }
</style>
