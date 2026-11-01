// The live Monte Carlo run behind the TARF essay. The page pulls paths from a
// worker a few blocks at a time and asks for the next blocks only once it has
// taken in the last ones, so work never queues up behind a slow page: pausing
// is not asking, and a slow device simply runs slower.

import type {TarfSpec} from './engine';
import type {PathJob, PathReply} from './tarfPathWorker';

// Paths per block. Each block is seeded by its index, so the run is the same
// whatever the speed (which only changes how many blocks a request asks for).
export const BLOCK_PATHS = 25;
// Paths recorded in full, for drawing; the run keeps pricing past them.
export const RECORDED_PATHS = 2000;
const RECORD_BLOCKS = RECORDED_PATHS / BLOCK_PATHS;
// While paths are still being recorded, at most this many blocks a request,
// so that drawing them never holds up the page for long.
const MAX_DRAWN_BLOCKS = 8;
export const SEED = 1;

export type Speed = 'slow' | 'medium' | 'fast';

// Blocks per request, and the pause before the next request (in ms; 0 means
// the next animation frame).
const PACE: Record<Speed, {blocks: number; delay: number}> = {
  slow: {blocks: 1, delay: 400},
  medium: {blocks: 4, delay: 100},
  fast: {blocks: 40, delay: 0},
};

// Paths recorded in a reply, for the charts to draw.
export type RecordedPaths = {
  count: number;
  spots: Float64Array;
  profits: Float64Array;
  knockouts: Int32Array;
  times: Float64Array;
  fixingTimes: Float64Array;
};

export type RunPoint = {paths: number; mean: number; halfWidth: number};

export class TarfRun {
  status = $state<'idle' | 'running' | 'paused' | 'done'>('idle');
  speed = $state<Speed>('medium');
  done = $state(0);
  error = $state<string | null>(null);
  // Bumped whenever paths arrive, for charts that read the arrays below.
  version = $state(0);
  history = $state.raw<RunPoint[]>([]);
  forwards = $state.raw<Float64Array | null>(null);

  readonly total: number;
  // Per path, in the order simulated: the NPV, and the fixing (from 0) at
  // which it reached the target, or -1.
  readonly npvs: Float64Array;
  readonly knockouts: Int16Array;

  private sum = 0;
  private sumSquares = 0;
  private spec: TarfSpec | null = null;
  private id = 0;
  private nextBlock = 0;
  private pending = false;
  private timer: ReturnType<typeof setTimeout> | null = null;
  private frame: number | null = null;
  private pausedWhileHidden = false;
  private readonly worker: Worker;

  constructor(
    total: number,
    private readonly onPaths: (paths: RecordedPaths) => void,
    private readonly onRestart: () => void,
  ) {
    this.total = total;
    this.npvs = new Float64Array(total);
    this.knockouts = new Int16Array(total);
    this.worker = new Worker(new URL('./tarfPathWorker.ts', import.meta.url), {type: 'module'});
    this.worker.onmessage = (e: MessageEvent<PathReply>) => this.absorb(e.data);
    this.worker.onerror = (e) => {
      this.error = `the simulation failed to load (${e.message || 'worker error'})`;
      this.status = 'paused';
    };
    if (typeof window !== 'undefined' && window.matchMedia?.('(prefers-reduced-motion: reduce)').matches) {
      this.speed = 'fast';
    }
    document.addEventListener('visibilitychange', this.onVisibility);
  }

  get mean(): number {
    return this.done > 0 ? this.sum / this.done : NaN;
  }

  // Half the width of the 95% confidence interval on the mean.
  get halfWidth(): number {
    const n = this.done;
    if (n < 2) return NaN;
    const variance = Math.max(0, (this.sumSquares - (this.sum * this.sum) / n) / (n - 1));
    return 1.96 * Math.sqrt(variance / n);
  }

