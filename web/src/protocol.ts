// Messages between the UI thread and the engine worker. The worker owns the
// WebAssembly instance and the time loop; the UI only sends commands and
// renders the latest frame, so a slow step never blocks input.
import type {
  ConvergenceResult, Diagnostics, DriftResult, IntegratorSpec, Meta, ParamSpec, ScenarioSpec,
} from './wasm/rocksim.mjs';

export type { ConvergenceResult, Diagnostics, DriftResult, IntegratorSpec, Meta, ParamSpec, ScenarioSpec };

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
  | { type: 'speed'; value: number }
  | { type: 'live'; key: string; value: number }
  | { type: 'integrator'; id: string }
  | { type: 'dt'; value: number }
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

/** Per-frame data: copies of the state, safe to transfer. */
export interface Frame {
  x: Float64Array;
  y: Float64Array;
  vx: Float64Array;
  vy: Float64Array;
  diag: Diagnostics;
  hash: string;
  running: boolean;
  stepsPerSecond: number;
  /** Closed-form oscillator solution at the current time (oscillator scenario only). */
  reference?: { x: number; numericX: number };
}

export type FromWorker =
  | { type: 'ready'; catalog: ScenarioSpec[]; integrators: IntegratorSpec[] }
  | { type: 'fatal'; message: string }
  | { type: 'loaded'; ok: true; topology: Topology }
  | { type: 'loaded'; ok: false; errors: string[] }
  | { type: 'frame'; frame: Frame }
  | { type: 'live'; key: string; ok: boolean; errors: string[] }
  | { type: 'convergence'; results: ConvergenceResult[]; damping: number }
  | { type: 'drift'; results: DriftResult[]; dt: number; eccentricity: number; orbits: number };
