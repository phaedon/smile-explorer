// Reactive parameters for the volatility essay.

export const volParams = $state({
  spot: 100,
  rate: 0.03,
  strike: 100,

  // Implied vols at 1, 2 and 3 years (Derman, The Volatility Smile, 13.6).
  termVols: [0.2, 0.255, 0.311],
  // The first timestep is 1 / termStepsPerYear; later ones adapt to the vol.
  termStepsPerYear: 5,

  // Local vol skew: base + range / (1 + exp(stretch * (S - spot))).
  smileBase: 0.15,
  smileRange: 0.2,
  smileStretch: 0.05,
  smileStepsPerYear: 12,
});
