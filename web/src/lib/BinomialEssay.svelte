<!--
  "Pricing an option on a binomial tree": the first interactive essay page.
  Every number is computed live by the C++ engine (via WebAssembly).
-->
<script lang="ts">
  import Convergence from './Convergence.svelte';
  import DraggableNumber from './DraggableNumber.svelte';
  import Distribution from './Distribution.svelte';
  import NodeFormula from './NodeFormula.svelte';
  import StepControls from './StepControls.svelte';
  import ToggleToken from './ToggleToken.svelte';
  import TreeView, {type NodeRef} from './TreeView.svelte';
  import {tryPrice, type VanillaPricingResult} from './engine';
  import {fmt, pct} from './format';
  import {params, requestFor} from './params.svelte';

  const outcome = $derived(tryPrice(requestFor(params)));

  // Keep showing the last good result while the parameters are invalid.
  let lastGood: VanillaPricingResult | null = null;
  const result = $derived.by(() => {
    if (outcome.result) lastGood = outcome.result;
    return outcome.result ?? lastGood;
  });

  const T = $derived(result?.expiryIndex ?? 0);
  const S0 = $derived(result?.assetValues[0] ?? params.spot);
  const u = $derived(result ? result.assetValues[2] / S0 : NaN);
  const d = $derived(result ? result.assetValues[1] / S0 : NaN);
  const dt = $derived(result ? result.times[1] : NaN);
  const p = $derived(result?.upProbs[0] ?? NaN);
  const growth = $derived(Math.exp(params.rate * dt));

  let assetHover = $state<NodeRef | null>(null);
  let optionHover = $state<NodeRef | null>(null);

  // Backward induction progress: the earliest time index computed so far.
  // Starts at expiry, where only the payoffs are known.
  let revealedFrom = $state(Number.POSITIVE_INFINITY);
  $effect(() => {
    if (revealedFrom > T) revealedFrom = T;
  });

  // Without a hovered node, explain the middle node of the latest step.
  const shownNode = $derived.by(() => {
    if (optionHover) return optionHover;
    const t = Math.min(revealedFrom, T);
    return {t, i: Math.ceil(t / 2)};
  });

  function tryAmericanPut() {
    params.isCall = false;
    params.isAmerican = true;
    params.rate = Math.max(params.rate, 0.08);
    revealedFrom = 0;
  }

  const priceDecimals = 4;
  const yearsFmt = (v: number) => fmt(v, 2);
</script>

