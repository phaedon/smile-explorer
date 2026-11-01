// Reactive parameters for the natural gas essay. Every number is
// illustrative: a curve shaped like a US gas curve (dearest in winter), not
// market data.

import {
  engine,
  type CommodityOptionResult,
  type CommoditySpec,
  type CommodityTreeResult,
} from './engine';

// Valued on 1 November. Contract m (1 to 24) delivers at the start of the
// m-th month after: December, January, and so on to the November after next.
export const NUM_CONTRACTS = 24;
export const CONTRACT_TIMES = Array.from({length: NUM_CONTRACTS}, (_, k) => (k + 1) / 12);
const MONTHS = ['Nov', 'Dec', 'Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct'];
// The name of the month m months after November: Dec for 1, Jan for 2.
export const monthName = (m: number) => MONTHS[((m % 12) + 12) % 12];
// The month at time t (in years), for axis labels.
export const monthAt = (t: number) => monthName(Math.round(t * 12));

export const gasParams = $state({
  spot: 3.3,
  // The curve's shape: its level, the winter premium (the size of the
  // seasonal swing, peaking in January), and its slope per year.
  level: 3.2,
  winter: 0.1,
  slope: 0.03,
  // Each contract's price relative to that shape, for months dragged by hand.
  tweaks: Array.from({length: NUM_CONTRACTS}, () => 0),

  rate: 0.04,
  // Storage cost, in dollars per MMBtu per month.
  storage: 0.08,

  meanReversion: 1.5,
  sigma: 0.6,
  stepsPerMonth: 4,

  // The option: on contract `contract`, expiring `monthsEarly` months before
  // delivery.
  contract: 3,
  monthsEarly: 0,
  strike: 3.5,
  isCall: true,
  isAmerican: true,
});

export type GasParams = typeof gasParams;

// The curve's shape before any hand-dragged changes.
export function shapePrice(p: GasParams, k: number): number {
  const t = CONTRACT_TIMES[k];
  return p.level * Math.exp(p.slope * t) * (1 + p.winter * Math.cos(2 * Math.PI * (t - 2 / 12)));
}

export function futuresPrices(p: GasParams): number[] {
  return CONTRACT_TIMES.map((_, k) => shapePrice(p, k) * (1 + p.tweaks[k]));
}

export function gasSpecFor(p: GasParams, horizonYears = NUM_CONTRACTS / 12): CommoditySpec {
  return {
    spot: p.spot,
    futuresTimes: CONTRACT_TIMES,
    futuresPrices: futuresPrices(p),
    rate: p.rate,
    meanReversion: p.meanReversion,
    sigma: p.sigma,
    stepsPerMonth: p.stepsPerMonth,
    horizonYears,
  };
}

const message = (e: unknown) => (e instanceof Error ? e.message : String(e));

export type GasTreeOutcome =
  | {tree: CommodityTreeResult; error?: undefined}
  | {tree?: undefined; error: string};

export function tryBuildGasTree(spec: CommoditySpec): GasTreeOutcome {
  try {
    return {tree: engine.buildCommodityTree(spec)};
  } catch (e) {
    return {error: message(e)};
  }
}

export type GasOptionOutcome =
  | {option: CommodityOptionResult; error?: undefined}
  | {option?: undefined; error: string};

export function tryPriceGasOption(p: GasParams): GasOptionOutcome {
  const delivery = p.contract / 12;
  try {
    return {
      option: engine.priceCommodityOption({
        commodity: gasSpecFor(p, delivery),
        option: {
          deliveryYears: delivery,
          expiryYears: Math.max(1, p.contract - p.monthsEarly) / 12,
          strike: p.strike,
          isCall: p.isCall,
          isAmerican: p.isAmerican,
        },
      }),
    };
  } catch (e) {
    return {error: message(e)};
  }
}
