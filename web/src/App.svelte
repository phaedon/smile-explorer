<script lang="ts">
  import BinomialEssay from './lib/BinomialEssay.svelte';
  import FxEssay from './lib/FxEssay.svelte';
  import HullWhiteEssay from './lib/HullWhiteEssay.svelte';
  import VolatilityEssay from './lib/VolatilityEssay.svelte';

  const pages = [
    {id: 'binomial', title: 'Binomial trees', component: BinomialEssay},
    {id: 'volatility', title: 'Volatility in the tree', component: VolatilityEssay},
    {id: 'fx', title: 'Currency options', component: FxEssay},
    {id: 'hull-white', title: 'Interest rate trees', component: HullWhiteEssay},
  ];

  // Hash routing (#/binomial), so the site works as static files anywhere.
  const pageFromHash = () =>
    pages.find((page) => `#/${page.id}` === location.hash) ?? pages[0];

  let current = $state(pageFromHash());

  $effect(() => {
    const onHashChange = () => {
      current = pageFromHash();
      window.scrollTo(0, 0);
    };
    window.addEventListener('hashchange', onHashChange);
    return () => window.removeEventListener('hashchange', onHashChange);
  });

  $effect(() => {
    document.title = `${current.title} · smile-explorer`;
  });
</script>

<nav>
  <span class="brand">smile-explorer</span>
  {#each pages as page (page.id)}
    <a href="#/{page.id}" aria-current={page.id === current.id ? 'page' : undefined}
      >{page.title}</a
    >
  {/each}
</nav>

{#key current.id}
  <current.component />
{/key}

<style>
  nav {
    max-width: 760px;
    margin: 0 auto;
    padding: 16px 16px 0;
    display: flex;
    flex-wrap: wrap;
    align-items: baseline;
    gap: 6px 20px;
    font: 14px var(--font-ui);
  }
  .brand {
    font-weight: 600;
    color: var(--text-primary);
    margin-right: auto;
  }
  a {
    color: var(--text-secondary);
    text-decoration: none;
    padding-bottom: 2px;
    border-bottom: 2px solid transparent;
  }
  a:hover {
    color: var(--text-primary);
  }
  a[aria-current='page'] {
    color: var(--text-primary);
    border-bottom-color: var(--accent);
  }
</style>
