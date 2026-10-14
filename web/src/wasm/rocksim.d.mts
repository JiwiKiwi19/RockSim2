// Types for the Emscripten module built from bindings/wasm/bindings.cpp.
export interface ParamSpec {
  key: string; label: string; unit: string; min: number; max: number; default: number;
  step: number; integer: boolean; live: boolean; help: string;
}
export interface ScenarioSpec {
  id: string; title: string; summary: string; units: string; validation: string;
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
}
export interface Result { ok: boolean; errors: string[] }
export interface Meta {
  scenario: string; xmin: number; xmax: number; ymin: number; ymax: number; referencePeriod: number;
  walls: boolean; boxXmin: number; boxXmax: number; boxYmin: number; boxYmax: number; dt: number; integrator: string;
}
export interface WasmSimulation {
  load(scenario: string, params: Record<string, number>, integrator: string, dt: number): Result;
  advance(simSeconds: number, maxSteps: number): number;
  stepN(n: number): number;
  setLiveParam(key: string, value: number): Result;
  setIntegrator(id: string): boolean;
  setDt(dt: number): boolean;
  count(): number;
  x(): Float64Array; y(): Float64Array; vx(): Float64Array; vy(): Float64Array;
  radius(): Float64Array; mass(): Float64Array; fixed(): Uint8Array;
  springs(): Uint32Array; springRest(): Float64Array;
  diagnostics(): Diagnostics;
  meta(): Meta;
  hash(): string;
  delete(): void;
}
export interface ConvergenceResult {
  ok: boolean; integrator: string; observedOrder: number;
  points: { dt: number; error: number; steps: number; forceEvaluations: number }[];
}
export interface DriftResult { ok: boolean; integrator: string; t: number[]; error: number[] }
export interface RockSimModule {
  Simulation: new () => WasmSimulation;
  catalog(): ScenarioSpec[];
  integrators(): IntegratorSpec[];
  oscillatorExact(m: number, k: number, c: number, x0: number, v0: number, t: number): { x: number; v: number };
  convergence(integrator: string, damping: number, levels: number): ConvergenceResult;
  energyDrift(integrator: string, dt: number, eccentricity: number, orbits: number, samples: number): DriftResult;
}
declare const createRockSim: () => Promise<RockSimModule>;
export default createRockSim;
