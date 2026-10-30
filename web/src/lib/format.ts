export function fmt(value: number, digits = 2): string {
  return Number.isFinite(value) ? value.toFixed(digits) : '–';
}

export function pct(value: number, digits = 0): string {
  return `${(value * 100).toFixed(digits)}%`;
}

// Round, evenly spaced axis ticks covering [lo, hi] (about `count` of them).
export function niceTicks(lo: number, hi: number, count = 5): number[] {
  if (!(hi > lo)) return [lo];
  const raw = (hi - lo) / count;
  const mag = 10 ** Math.floor(Math.log10(raw));
  const step =
    [1, 2, 2.5, 5, 10].map((m) => m * mag).find((s) => s >= raw) ?? 10 * mag;
  const ticks: number[] = [];
  for (let v = Math.ceil(lo / step) * step; v <= hi + step * 1e-9; v += step) {
    ticks.push(Number(v.toPrecision(12)));
  }
  return ticks;
}

// Ticks for a logarithmic axis: 1, 2 and 5 times powers of ten (or only the
// powers of ten when the range is wide), falling back to linear ticks for
// narrow ranges.
export function logTicks(lo: number, hi: number): number[] {
  if (!(lo > 0) || hi / lo < 4) return niceTicks(lo, hi, 5);
  const ticks = (mantissas: number[]) => {
    const out: number[] = [];
    for (let k = Math.floor(Math.log10(lo)); k <= Math.ceil(Math.log10(hi)); ++k) {
      for (const m of mantissas) {
        const v = Number((m * 10 ** k).toPrecision(12));
        if (v >= lo && v <= hi) out.push(v);
      }
    }
    return out;
  };
  const dense = ticks([1, 2, 5]);
  return dense.length <= 7 ? dense : ticks([1]);
}