  // Starts over with new terms: running if `then` is 'play', or if it is
  // 'keep' and the old run was running.
  restart(spec: TarfSpec, then: 'keep' | 'play' | 'stop' = 'keep') {
    const wasRunning = this.status === 'running' || this.pausedWhileHidden;
    this.pausedWhileHidden = false;
    this.spec = spec;
    this.clearSchedule();
    this.id += 1;
    this.nextBlock = 0;
    this.pending = false;
    this.done = 0;
    this.sum = 0;
    this.sumSquares = 0;
    this.history = [];
    this.forwards = null;
    this.error = null;
    this.version += 1;
    this.onRestart();
    this.status = 'idle';
    if (then === 'play' || (then === 'keep' && wasRunning)) this.play();
  }

  // The same terms, from the first path again, stopped.
  reset() {
    if (this.spec) this.restart(this.spec, 'stop');
  }

  play() {
    if (!this.spec || this.done >= this.total) return;
    this.status = 'running';
    this.pump();
  }

  pause() {
    if (this.status !== 'running') return;
    this.status = 'paused';
    this.clearSchedule();
  }

  // One block, then paused.
  step() {
    if (!this.spec || this.done >= this.total) return;
    this.pause();
    if (this.status === 'idle') this.status = 'paused';
    if (!this.pending) this.request(1);
  }

  destroy() {
    this.clearSchedule();
    this.worker.terminate();
    document.removeEventListener('visibilitychange', this.onVisibility);
  }

  private onVisibility = () => {
    if (document.hidden && this.status === 'running') {
      this.pause();
      this.pausedWhileHidden = true;
    } else if (!document.hidden && this.pausedWhileHidden) {
      this.pausedWhileHidden = false;
      this.play();
    }
  };

  private pump = () => {
    this.timer = null;
    this.frame = null;
    if (this.status !== 'running' || this.pending) return;
    const {blocks} = PACE[this.speed];
    this.request(this.nextBlock < RECORD_BLOCKS ? Math.min(blocks, MAX_DRAWN_BLOCKS) : blocks);
  };

  private request(blocks: number) {
    if (!this.spec) return;
    const remaining = Math.ceil((this.total - this.done) / BLOCK_PATHS);
    const job: PathJob = {
      id: this.id,
      tarf: this.spec,
      seed: SEED,
      blockPaths: BLOCK_PATHS,
      firstBlock: this.nextBlock,
      blocks: Math.min(blocks, remaining),
      recordBlocks: RECORD_BLOCKS,
    };
    this.pending = true;
    this.worker.postMessage(job);
  }

  private absorb(reply: PathReply) {
    // Replies for an earlier run are dropped.
    if (reply.id !== this.id) return;
    this.pending = false;
    if (reply.error !== undefined) {
      this.error = reply.error;
      this.status = 'paused';
      return;
    }

    const n = Math.min(reply.npvs.length, this.total - this.done);
    for (let k = 0; k < n; ++k) {
      const npv = reply.npvs[k];
      this.npvs[this.done + k] = npv;
      this.knockouts[this.done + k] = reply.knockouts[k];
      this.sum += npv;
      this.sumSquares += npv * npv;
    }
    this.done += n;
    this.nextBlock = reply.firstBlock + reply.blocks;
    this.forwards ??= reply.forwards;
    this.history = [...this.history, {paths: this.done, mean: this.mean, halfWidth: this.halfWidth}];
    this.version += 1;
    if (reply.recorded > 0) {
      this.onPaths({
        count: reply.recorded,
        spots: reply.spots,
        profits: reply.profits,
        knockouts: reply.knockouts.subarray(0, reply.recorded),
        times: reply.times,
        fixingTimes: reply.fixingTimes,
      });
    }

    if (this.done >= this.total) {
      this.status = 'done';
    } else if (this.status === 'running') {
      const {delay} = PACE[this.speed];
      if (delay === 0) this.frame = requestAnimationFrame(this.pump);
      else this.timer = setTimeout(this.pump, delay);
    }
  }

  private clearSchedule() {
    if (this.timer !== null) clearTimeout(this.timer);
    if (this.frame !== null) cancelAnimationFrame(this.frame);
    this.timer = null;
    this.frame = null;
  }
}
