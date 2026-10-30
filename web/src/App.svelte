<script lang="ts">
  import {engine} from './lib/engine';

  const {price} = engine.priceVanilla({
    tree: {
      model: engine.TreeModel.CoxRossRubinstein,
      spot: 100,
      vol: 0.2,
      rate: 0.05,
      drift: 0,
      dtYears: 1 / 52,
    },
    option: {strike: 100, expiryYears: 1, isCall: true, isAmerican: false},
  });
</script>

<p>Engine loaded: a 1-year at-the-money call is worth {price.toFixed(4)}.</p>
