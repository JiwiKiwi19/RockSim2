// Types for the Emscripten module built from bindings/wasm/bindings.cpp.
export interface ParamSpec {
  key: string; label: string; unit: string; min: number; max: number; default: number;
  step: number; integer: boolean; live: boolean; help: string;
}
export interface ScenarioSpec {
  id: string; title: string; summary: string; units: string; validation: string; tryThis: string[];
  defaultIntegrator: string; defaultDt: number; params: ParamSpec[];
}
export interface IntegratorSpec { id: string; name: string; order: number; symplectic: boolean }
export interface Invariants {
  kinetic: number; potential: number; total: number; px: number; py: number;
  angularMomentum: number; comX: number; comY: number;
}
export interface Diagnostics {
  time: number; steps: number; now: Invariants; initial: Invariants; relativeEnergyError: number;
  contacts: number; impulses: number; wallHits: number; droppedTime: number; forceEvaluations: number;
  status: 'ok' | 'unstable'; statusMessage: string;
  dt: number; integrator: string; grabbed: boolean; eventCount: number; headStep: number; headTime: number;
  /** Exact oscillator position at this run's time (oscillator scenario only). */
  referenceX?: number;
}
export interface Result { ok: boolean; errors: string[] }
export interface CommandResult extends Result { branched: boolean }
export interface Meta {
  scenario: string; xmin: number; xmax: number; ymin: number; ymax: number; referencePeriod: number;
  walls: boolean; boxXmin: number; boxXmax: number; boxYmin: number; boxYmax: number;
}
export interface TimelineState {
  runs: number; running: boolean; cursorStep: number; cursorTime: number; oldestStep: number; oldestTime: number;
  seekLimit: number; stepsPerSecond: number; divergence: number; selected: number; keyframeBytes: number;
  branchTime: number;
}
export interface SeriesViews {
  step: Float64Array; t: Float64Array; K: Float64Array; U: Float64Array; E: Float64Array;
  rel: Float64Array; div: Float64Array; numX: Float64Array; refX: Float64Array;
}
export interface WasmSession {
  load(scenario: string, params: Record<string, number>, integrator: string, dt: number): Result;
  play(): void; pause(): void; running(): boolean; setSpeed(v: number): void;
  tick(wallSeconds: number, lastComputeMs: number): number;
  step(n: number): number;
  seek(step: number): number; seekTime(t: number): number; stepBack(n: number): number;
  grabStart(run: number, body: number, x: number, y: number): CommandResult;
  grabMove(run: number, x: number, y: number): CommandResult;
  grabEnd(run: number): CommandResult;
  setParam(run: number, key: string, value: number): CommandResult;
  setDt(run: number, dt: number): CommandResult;
  setIntegrator(run: number, id: string): CommandResult;
  setBody(run: number, body: number, x: number, y: number, vx: number, vy: number): CommandResult;
  pick(run: number, x: number, y: number, tolerance: number): number;
  selectBody(body: number): void;
  runCount(): number;
  count(): number;
  x(run: number): Float64Array; y(run: number): Float64Array;
  vx(run: number): Float64Array; vy(run: number): Float64Array;
  radius(): Float64Array; mass(): Float64Array; fixed(): Uint8Array;
  springs(): Uint32Array; springRest(): Float64Array;
  trailPoints(run: number): Float64Array; trailCounts(): Float64Array; traced(): number[];
  /** Flat (step, time, kind) triples. */
  events(run: number): Float64Array;
  series(run: number): SeriesViews;
  diagnostics(run: number): Diagnostics;
  liveParams(run: number): Record<string, number>;
  timeline(): TimelineState;
  hash(run: number): string;
  meta(): Meta;
  delete(): void;
}
export interface ConvergenceResult {
  ok: boolean; integrator: string; observedOrder: number;
  points: { dt: number; error: number; steps: number; forceEvaluations: number }[];
}
export interface DriftResult { ok: boolean; integrator: string; t: number[]; error: number[] }
export interface RockSimModule {
  Session: new () => WasmSession;
  catalog(): ScenarioSpec[];
  integrators(): IntegratorSpec[];
  convergence(integrator: string, damping: number, levels: number): ConvergenceResult;
  energyDrift(integrator: string, dt: number, eccentricity: number, orbits: number, samples: number): DriftResult;
}
declare const createRockSim: () => Promise<RockSimModule>;
export default createRockSim;
