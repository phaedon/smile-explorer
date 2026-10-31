// Reactive parameters for the smile calibration essay.

export const SMILE_EXPIRIES = [0.25, 0.5, 1, 2];
export const SMILE_STRIKES = Array.from({length: 13}, (_, k) => 70 + 5 * k);

export const smileParams = $state({
  spot: 100,
  rate: 0.02,
  // The quote generator's knobs: at-the-money vol per expiry, and how vol
  // changes with standardised moneyness m = ln(K / F) / sqrt(T).
  atmVols: [0.22, 0.2, 0.19, 0.185],
  skew: 0.06,
  curvature: 0.02,
  noise: true,
  // Which expiry the slice charts show (an index into SMILE_EXPIRIES).
  slice: 2,
  // Steps a year for repricing on the tree (a multiple of 4, so that every
  // expiry falls on a step).
  stepsPerYear: 100,
});

export type SmileParams = typeof smileParams;

// A fixed pattern of "noise", in vol points, so quotes look like quotes but
// stay reproducible.
const NOISE = [0.3, -0.2, 0.1, 0.4, -0.3, 0.0, 0.2, -0.1, 0.3, -0.4, 0.1, 0.2, -0.2];

// Illustrative quotes (not market data): a vol quadratic in standardised
// moneyness, a different family from SSVI, so that fitting it is a real fit.
export function quotesFor(p: SmileParams) {
  const vols = SMILE_EXPIRIES.flatMap((t, i) =>
    SMILE_STRIKES.map((k, j) => {
      const m = Math.log(k / (p.spot * Math.exp(p.rate * t))) / Math.sqrt(t);
      const noise = p.noise ? NOISE[(j + 3 * i) % NOISE.length] / 1e3 : 0;
      return Math.max(0.02, p.atmVols[i] - p.skew * m + p.curvature * m * m + noise);
    }),
  );
  return {spot: p.spot, rate: p.rate, expiries: SMILE_EXPIRIES, strikes: SMILE_STRIKES, vols};
}
