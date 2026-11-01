// Simulates TARF paths for the live run, a few blocks at a time. A block is a
// fixed number of paths seeded by its own index, so the run's paths are the
// same however it is split into requests: paused, stepped, or run at any
// speed.

import type {TarfSpec} from './engine';

// The engine is imported when the first message arrives, not at the top of
// the module: a static import would delay registering onmessage until the
// WebAssembly had loaded, and messages arriving before then would be lost.
const engineModule = import('./engine');

export type PathJob = {
  id: number;
  tarf: TarfSpec;
  seed: number;
  blockPaths: number;
  firstBlock: number;
  blocks: number;
  // Blocks with a lower index have every path recorded in full.
  recordBlocks: number;
};

export type PathReply =
  | {
      id: number;
      error?: undefined;
      firstBlock: number;
      blocks: number;
      npvs: Float64Array;
      knockouts: Int32Array;
      // The recorded paths, row by row: the FX rate at each of `times`, and
      // the profit accumulated towards the target at each fixing.
      recorded: number;
      spots: Float64Array;
      profits: Float64Array;
      times: Float64Array;
      fixingTimes: Float64Array;
      forwards: Float64Array;
    }
  | {id: number; error: string};

function concat<T extends Float64Array | Int32Array>(parts: T[], make: (n: number) => T): T {
  const out = make(parts.reduce((n, p) => n + p.length, 0));
  let offset = 0;
  for (const p of parts) {
    out.set(p, offset);
    offset += p.length;
  }
  return out;
}

self.onmessage = async (e: MessageEvent<PathJob>) => {
  const job = e.data;
  try {
    const {engine} = await engineModule;
    const batches = Array.from({length: job.blocks}, (_, k) => {
      const block = job.firstBlock + k;
      return engine.simulateTarfBatch({
        tarf: job.tarf,
        paths: job.blockPaths,
        seed: job.seed,
        batchIndex: block,
        recordedPaths: block < job.recordBlocks ? job.blockPaths : 0,
      });
    });
    const first = batches[0];
    const reply: PathReply = {
      id: job.id,
      firstBlock: job.firstBlock,
      blocks: job.blocks,
      npvs: concat(batches.map((b) => b.npvs), (n) => new Float64Array(n)),
      knockouts: concat(batches.map((b) => b.knockoutFixings), (n) => new Int32Array(n)),
      recorded: batches.reduce((n, b) => n + b.recordedPaths, 0),
      spots: concat(batches.map((b) => b.spots), (n) => new Float64Array(n)),
      profits: concat(batches.map((b) => b.accumulatedProfits), (n) => new Float64Array(n)),
      times: first.times,
      fixingTimes: first.fixingTimes,
      forwards: first.forwards,
    };
    // Transferred rather than copied.
    const transfer = [reply.npvs, reply.knockouts, reply.spots, reply.profits].map((a) => a.buffer as ArrayBuffer);
    self.postMessage(reply, {transfer});
  } catch (err) {
    self.postMessage({id: job.id, error: err instanceof Error ? err.message : String(err)} satisfies PathReply);
  }
};