<article>
  <header>
    <p class="kicker">smile-explorer · interactive notes</p>
    <h1>Pricing an option on a binomial tree</h1>
    <p class="lede">
      The binomial tree is the oldest trick in derivatives pricing, and still
      one of the clearest. Every number on this page is computed live by
      smile-explorer's C++ pricing engine, compiled to WebAssembly and running
      in your browser. <span class="hint">Drag any
        <span class="sample">underlined number</span> to change it.</span
      >
    </p>
  </header>

  {#if outcome.error}
    <div class="error" role="alert">
      <strong>The engine rejected these parameters:</strong>
      {outcome.error}. Showing the last valid tree.
    </div>
  {/if}

  <section>
    <h2>1. A tree of possible prices</h2>
    <p>
      Start with a stock at S<sub>0</sub> =
      <DraggableNumber bind:value={params.spot} min={50} max={150} step={1} label="spot price" />.
      Split the next
      <DraggableNumber
        bind:value={params.expiryYears}
        min={0.25}
        max={3}
        step={0.25}
        label="expiry in years"
        format={yearsFmt}
      /> years into steps of Δt = 1/<DraggableNumber
        bind:value={params.stepsPerYear}
        min={1}
        max={40}
        step={1}
        label="steps per year"
      /> of a year. In each step the price either rises by a factor <i>u</i> or
      falls by a factor <i>d</i>.
    </p>
    <p>
      Cox, Ross and Rubinstein chose <i>u</i> = e<sup>σ√Δt</sup> and
      <i>d</i> = 1/<i>u</i>. With volatility σ =
      <DraggableNumber
        bind:value={params.vol}
        min={0.05}
        max={0.8}
        step={0.01}
        label="volatility"
        format={(v) => pct(v)}
      />, that gives <i>u</i> = {fmt(u, 4)} and <i>d</i> = {fmt(d, 4)}. An up
      move followed by a down move lands exactly where a down then an up does,
      so the tree <em>recombines</em>: after <i>n</i> steps there are only
      <i>n</i> + 1 possible prices, not 2<sup><i>n</i></sup>.
    </p>

    <pre><code>Volatility vol(FlatVol(<DraggableNumber
          bind:value={params.vol}
          min={0.05}
          max={0.8}
          step={0.01}
          label="volatility"
          format={(v) => fmt(v, 2)}
        />));
StochasticTreeModel&lt;CRRPropagator&gt; asset(
    BinomialTree(<DraggableNumber
          bind:value={params.expiryYears}
          min={0.25}
          max={3}
          step={0.25}
          label="expiry in years"
          format={yearsFmt}
        />, 1 / <DraggableNumber
          bind:value={params.stepsPerYear}
          min={1}
          max={40}
          step={1}
          label="steps per year"
        />.), CRRPropagator(<DraggableNumber
          bind:value={params.spot}
          min={50}
          max={150}
          step={1}
          label="spot price"
        />));
asset.forwardPropagate(vol);</code></pre>

    {#if result}
      <figure>
        <TreeView {result} mode="asset" bind:hovered={assetHover} />
        <figcaption>
          {#if assetHover}
            Step {assetHover.t}, after {assetHover.i} up and {assetHover.t - assetHover.i}
            down moves: S = {fmt(result.assetValues[(assetHover.t * (assetHover.t + 1)) / 2 + assetHover.i])}.
          {:else}
            The asset tree: {T} steps, {((T + 1) * (T + 2)) / 2} nodes. The price
            axis is logarithmic, so every up move is the same height. Hover a
            node to read it.
          {/if}
        </figcaption>
      </figure>
    {/if}
  </section>

  <section>
    <h2>2. Risk-neutral probabilities</h2>
    <p>
      To price an option we don't need the real-world odds of an up move.
      Instead, pick the probability <i>p</i> under which the stock is expected
      to grow at exactly the risk-free rate <i>r</i> =
      <DraggableNumber
        bind:value={params.rate}
        min={0}
        max={0.2}
        step={0.005}
        label="risk-free rate"
        format={(v) => pct(v, 1)}
      />:
    </p>
    <p class="display">
      <i>p</i> = (e<sup><i>r</i>Δt</sup> − <i>d</i>) / (<i>u</i> − <i>d</i>)
      = ({fmt(growth, 4)} − {fmt(d, 4)}) / ({fmt(u, 4)} − {fmt(d, 4)})
      = <strong>{fmt(p, 4)}</strong>
    </p>
    <p>
      This isn't a forecast. It is the only probability that makes a hedged
      position earn the risk-free rate, so any other price would allow
      arbitrage. If e<sup><i>r</i>Δt</sup> falls outside [<i>d</i>, <i>u</i>]
      there is no such <i>p</i>, and the engine refuses to price. Try a 20% rate
      with 5% vol and one step a year.
    </p>
  </section>

  <section>
    <h2>3. Backward induction</h2>
    <p>
      At expiry, the option is worth its payoff. For a
      <ToggleToken bind:value={params.isCall} on="call" off="put" label="option type" />
      struck at <i>K</i> =
      <DraggableNumber bind:value={params.strike} min={50} max={150} step={1} label="strike" />
      that is {params.isCall ? 'max(S − K, 0)' : 'max(K − S, 0)'}. Stepping back
      in time, each node is worth the discounted expected value of its two
      successors:
    </p>
    <p class="display">
      V = e<sup>−<i>r</i>Δt</sup> × (<i>p</i> · V<sub>up</sub> + (1 − <i>p</i>) ·
      V<sub>down</sub>)
    </p>
    <p>
      Press play to watch the values flow back from expiry to today, then hover
      over any node to see its arithmetic.
    </p>

    <pre><code>SingleAssetDerivative deriv(&amp;asset.binomialTree(), &amp;curve);
double price = deriv.price(
    VanillaOption(<DraggableNumber
          bind:value={params.strike}
          min={50}
          max={150}
          step={1}
          label="strike"
        />, OptionPayoff::<ToggleToken
          bind:value={params.isCall}
          on="Call"
          off="Put"
          label="option type"
        />, ExerciseStyle::<ToggleToken
          bind:value={params.isAmerican}
          on="American"
          off="European"
          label="exercise style"
        />),
    <DraggableNumber
          bind:value={params.expiryYears}
          min={0.25}
          max={3}
          step={0.25}
          label="expiry in years"
          format={yearsFmt}
        />);  // = {result ? fmt(result.price, priceDecimals) : '–'}</code></pre>

    {#if result}
      <figure>
        <StepControls bind:revealedFrom expiryIndex={T} />
        <div class="legend" aria-hidden="true">
          <span class="ramp"
            >option value: 0 <i class="gradient"></i>
            {fmt(Math.max(...result.optionValues))}</span
          >
          {#if Math.min(revealedFrom, T) > 0}
            <span><i class="hollow"></i>not computed yet</span>
          {/if}
          {#if params.isAmerican}
            <span><i class="diamond"></i>exercise early</span>
          {/if}
        </div>
        <TreeView
          {result}
          mode="option"
          strike={params.strike}
          isAmerican={params.isAmerican}
          revealedFrom={Math.min(revealedFrom, T)}
          bind:hovered={optionHover}
        />
        <NodeFormula
          {result}
          node={shownNode}
          strike={params.strike}
          isCall={params.isCall}
          isAmerican={params.isAmerican}
          revealedFrom={Math.min(revealedFrom, T)}
        />
      </figure>

      <p class="stat">
        Tree price <strong>{fmt(result.price, priceDecimals)}</strong>
        <span class="muted"
          >· Black–Scholes (European) {fmt(result.bsmPrice, priceDecimals)}</span
        >
      </p>
    {/if}
  </section>

  <section>
    <h2>4. Exercising early</h2>
    <p>
      An American option can be exercised at any node, so its value there is
      the larger of exercising now and holding on. That takes one extra line of
      code: V = max(payoff, hold). For calls on a stock without dividends,
      exercising early never pays, but for puts it often does: deep in the money
      you would rather have the strike now and earn interest on it.
      <button class="inline" onclick={tryAmericanPut}>Show an American put</button>
      and the nodes where exercising beats holding light up. Together they trace
      out the <em>early-exercise boundary</em>.
    </p>
  </section>

  <section>
    <h2>5. Where the probability goes</h2>
    <p>
      Run the tree forwards instead of backwards and you get the
      <em>Arrow–Debreu price</em> of every node: what $1, paid only if that node
      is reached, is worth today. At expiry they form the discounted
      risk-neutral distribution of the stock price, and pricing any European
      payoff is just a weighted sum.
    </p>
    {#if result}
      <figure>
        <Distribution {result} strike={params.strike} isAmerican={params.isAmerican} />
      </figure>
    {/if}
  </section>

  <section>
    <h2>6. Converging to Black–Scholes</h2>
    <p>
      As the steps get smaller, the price of a European option on the tree
      converges to the Black–Scholes formula. It doesn't converge smoothly, though: it zig-zags,
      because the payoff's kink at the strike falls in a different place
      relative to the nodes at expiry as the number of steps changes.
    </p>
    <figure>
      <Convergence p={params} />
    </figure>
  </section>

  <footer>
    Built with <a href="https://github.com/phaedon/smile-explorer">smile-explorer</a>,
    following Derman's <i>The Volatility Smile</i> and Hull's
    <i>Options, Futures, and Other Derivatives</i>.
  </footer>
</article>

<style>
  article {
    max-width: 760px;
    margin: 0 auto;
    padding: 48px 16px 64px;
  }
  .kicker {
    font: 600 13px var(--font-ui);
    letter-spacing: 0.06em;
    text-transform: uppercase;
    color: var(--text-muted);
    margin: 0;
  }
  h1 {
    font-size: clamp(30px, 6vw, 42px);
    margin: 8px 0 16px;
  }
  h2 {
    font-size: 24px;
    margin: 56px 0 12px;
  }
  .lede {
    font-size: 20px;
    color: var(--text-secondary);
  }
  .hint {
    color: var(--text-primary);
  }
  .sample {
    color: var(--accent);
    border-bottom: 1px dashed currentColor;
  }
  pre {
    background: var(--surface-raised);
    border: 1px solid var(--border);
    border-radius: 8px;
    padding: 14px 16px;
    overflow-x: auto;
    line-height: 1.6;
    font-size: 15px;
  }
  pre code {
    font-size: 14px;
  }
  .display {
    text-align: center;
    font-variant-numeric: tabular-nums;
    overflow-x: auto;
  }
  figure {
    margin: 24px 0;
  }
  figcaption {
    font: 14px/1.5 var(--font-ui);
    color: var(--text-secondary);
    margin-top: 6px;
    min-height: 3em;
  }
  .legend {
    display: flex;
    flex-wrap: wrap;
    gap: 16px;
    font: 13px var(--font-ui);
    color: var(--text-secondary);
    margin: 4px 0;
    font-variant-numeric: tabular-nums;
  }
  .legend i {
    display: inline-block;
    vertical-align: -1px;
    margin-right: 6px;
  }
  .gradient {
    width: 80px;
    height: 10px;
    border-radius: 2px;
    margin: 0 6px;
    background: linear-gradient(
      to right,
      var(--seq-0),
      var(--seq-3),
      var(--seq-6),
      var(--seq-9)
    );
  }
  .hollow {
    width: 10px;
    height: 10px;
    border-radius: 50%;
    border: 1px solid var(--text-muted);
  }
  .diamond {
    width: 9px;
    height: 9px;
    border: 2px solid var(--series-2);
    transform: rotate(45deg);
  }
  .stat {
    font: 16px var(--font-ui);
    font-variant-numeric: tabular-nums;
  }
  .muted {
    color: var(--text-secondary);
  }
  .error {
    position: sticky;
    top: 8px;
    z-index: 1;
    font: 14px/1.5 var(--font-ui);
    color: var(--error);
    background: var(--surface-raised);
    border: 1px solid var(--error);
    border-radius: 8px;
    padding: 10px 14px;
    margin: 16px 0;
  }
  button.inline {
    font: 15px var(--font-ui);
    padding: 2px 10px;
    border-radius: 6px;
    border: 1px solid var(--accent);
    background: transparent;
    color: var(--accent);
    cursor: pointer;
  }
  footer {
    margin-top: 64px;
    padding-top: 16px;
    border-top: 1px solid var(--border);
    font: 14px var(--font-ui);
    color: var(--text-muted);
  }
</style>
