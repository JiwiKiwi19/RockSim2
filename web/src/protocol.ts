// Messages between the UI thread and the engine worker. The worker owns the
// WebAssembly session and marshals; the UI sends input and draws what comes
// back. Frames arrive whenever the state changes, series at 10 Hz.
import type {
  CommandResult, ConvergenceResult, Diagnostics, DriftResult, IntegratorSpec, Meta, ParamSpec, ScenarioSpec,
  TimelineState,
} from './wasm/rocksim.mjs';

export type {
  CommandResult, ConvergenceResult, Diagnostics, DriftResult, IntegratorSpec, Meta, ParamSpec, ScenarioSpec,
  TimelineState,
};

export interface LoadRequest {
  scenario: string;
  params: Record<string, number>;
  integrator: string;
  dt: number;
}

export type ToWorker =
  | { type: 'load'; req: LoadRequest }
  | { type: 'play' }
  | { type: 'pause' }
  | { type: 'step'; n: number }
  | { type: 'stepBack'; n: number }
  | { type: 'speed'; value: number }
  | { type: 'seek'; step?: number; time?: number }
  | { type: 'live'; run: number; key: string; value: number }
  | { type: 'integrator'; run: number; id: string }
  | { type: 'dt'; run: number; value: number }
  | { type: 'grabStart'; run: number; body: number; x: number; y: number }
  | { type: 'grabMove'; run: number; x: number; y: number }
  | { type: 'grabEnd'; run: number }
  | { type: 'setBody'; run: number; body: number; x: number; y: number; vx: number; vy: number }
  | { type: 'pick'; id: number; run: number; x: number; y: number; tolerance: number }
  | { type: 'selectBody'; body: number }
  | { type: 'convergence'; damping: number; levels: number }
  | { type: 'drift'; dt: number; eccentricity: number; orbits: number };

/** Static per-load data: topology and per-body constants. */
export interface Topology {
  meta: Meta;
  count: number;
  radius: Float64Array;
  mass: Float64Array;
  fixed: Uint8Array;
  springs: Uint32Array;
  springRest: Float64Array;
}

/** One run's state: copies, safe to transfer. */
export interface RunFrame {
  x: Float64Array;
  y: Float64Array;
  vx: Float64Array;
  vy: Float64Array;
  diag: Diagnostics;
  hash: string;
  /** Current values of the parameters that can change while running. */
  liveParams: Record<string, number>;
  /** (x, y) points of every traced body, oldest first; trailCounts[k] points belong to the k-th traced body. */
  trails: Float64Array;
  trailCounts: Float64Array;
  /** Logged user actions as (step, time, kind) triples. */
  events: Float64Array;
}

export interface Frame {
  runs: RunFrame[];
  timeline: TimelineState;
  traced: number[];
}

/** Chart columns of one run. */
export interface SeriesData {
  step: Float64Array;
  t: Float64Array;
  K: Float64Array;
  U: Float64Array;
  E: Float64Array;
  rel: Float64Array;
  div: Float64Array;
  numX: Float64Array;
  refX: Float64Array;
}

export type FromWorker =
  | { type: 'ready'; catalog: ScenarioSpec[]; integrators: IntegratorSpec[] }
  | { type: 'fatal'; message: string }
  | { type: 'loaded'; ok: true; topology: Topology }
  | { type: 'loaded'; ok: false; errors: string[] }
  | { type: 'frame'; frame: Frame }
  | { type: 'series'; runs: SeriesData[] }
  | { type: 'result'; what: 'live' | 'integrator' | 'dt' | 'setBody' | 'grab'; key?: string; result: CommandResult }
  | { type: 'picked'; id: number; body: number }
  | { type: 'convergence'; results: ConvergenceResult[]; damping: number }
  | { type: 'drift'; results: DriftResult[]; dt: number; eccentricity: number; orbits: number };
